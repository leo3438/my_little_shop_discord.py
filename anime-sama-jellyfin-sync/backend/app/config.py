"""Chargement de la configuration depuis l'environnement / le fichier .env."""

from __future__ import annotations

from functools import lru_cache
from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Paramètres applicatifs (lus depuis le fichier `.env`)."""

    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    # Serveur HTTP
    host: str = "127.0.0.1"
    port: int = 8000
    cors_origins: str = "*"

    # Base de données
    database_url: str = "sqlite:///./data/episodes.db"

    # Stockage
    media_root: Path = Path("./media")
    # Playlist M3U pour la section Live TV de Jellyfin. Vide => {media_root}/iptv.m3u
    iptv_playlist_path: str = ""

    # Téléchargement
    downloader: str = "yt-dlp"  # "yt-dlp" | "aria2c"
    ytdlp_path: str = "yt-dlp"
    aria2c_path: str = "aria2c"
    download_user_agent: str = (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
    )
    max_concurrent_downloads: int = 1

    # Jellyfin
    jellyfin_url: str = "http://localhost:8096"
    jellyfin_api_key: str = ""
    jellyfin_refresh_enabled: bool = True

    @property
    def iptv_playlist(self) -> Path:
        """Chemin résolu de la playlist IPTV (défaut : {media_root}/iptv.m3u)."""
        if self.iptv_playlist_path.strip():
            return Path(self.iptv_playlist_path).expanduser().resolve()
        return Path(self.media_root).expanduser().resolve() / "iptv.m3u"

    @property
    def cors_origin_list(self) -> list[str]:
        """Retourne les origines CORS sous forme de liste."""
        raw = self.cors_origins.strip()
        if raw == "*" or not raw:
            return ["*"]
        return [origin.strip() for origin in raw.split(",") if origin.strip()]


@lru_cache
def get_settings() -> Settings:
    """Instance unique (mise en cache) des paramètres."""
    return Settings()
