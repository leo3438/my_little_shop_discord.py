"""Worker de traitement asynchrone.

Selon le type de média :
  - movie          -> {MEDIA_ROOT}/Movies/{Titre} ({Année})/{Titre} ({Année}) [{Langue}].mp4
  - series / manga -> {MEDIA_ROOT}/Shows/{Titre}/Season {SS}/{Titre} - S{SS}E{EE} [{Langue}].mp4
  - live_tv        -> pas de téléchargement : (dé)inscription dans la playlist iptv.m3u
"""

from __future__ import annotations

import asyncio
import json
import logging
import re
from datetime import datetime, timezone
from pathlib import Path

from sqlmodel import Session

from .config import get_settings
from .database import engine
from .jellyfin import refresh_library
from .models import Episode, EpisodeStatus, MediaType

logger = logging.getLogger("anime_sama.downloader")

_semaphore: asyncio.Semaphore | None = None
# Sérialise les écritures de la playlist IPTV (accès concurrent au fichier).
_iptv_lock = asyncio.Lock()


def _get_semaphore() -> asyncio.Semaphore:
    global _semaphore
    if _semaphore is None:
        _semaphore = asyncio.Semaphore(get_settings().max_concurrent_downloads)
    return _semaphore


# --------------------------------------------------------------------------- #
# Aides au nommage / chemins
# --------------------------------------------------------------------------- #
_ILLEGAL = re.compile(r'[<>:"/\\|?*\x00-\x1f]')


def sanitize(name: str) -> str:
    """Nettoie une chaîne pour un usage dans un chemin de fichier."""
    cleaned = _ILLEGAL.sub("", name or "").strip().rstrip(".")
    cleaned = re.sub(r"\s+", " ", cleaned)
    return cleaned or "Unknown"


def build_target_path(episode: Episode) -> Path:
    """Construit le chemin final selon le type de média et la convention Jellyfin."""
    root = Path(get_settings().media_root).expanduser().resolve()
    title = sanitize(episode.anime_title)
    lang = sanitize(episode.language)

    if episode.media_type == MediaType.MOVIE:
        year = episode.year or datetime.now(timezone.utc).year
        folder = f"{title} ({year})"
        filename = f"{title} ({year}) [{lang}].mp4"
        return root / "Movies" / folder / filename

    # series / manga
    season = f"{episode.season:02d}"
    ep = f"{episode.episode_number:02d}"
    filename = f"{title} - S{season}E{ep} [{lang}].mp4"
    return root / "Shows" / title / f"Season {season}" / filename


# --------------------------------------------------------------------------- #
# Live TV : gestion de la playlist iptv.m3u
# --------------------------------------------------------------------------- #
def _parse_m3u(text: str) -> dict[str, list[str]]:
    """Parse une playlist M3U en {nom_de_chaine: [lignes du bloc]}."""
    entries: dict[str, list[str]] = {}
    block: list[str] = []
    name: str | None = None
    for line in text.splitlines():
        if line.startswith("#EXTM3U"):
            continue
        if line.startswith("#EXTINF"):
            # Sauvegarde le bloc précédent avant d'en démarrer un nouveau.
            if name is not None and block:
                entries[name] = block
            block = [line]
            m = re.search(r'tvg-name="([^"]*)"', line) or re.search(r",(.*)$", line)
            name = (m.group(1).strip() if m else "").strip() or None
        elif line.strip():
            block.append(line)
    if name is not None and block:
        entries[name] = block
    return entries


def _render_m3u(entries: dict[str, list[str]]) -> str:
    out = ["#EXTM3U"]
    for block in entries.values():
        out.extend(block)
    return "\n".join(out) + "\n"


async def update_iptv_playlist(episode: Episode, headers: dict[str, str]) -> Path:
    """Ajoute ou met à jour une chaîne dans la playlist IPTV locale."""
    settings = get_settings()
    playlist = settings.iptv_playlist
    channel = episode.channel_name or episode.anime_title
    referer = headers.get("Referer") or headers.get("referer") or ""
    user_agent = headers.get("User-Agent") or settings.download_user_agent

    block = [
        f'#EXTINF:-1 tvg-name="{channel}" group-title="Live TV",{channel}',
    ]
    if referer:
        block.append(f"#EXTVLCOPT:http-referrer={referer}")
    block.append(f"#EXTVLCOPT:http-user-agent={user_agent}")
    block.append(episode.video_url)

    async with _iptv_lock:
        playlist.parent.mkdir(parents=True, exist_ok=True)
        existing = playlist.read_text(encoding="utf-8") if playlist.exists() else ""
        entries = _parse_m3u(existing)
        entries[channel] = block  # upsert par nom de chaîne
        playlist.write_text(_render_m3u(entries), encoding="utf-8")

    logger.info("Chaîne Live TV inscrite/màj dans %s : %s", playlist, channel)
    return playlist


