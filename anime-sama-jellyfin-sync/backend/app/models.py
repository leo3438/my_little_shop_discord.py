"""Modèles de base de données (SQLModel)."""

from __future__ import annotations

from datetime import datetime, timezone
from enum import Enum

from sqlmodel import Field, SQLModel, UniqueConstraint


class EpisodeStatus(str, Enum):
    """Cycle de vie d'un épisode côté backend."""

    PENDING = "pending"          # reçu, en attente de téléchargement
    DOWNLOADING = "downloading"  # téléchargement en cours
    DOWNLOADED = "downloaded"    # fichier prêt et rangé pour Jellyfin
    FAILED = "failed"            # le téléchargement a échoué


def _utcnow() -> datetime:
    return datetime.now(timezone.utc)


class Episode(SQLModel, table=True):
    """Un épisode capturé sur Anime-Sama.

    L'unicité (anime + saison + épisode + langue) empêche les doublons :
    re-cliquer sur un épisode déjà connu ne relance pas un téléchargement.
    """

    __tablename__ = "episodes"
    __table_args__ = (
        UniqueConstraint(
            "anime_title",
            "season",
            "episode_number",
            "language",
            name="uq_episode_identity",
        ),
    )

    id: int | None = Field(default=None, primary_key=True)

    anime_title: str = Field(index=True)
    season: int = Field(default=1)
    episode_number: int
    language: str = Field(default="VOSTFR")  # VOSTFR | VF

    video_url: str
    # En-têtes HTTP nécessaires au téléchargement (Referer, User-Agent...),
    # sérialisés en JSON.
    request_headers: str = Field(default="{}")

    file_path: str | None = Field(default=None)
    status: EpisodeStatus = Field(default=EpisodeStatus.PENDING, index=True)
    error: str | None = Field(default=None)

    created_at: datetime = Field(default_factory=_utcnow)
    downloaded_at: datetime | None = Field(default=None)
