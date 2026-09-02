import { config } from '../config.js';
import { logger } from '../logger.js';

/**
 * Récupération des jaquettes via l'API IGDB.
 *
 * IGDB s'authentifie via Twitch (OAuth client_credentials). On met en cache :
 *   - le token d'accès (valable ~60 jours) ;
 *   - le résultat par nom de jeu nettoyé (évite de re-solliciter l'API) ;
 *   - les requêtes en vol (déduplication).
 * Un limiteur de débit espace les appels pour rester sous la limite IGDB (~4/s).
 */

export const igdbConfigured = () =>
  Boolean(config.igdb.clientId && config.igdb.clientSecret);

let tokenCache = { token: null, expiresAt: 0 };
const coverCache = new Map(); // nomNettoyé -> url|null
const inflight = new Map(); // nomNettoyé -> Promise<url|null>

// Limiteur simple : au moins MIN_INTERVAL ms entre deux appels IGDB.
const MIN_INTERVAL = 260;
let nextSlot = 0;
async function throttle() {
  const now = Date.now();
  const wait = Math.max(0, nextSlot - now);
  nextSlot = Math.max(now, nextSlot) + MIN_INTERVAL;
  if (wait) await new Promise((r) => setTimeout(r, wait));
}

async function getToken() {
  if (tokenCache.token && Date.now() < tokenCache.expiresAt) return tokenCache.token;
  const url =
    'https://id.twitch.tv/oauth2/token' +
    `?client_id=${encodeURIComponent(config.igdb.clientId)}` +
    `&client_secret=${encodeURIComponent(config.igdb.clientSecret)}` +
    '&grant_type=client_credentials';
  const res = await fetch(url, { method: 'POST' });
  if (!res.ok) throw new Error(`Twitch token HTTP ${res.status}`);
  const data = await res.json();
  tokenCache = {
    token: data.access_token,
    expiresAt: Date.now() + Math.max(0, (data.expires_in || 3600) - 60) * 1000,
  };
  return tokenCache.token;
}

/**
 * Nettoie un nom de fichier de jeu pour la recherche IGDB :
 * retire l'extension, les [titleid]/[v0], les (Region), les séparateurs.
 */
export function cleanGameName(raw) {
  return String(raw || '')
    .replace(/\.[a-z0-9]{2,4}$/i, '')  // extension (.nsp, .zip, .sfc, .z64…)
    .replace(/\[[^\]]*\]/g, ' ')       // [titleid], [v0]…
    .replace(/\([^)]*\)/g, ' ')        // (USA), (Region)…
    .replace(/[._]+/g, ' ')            // séparateurs
    .replace(/\s+/g, ' ')
    .trim();
}

// IGDB renvoie une URL en //images.igdb.com/.../t_thumb/xxx.jpg ; on passe en
// https + taille « cover_big ».
const toCoverUrl = (u) => (u ? 'https:' + u.replace('/t_thumb/', '/t_cover_big/') : null);

/**
 * Renvoie l'URL de la jaquette d'un jeu (ou null si introuvable / non configuré).
 */
export async function getCover(rawName) {
  if (!igdbConfigured()) return null;
  const name = cleanGameName(rawName);
  if (!name) return null;
  if (coverCache.has(name)) return coverCache.get(name);
  if (inflight.has(name)) return inflight.get(name);

  const promise = (async () => {
    try {
      const token = await getToken();
      await throttle();
      const res = await fetch('https://api.igdb.com/v4/games', {
        method: 'POST',
        headers: {
          'Client-ID': config.igdb.clientId,
          Authorization: `Bearer ${token}`,
          Accept: 'application/json',
        },
        body: `search "${name.replace(/"/g, '')}"; fields name,cover.url; limit 1;`,
      });
      if (!res.ok) throw new Error(`IGDB games HTTP ${res.status}`);
      const arr = await res.json();
      const url = toCoverUrl(arr?.[0]?.cover?.url) || null;
      coverCache.set(name, url);
      return url;
    } catch (err) {
      logger.warn(`IGDB : jaquette introuvable pour « ${name} » (${err.message})`);
      coverCache.set(name, null);
      return null;
    } finally {
      inflight.delete(name);
    }
  })();

  inflight.set(name, promise);
  return promise;
}
