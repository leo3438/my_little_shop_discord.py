"""Modèles de base de données (SQLModel)."""

from __future__ import annotations

from datetime import datetime, timezone
from enum import Enum

from sqlmodel import Field, SQLModel


class EpisodeStatus(str, Enum):
    """Cycle de vie d'un contenu côté backend."""

    PENDING = "pending"          # reçu, en attente de traitement
    DOWNLOADING = "downloading"  # téléchargement en cours
    DOWNLOADED = "downloaded"    # fichier prêt et rangé pour Jellyfin
    REGISTERED = "registered"    # chaîne Live TV inscrite dans la playlist
    FAILED = "failed"            # le traitement a échoué


class MediaType(str, Enum):
    """Type de média capturé."""

    SERIES = "series"
    MANGA = "manga"
    MOVIE = "movie"
    LIVE_TV = "live_tv"


def _utcnow() -> datetime:
    return datetime.now(timezone.utc)


class Episode(SQLModel, table=True):
    """Un contenu capturé (Anime-Sama ou Afterdark).

    La déduplication dépend du type de média et est gérée applicativement
    (voir `main.py`), car les colonnes optionnelles (year, channel_name)
    rendraient une contrainte UNIQUE peu fiable sous SQLite (NULL distincts).
    """

    __tablename__ = "episodes"

    id: int | None = Field(default=None, primary_key=True)

    media_type: MediaType = Field(default=MediaType.SERIES, index=True)

    # Titre de l'œuvre (série/film) ou nom de chaîne (rempli aussi dans
    # `channel_name` pour la TV en direct). Conservé sous `anime_title` pour
    # compatibilité avec la première version.
    anime_title: str = Field(index=True)
    season: int = Field(default=1)
    episode_number: int = Field(default=0)
    language: str = Field(default="VOSTFR")

    # Champs spécifiques selon le type de média.
    year: int | None = Field(default=None)          # films
    channel_name: str | None = Field(default=None)  # live_tv

    video_url: str
    # En-têtes HTTP nécessaires (Referer, User-Agent...), sérialisés en JSON.
    request_headers: str = Field(default="{}")

    file_path: str | None = Field(default=None)
    status: EpisodeStatus = Field(default=EpisodeStatus.PENDING, index=True)
    error: str | None = Field(default=None)

    created_at: datetime = Field(default_factory=_utcnow)
    downloaded_at: datetime | None = Field(default=None)
