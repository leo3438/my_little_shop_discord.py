"""Schémas Pydantic pour l'API (entrée / sortie)."""

from __future__ import annotations

from datetime import datetime

from pydantic import BaseModel, Field, field_validator

from .models import EpisodeStatus


class EpisodeIn(BaseModel):
    """Payload envoyé par l'extension de navigateur."""

    anime_title: str = Field(..., min_length=1)
    season: int = Field(default=1, ge=0)
    episode_number: int = Field(..., ge=0)
    language: str = Field(default="VOSTFR")

    video_url: str = Field(..., min_length=1)
    # En-têtes utiles au téléchargement (au minimum le Referer).
    headers: dict[str, str] = Field(default_factory=dict)

    @field_validator("language")
    @classmethod
    def _normalize_language(cls, value: str) -> str:
        value = (value or "").strip().upper()
        return value or "VOSTFR"

    @field_validator("anime_title")
    @classmethod
    def _strip_title(cls, value: str) -> str:
        return value.strip()


class EpisodeOut(BaseModel):
    """Représentation renvoyée par l'API."""

    id: int
    anime_title: str
    season: int
    episode_number: int
    language: str
    video_url: str
    file_path: str | None
    status: EpisodeStatus
    error: str | None
    created_at: datetime
    downloaded_at: datetime | None

    model_config = {"from_attributes": True}


class EpisodeAccepted(BaseModel):
    """Réponse immédiate au webhook (avant la fin du téléchargement)."""

    id: int
    status: EpisodeStatus
    duplicate: bool = False
    detail: str
