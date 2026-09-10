"""Application FastAPI : reçoit les webhooks de l'extension."""

from __future__ import annotations

import json
import logging
from contextlib import asynccontextmanager

from fastapi import BackgroundTasks, Depends, FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from sqlmodel import Session, select

from .config import get_settings
from .database import get_session, init_db
from .downloader import process_episode
from .models import Episode, EpisodeStatus, MediaType
from .schemas import EpisodeAccepted, EpisodeIn, EpisodeOut

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("anime_sama.api")

settings = get_settings()


@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    logger.info("Base de données initialisée. Médias -> %s", settings.media_root)
    yield


app = FastAPI(
    title="Anime-Sama -> Jellyfin Sync",
    version="1.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origin_list,
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


def _find_existing(session: Session, payload: EpisodeIn) -> Episode | None:
    """Recherche un contenu déjà connu, selon le type de média."""
    stmt = select(Episode).where(
        Episode.media_type == payload.media_type,
        Episode.language == payload.language,
    )
    if payload.media_type == MediaType.LIVE_TV:
        stmt = stmt.where(Episode.channel_name == payload.channel_name)
    elif payload.media_type == MediaType.MOVIE:
        stmt = stmt.where(
            Episode.anime_title == payload.anime_title,
            Episode.year == payload.year,
        )
    else:  # series / manga
        stmt = stmt.where(
            Episode.anime_title == payload.anime_title,
            Episode.season == payload.season,
            Episode.episode_number == payload.episode_number,
        )
    return session.exec(stmt).first()


@app.post("/api/episode", response_model=EpisodeAccepted, status_code=202)
def receive_episode(
    payload: EpisodeIn,
    background_tasks: BackgroundTasks,
    session: Session = Depends(get_session),
) -> EpisodeAccepted:
    """Reçoit un contenu capturé par l'extension.

    - Déduplique selon le type de média.
    - Sauvegarde l'entrée puis lance le traitement en tâche de fond.
    - Live TV : l'entrée est toujours (ré)inscrite dans la playlist (upsert).
    """
    existing = _find_existing(session, payload)

    if existing is not None:
        # Live TV : on met toujours à jour l'URL du flux et on réinscrit.
        if payload.media_type == MediaType.LIVE_TV:
            existing.video_url = payload.video_url
            existing.request_headers = json.dumps(payload.headers)
            existing.status = EpisodeStatus.PENDING
            existing.error = None
            session.add(existing)
            session.commit()
            session.refresh(existing)
            background_tasks.add_task(process_episode, existing.id)
            return EpisodeAccepted(
                id=existing.id,
                status=existing.status,
                duplicate=True,
                detail="Chaîne Live TV connue : playlist mise à jour.",
            )

        # Movie / Series : on relance uniquement si l'essai précédent a échoué.
        if existing.status == EpisodeStatus.FAILED:
            existing.video_url = payload.video_url
            existing.request_headers = json.dumps(payload.headers)
            existing.status = EpisodeStatus.PENDING
            existing.error = None
            session.add(existing)
            session.commit()
            session.refresh(existing)
            background_tasks.add_task(process_episode, existing.id)
            return EpisodeAccepted(
                id=existing.id,
                status=existing.status,
                duplicate=True,
                detail="Contenu déjà connu (échec précédent) : traitement relancé.",
            )

        return EpisodeAccepted(
            id=existing.id,
            status=existing.status,
            duplicate=True,
            detail="Contenu déjà enregistré : aucune action.",
        )

    episode = Episode(
        media_type=payload.media_type,
        anime_title=payload.anime_title,
        season=payload.season,
        episode_number=payload.episode_number,
        language=payload.language,
        year=payload.year,
        channel_name=payload.channel_name,
        video_url=payload.video_url,
        request_headers=json.dumps(payload.headers),
        status=EpisodeStatus.PENDING,
    )
    session.add(episode)
    session.commit()
    session.refresh(episode)

    background_tasks.add_task(process_episode, episode.id)
    logger.info(
        "Contenu reçu [%s] : %s (S%02dE%02d / %s / %s)",
        episode.media_type.value,
        episode.anime_title,
        episode.season,
        episode.episode_number,
        episode.year,
        episode.language,
    )

    detail = (
        "Chaîne Live TV acceptée : inscription en cours."
        if payload.media_type == MediaType.LIVE_TV
        else "Contenu accepté : téléchargement lancé."
    )
    return EpisodeAccepted(
        id=episode.id,
        status=episode.status,
        duplicate=False,
        detail=detail,
    )


@app.get("/api/episodes", response_model=list[EpisodeOut])
def list_episodes(session: Session = Depends(get_session)) -> list[Episode]:
    """Liste tous les épisodes (les plus récents d'abord)."""
    return list(
        session.exec(select(Episode).order_by(Episode.created_at.desc())).all()
    )


@app.get("/api/episodes/{episode_id}", response_model=EpisodeOut)
def get_episode(
    episode_id: int, session: Session = Depends(get_session)
) -> Episode:
    episode = session.get(Episode, episode_id)
    if episode is None:
        raise HTTPException(status_code=404, detail="Épisode introuvable.")
    return episode
