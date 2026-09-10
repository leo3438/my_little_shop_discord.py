"""Worker de téléchargement asynchrone + rangement au format Jellyfin."""

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
from .models import Episode, EpisodeStatus

logger = logging.getLogger("anime_sama.downloader")

# Limite le nombre de téléchargements simultanés.
_semaphore: asyncio.Semaphore | None = None


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
    cleaned = _ILLEGAL.sub("", name).strip().rstrip(".")
    cleaned = re.sub(r"\s+", " ", cleaned)
    return cleaned or "Unknown"


def build_target_path(episode: Episode) -> Path:
    """Construit le chemin final selon la convention Jellyfin :

    /{Anime Title}/Season {SS}/{Anime Title} - S{SS}E{EE} [{Language}].mp4
    """
    settings = get_settings()
    title = sanitize(episode.anime_title)
    season = f"{episode.season:02d}"
    ep = f"{episode.episode_number:02d}"
    lang = sanitize(episode.language)

    filename = f"{title} - S{season}E{ep} [{lang}].mp4"
    return (
        Path(settings.media_root).expanduser().resolve()
        / title
        / f"Season {season}"
        / filename
    )


# --------------------------------------------------------------------------- #
# Construction des commandes de téléchargement
# --------------------------------------------------------------------------- #
def _build_command(
    video_url: str,
    headers: dict[str, str],
    target: Path,
) -> list[str]:
    """Construit la commande yt-dlp ou aria2c avec les bons en-têtes."""
    settings = get_settings()
    user_agent = headers.get("User-Agent") or settings.download_user_agent
    referer = headers.get("Referer") or headers.get("referer") or ""

    if settings.downloader == "aria2c":
        # aria2c : téléchargement direct (ne gère pas les playlists .m3u8).
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

    # yt-dlp par défaut : gère .mp4, .m3u8 et de nombreux hébergeurs.
    cmd = [
        settings.ytdlp_path,
        "--no-playlist",
        "--no-part",
        "--retries", "5",
        "--fragment-retries", "10",
        "--remux-video", "mp4",  # garantit un conteneur .mp4 en sortie
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
    """Exécute une commande externe et retourne (code, sortie combinée)."""
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
    """Télécharge l'épisode puis déclenche le rafraîchissement Jellyfin.

    Appelé via `BackgroundTasks`. Toute exception est capturée et enregistrée
    en base afin de ne jamais faire planter le serveur.
    """
    async with _get_semaphore():
        with Session(engine) as session:
            episode = session.get(Episode, episode_id)
            if episode is None:
                logger.error("Épisode %s introuvable.", episode_id)
                return

            headers = json.loads(episode.request_headers or "{}")
            target = build_target_path(episode)
            target.parent.mkdir(parents=True, exist_ok=True)

            episode.status = EpisodeStatus.DOWNLOADING
            session.add(episode)
            session.commit()

            cmd = _build_command(episode.video_url, headers, target)

        try:
            code, tail = await _run(cmd)
        except FileNotFoundError as exc:
            code, tail = 127, f"Binaire de téléchargement introuvable : {exc}"
        except Exception as exc:  # noqa: BLE001 — on veut tout capturer
            code, tail = 1, f"Erreur inattendue : {exc}"

        success = code == 0 and target.exists()

        with Session(engine) as session:
            episode = session.get(Episode, episode_id)
            if episode is None:
                return
            if success:
                episode.status = EpisodeStatus.DOWNLOADED
                episode.file_path = str(target)
                episode.downloaded_at = datetime.now(timezone.utc)
                episode.error = None
                logger.info("Téléchargé : %s", target)
            else:
                episode.status = EpisodeStatus.FAILED
                episode.error = f"code={code} :: {tail[-500:]}"
                logger.error("Échec du téléchargement (%s) : %s", code, tail[-300:])
            session.add(episode)
            session.commit()

    if success:
        await refresh_library()
