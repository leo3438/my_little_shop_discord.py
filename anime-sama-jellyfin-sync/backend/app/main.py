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
from .models import Episode, EpisodeStatus
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


@app.post("/api/episode", response_model=EpisodeAccepted, status_code=202)
def receive_episode(
    payload: EpisodeIn,
    background_tasks: BackgroundTasks,
    session: Session = Depends(get_session),
) -> EpisodeAccepted:
    """Reçoit un épisode capturé par l'extension.

    - Vérifie les doublons (anime + saison + épisode + langue).
    - Sauvegarde l'entrée.
    - Lance le téléchargement en tâche de fond.
    """
    existing = session.exec(
        select(Episode).where(
            Episode.anime_title == payload.anime_title,
            Episode.season == payload.season,
            Episode.episode_number == payload.episode_number,
            Episode.language == payload.language,
        )
    ).first()

    if existing is not None:
        # Si un précédent essai a échoué, on autorise une relance.
        if existing.status == EpisodeStatus.FAILED:
            existing.status = EpisodeStatus.PENDING
            existing.video_url = payload.video_url
            existing.request_headers = json.dumps(payload.headers)
            existing.error = None
            session.add(existing)
            session.commit()
            session.refresh(existing)
            background_tasks.add_task(process_episode, existing.id)
            return EpisodeAccepted(
                id=existing.id,
                status=existing.status,
                duplicate=True,
                detail="Épisode déjà connu (échec précédent) : téléchargement relancé.",
            )

        return EpisodeAccepted(
            id=existing.id,
            status=existing.status,
            duplicate=True,
            detail="Épisode déjà enregistré : aucune action.",
        )

    episode = Episode(
        anime_title=payload.anime_title,
        season=payload.season,
        episode_number=payload.episode_number,
        language=payload.language,
        video_url=payload.video_url,
        request_headers=json.dumps(payload.headers),
        status=EpisodeStatus.PENDING,
    )
    session.add(episode)
    session.commit()
    session.refresh(episode)

    background_tasks.add_task(process_episode, episode.id)
    logger.info(
        "Épisode reçu : %s S%02dE%02d [%s]",
        episode.anime_title,
        episode.season,
        episode.episode_number,
        episode.language,
    )

    return EpisodeAccepted(
        id=episode.id,
        status=episode.status,
        duplicate=False,
        detail="Épisode accepté : téléchargement lancé.",
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
