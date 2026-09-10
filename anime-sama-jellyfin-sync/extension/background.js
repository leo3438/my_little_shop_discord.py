/*
 * background.js (service worker MV3)
 * ----------------------------------
 * - Reçoit les métadonnées d'épisode envoyées par le content script.
 * - Intercepte les requêtes réseau pour capturer l'URL directe du flux vidéo
 *   (.mp4 / .m3u8 ou hébergeurs connus) ainsi que les en-têtes utiles (Referer,
 *   User-Agent, Origin...).
 * - Combine les deux et envoie un POST au backend local.
 */

"use strict";

// Compatibilité Chrome / Firefox.
const api = globalThis.browser || globalThis.chrome;

const DEFAULT_BACKEND = "http://localhost:8000/api/episode";

// Hébergeurs vidéo connus (anime-sama + afterdark). Sert d'indice
// supplémentaire ; la détection repose surtout sur l'extension (.mp4/.m3u8).
const VIDEO_HOSTS = [
  "sibnet.ru",
  "sendvid.com",
  "vidmoly",
  "myvi.top",
  "smoothpre.com",
  "anime-sama.fr",
  ".mom",
];

// En-têtes que l'on transmet au backend pour rejouer le téléchargement.
const RELEVANT_HEADERS = ["referer", "user-agent", "origin", "cookie"];

// État en mémoire : métadonnées et clé du dernier envoi, par onglet.
const tabMetadata = new Map(); // tabId -> metadata
const tabLastSent = new Map(); // tabId -> clé d'unicité déjà envoyée

/** Détermine si une URL correspond à un flux vidéo (et non un fragment .ts). */
function isVideoRequest(url) {
  const u = url.toLowerCase();
  if (u.includes("/seg-") || /\.ts(\?|$)/.test(u)) return false; // fragments HLS
  if (u.includes(".m3u8")) return true;
  if (/\.mp4(\?|$)/.test(u)) return true;
  // hébergeur connu servant explicitement une vidéo
  return VIDEO_HOSTS.some((h) => u.includes(h)) && u.includes("video");
}

/** Clé d'unicité d'un contenu (dépend du type de média). */
function episodeKey(meta) {
  if (meta.media_type === "live_tv") {
    return ["live_tv", meta.channel_name, meta.language].join("|");
  }
  if (meta.media_type === "movie") {
    return ["movie", meta.anime_title, meta.year, meta.language].join("|");
  }
  return [
    meta.media_type || "series",
    meta.anime_title,
    meta.season,
    meta.episode_number,
    meta.language,
  ].join("|");
}

/** Récupère l'URL du backend configurée (ou la valeur par défaut). */
async function getBackendUrl() {
  try {
    const stored = await api.storage.local.get("backendUrl");
    return stored.backendUrl || DEFAULT_BACKEND;
  } catch {
    return DEFAULT_BACKEND;
  }
}

/** Envoie le payload combiné au backend. */
async function sendToBackend(meta, videoUrl, headers) {
  const backendUrl = await getBackendUrl();
  const payload = {
    media_type: meta.media_type || "series",
    anime_title: meta.anime_title,
    season: meta.season,
    episode_number: meta.episode_number,
    language: meta.language,
    year: meta.year ?? null,
    channel_name: meta.channel_name ?? null,
    video_url: videoUrl,
    headers,
  };

  try {
    const resp = await fetch(backendUrl, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const body = await resp.json().catch(() => ({}));
    console.info("[AS->JF] Envoyé :", payload.anime_title,
      `S${meta.season}E${meta.episode_number}`, "->", resp.status, body);
    // Notification visuelle discrète via le badge.
    api.action?.setBadgeText?.({ text: resp.ok ? "OK" : "ERR" });
    api.action?.setBadgeBackgroundColor?.({ color: resp.ok ? "#2e7d32" : "#c62828" });
    setTimeout(() => api.action?.setBadgeText?.({ text: "" }), 4000);
  } catch (err) {
    console.error("[AS->JF] Échec de l'envoi au backend :", err);
    api.action?.setBadgeText?.({ text: "!" });
    api.action?.setBadgeBackgroundColor?.({ color: "#c62828" });
  }
}

// --- Réception des métadonnées depuis le content script -------------------- //
api.runtime.onMessage.addListener((message, sender) => {
  if (message?.type === "EPISODE_METADATA" && sender.tab) {
    tabMetadata.set(sender.tab.id, message.metadata);
    // Un changement d'épisode réautorise une capture pour cet onglet.
    tabLastSent.delete(sender.tab.id);
  }
  return false;
});

// --- Interception réseau : capture des en-têtes + de l'URL vidéo ----------- //
const extraInfoSpec = ["requestHeaders"];
// "extraHeaders" est requis par Chrome pour lire certains en-têtes (Referer...).
if (
  api.webRequest.OnSendHeadersOptions &&
  api.webRequest.OnSendHeadersOptions.EXTRA_HEADERS
) {
  extraInfoSpec.push("extraHeaders");
}

api.webRequest.onSendHeaders.addListener(
  (details) => {
    if (details.tabId < 0) return; // requêtes hors onglet
    if (!isVideoRequest(details.url)) return;

    const meta = tabMetadata.get(details.tabId);
    if (!meta) return; // pas encore de contexte d'épisode pour cet onglet

    const key = episodeKey(meta);
    if (tabLastSent.get(details.tabId) === key) return; // déjà envoyé
    tabLastSent.set(details.tabId, key);

    // Extraction des en-têtes pertinents.
    const headers = {};
    for (const h of details.requestHeaders || []) {
      if (RELEVANT_HEADERS.includes(h.name.toLowerCase())) {
        // Reconstitue une casse canonique (Referer, User-Agent...).
        const name = h.name
          .split("-")
          .map((p) => p.charAt(0).toUpperCase() + p.slice(1).toLowerCase())
          .join("-");
        headers[name] = h.value;
      }
    }
    if (!headers.Referer) headers.Referer = meta.page_url || details.url;

    sendToBackend(meta, details.url, headers);
  },
  { urls: ["<all_urls>"] },
  extraInfoSpec
);

// Nettoyage mémoire à la fermeture d'un onglet.
api.tabs?.onRemoved.addListener((tabId) => {
  tabMetadata.delete(tabId);
  tabLastSent.delete(tabId);
});