# --------------------------------------------------------------------------- #
# Construction des commandes de téléchargement (movie / series)
# --------------------------------------------------------------------------- #
def _build_command(
    video_url: str,
    headers: dict[str, str],
    target: Path,
) -> list[str]:
    settings = get_settings()
    user_agent = headers.get("User-Agent") or settings.download_user_agent
    referer = headers.get("Referer") or headers.get("referer") or ""

    if settings.downloader == "aria2c":
        cmd = [
            settings.aria2c_path,
            "--continue=true",
            "--auto-file-renaming=false",
            "--allow-overwrite=true",
            f"--user-agent={user_agent}",
            "--dir", str(target.parent),
            "--out", target.name,
        ]
        if referer:
            cmd.append(f"--referer={referer}")
        for key, value in headers.items():
            if key.lower() in ("user-agent", "referer"):
                continue
            cmd.append(f"--header={key}: {value}")
        cmd.append(video_url)
        return cmd

    cmd = [
        settings.ytdlp_path,
        "--no-playlist",
        "--no-part",
        "--retries", "5",
        "--fragment-retries", "10",
        "--remux-video", "mp4",
        "--user-agent", user_agent,
        "--output", str(target.with_suffix("")) + ".%(ext)s",
    ]
    if referer:
        cmd += ["--referer", referer]
    for key, value in headers.items():
        if key.lower() in ("user-agent", "referer"):
            continue
        cmd += ["--add-header", f"{key}:{value}"]
    cmd.append(video_url)
    return cmd


async def _run(cmd: list[str]) -> tuple[int, str]:
    logger.info("Commande : %s", " ".join(cmd))
    proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
    )
    output_chunks: list[str] = []
    assert proc.stdout is not None
    async for line in proc.stdout:
        text = line.decode(errors="replace").rstrip()
        output_chunks.append(text)
        logger.debug(text)
    await proc.wait()
    return proc.returncode or 0, "\n".join(output_chunks[-40:])


# --------------------------------------------------------------------------- #
# Point d'entrée de la tâche de fond
# --------------------------------------------------------------------------- #
async def process_episode(episode_id: int) -> None:
    """Traite un contenu (téléchargement ou inscription Live TV) puis Jellyfin."""
    async with _get_semaphore():
        with Session(engine) as session:
            episode = session.get(Episode, episode_id)
            if episode is None:
                logger.error("Contenu %s introuvable.", episode_id)
                return
            headers = json.loads(episode.request_headers or "{}")
            media_type = episode.media_type

            episode.status = EpisodeStatus.DOWNLOADING
            session.add(episode)
            session.commit()
            session.refresh(episode)
            # On détache une copie des champs nécessaires après fermeture.
            snapshot = episode

        # ---- Live TV : pas de téléchargement, on met à jour la playlist ---- #
        if media_type == MediaType.LIVE_TV:
            try:
                playlist = await update_iptv_playlist(snapshot, headers)
                _finalize(episode_id, EpisodeStatus.REGISTERED, str(playlist), None)
            except Exception as exc:  # noqa: BLE001
                _finalize(episode_id, EpisodeStatus.FAILED, None, f"iptv: {exc}")
                logger.error("Échec d'inscription Live TV : %s", exc)
            await refresh_library()
            return

        # ---- Movie / Series : téléchargement ------------------------------- #
        target = build_target_path(snapshot)
        target.parent.mkdir(parents=True, exist_ok=True)
        cmd = _build_command(snapshot.video_url, headers, target)

        try:
            code, tail = await _run(cmd)
        except FileNotFoundError as exc:
            code, tail = 127, f"Binaire de téléchargement introuvable : {exc}"
        except Exception as exc:  # noqa: BLE001
            code, tail = 1, f"Erreur inattendue : {exc}"

        success = code == 0 and target.exists()
        if success:
            _finalize(episode_id, EpisodeStatus.DOWNLOADED, str(target), None)
            logger.info("Téléchargé : %s", target)
        else:
            _finalize(
                episode_id,
                EpisodeStatus.FAILED,
                None,
                f"code={code} :: {tail[-500:]}",
            )
            logger.error("Échec du téléchargement (%s) : %s", code, tail[-300:])

    if success:
        await refresh_library()


def _finalize(
    episode_id: int,
    status: EpisodeStatus,
    file_path: str | None,
    error: str | None,
) -> None:
    """Écrit l'état final du contenu en base."""
    with Session(engine) as session:
        episode = session.get(Episode, episode_id)
        if episode is None:
            return
        episode.status = status
        if file_path is not None:
            episode.file_path = file_path
        episode.error = error
        if status in (EpisodeStatus.DOWNLOADED, EpisodeStatus.REGISTERED):
            episode.downloaded_at = datetime.now(timezone.utc)
        session.add(episode)
        session.commit()
