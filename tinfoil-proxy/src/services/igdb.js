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
    config.igdb.tokenUrl +
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
// https + une taille donnée (cover_big par défaut).
const toCoverUrl = (u, size = 't_cover_big') => (u ? 'https:' + u.replace('/t_thumb/', `/${size}/`) : null);

/** Exécute une recherche IGDB et renvoie le premier jeu (ou null). */
async function queryFirstGame(name, fields) {
  const token = await getToken();
  await throttle();
  const res = await fetch(config.igdb.apiUrl, {
    method: 'POST',
    headers: {
      'Client-ID': config.igdb.clientId,
      Authorization: `Bearer ${token}`,
      Accept: 'application/json',
    },
    body: `search "${name.replace(/"/g, '')}"; fields ${fields}; limit 1;`,
  });
  if (!res.ok) throw new Error(`IGDB games HTTP ${res.status}`);
  const arr = await res.json();
  return Array.isArray(arr) && arr[0] ? arr[0] : null;
}

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
      const game = await queryFirstGame(name, 'name,cover.url');
      const url = toCoverUrl(game?.cover?.url) || null;
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

const detailCache = new Map();
const detailInflight = new Map();

/**
 * Renvoie les détails d'un jeu (synopsis, date de sortie, note, genres, jaquette)
 * pour la modale, ou null. Résultat mis en cache par nom nettoyé.
 */
export async function getGameDetails(rawName) {
  if (!igdbConfigured()) return null;
  const name = cleanGameName(rawName);
  if (!name) return null;
  if (detailCache.has(name)) return detailCache.get(name);
  if (detailInflight.has(name)) return detailInflight.get(name);

  const promise = (async () => {
    try {
      const g = await queryFirstGame(
        name,
        'name,summary,storyline,first_release_date,total_rating,rating,genres.name,cover.url',
      );
      const details = g && {
        name: g.name || name,
        cover: toCoverUrl(g.cover?.url),
        summary: g.summary || g.storyline || '',
        // first_release_date : timestamp Unix en secondes -> ISO (YYYY-MM-DD).
        released: g.first_release_date
          ? new Date(g.first_release_date * 1000).toISOString().slice(0, 10)
          : null,
        rating: Number.isFinite(g.total_rating) ? Math.round(g.total_rating)
          : Number.isFinite(g.rating) ? Math.round(g.rating) : null,
        genres: Array.isArray(g.genres) ? g.genres.map((x) => x.name).filter(Boolean) : [],
      };
      detailCache.set(name, details || null);
      return details || null;
    } catch (err) {
      logger.warn(`IGDB : détails introuvables pour « ${name} » (${err.message})`);
      detailCache.set(name, null);
      return null;
    } finally {
      detailInflight.delete(name);
    }
  })();

  detailInflight.set(name, promise);
  return promise;
}
