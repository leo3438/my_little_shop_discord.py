"""Intégration avec l'API Jellyfin."""

from __future__ import annotations

import logging

import httpx

from .config import get_settings

logger = logging.getLogger("anime_sama.jellyfin")


async def refresh_library() -> bool:
    """Déclenche un scan de la bibliothèque Jellyfin.

    Retourne True si la requête a été acceptée par Jellyfin, False sinon.
    N'interrompt jamais le flux principal : les erreurs sont journalisées.
    """
    settings = get_settings()

    if not settings.jellyfin_refresh_enabled:
        logger.info("Rafraîchissement Jellyfin désactivé (JELLYFIN_REFRESH_ENABLED=false).")
        return False

    if not settings.jellyfin_api_key:
        logger.warning("JELLYFIN_API_KEY absente : rafraîchissement ignoré.")
        return False

    url = f"{settings.jellyfin_url.rstrip('/')}/Library/Refresh"
    # Jellyfin accepte la clé en query param ou via l'en-tête X-Emby-Token.
    params = {"api_key": settings.jellyfin_api_key}
    headers = {"X-Emby-Token": settings.jellyfin_api_key}

    try:
        async with httpx.AsyncClient(timeout=30) as client:
            resp = await client.post(url, params=params, headers=headers)
        if resp.status_code in (200, 204):
            logger.info("Bibliothèque Jellyfin en cours de rafraîchissement.")
            return True
        logger.warning(
            "Jellyfin a répondu %s lors du rafraîchissement : %s",
            resp.status_code,
            resp.text[:200],
        )
        return False
    except httpx.HTTPError as exc:  # réseau, timeout, DNS...
        logger.error("Échec de l'appel de rafraîchissement Jellyfin : %s", exc)
        return False
