import { config } from '../config.js';
import { logger } from '../logger.js';
import { SOURCES, toRef, encodeRef } from './sources.js';

/**
 * Récupère et fusionne les index Tinfoil des deux sources.
 *
 * Pour chaque source :
 *   1. On télécharge l'index amont (JSON) avec la bonne authentification.
 *   2. On réécrit les liens `files`/`directories` pour qu'ils pointent vers les
 *      routes locales du proxy (/download/<src>/... et /index/<src>/...).
 *   3. On fusionne les tableaux des deux sources en un seul index unifié.
 */

/** Télécharge et parse l'index JSON amont d'une source. */
export async function fetchSourceIndex(source, indexUrl = source.indexUrl()) {
  logger.info(`⬇️  Récupération de l'index [${source.name}] → ${indexUrl}`);
  const res = await fetch(indexUrl, {
    headers: { 'User-Agent': 'Tinfoil', Accept: 'application/json', ...source.authHeaders() },
  });
  if (!res.ok) {
    throw Object.assign(new Error(`Index ${source.name} : HTTP ${res.status}`), {
      statusCode: res.status,
    });
  }
  return res.json();
}

/**
 * Construit une URL locale de route pour une URL amont absolue.
 * `toRef` masque au passage les identifiants UltraNX présents dans le chemin.
 */
const localUrl = (route, source, absoluteUrl) =>
  `${config.publicBaseUrl}/${route}/${source.key}/${encodeRef(toRef(source, absoluteUrl))}`;

/**
 * Réécrit un index Tinfoil : les `files` pointent vers /download, les
 * `directories` vers /index (sous-index re-proxifiés récursivement).
 *
 * @param {object} source  La source concernée.
 * @param {object} json    L'index amont brut.
 * @param {string} baseUrl L'URL depuis laquelle l'index a été récupéré
 *                         (sert de base de résolution des liens relatifs).
 */
export function rewriteIndex(source, json, baseUrl) {
  const files = Array.isArray(json.files) ? json.files : [];
  const directories = Array.isArray(json.directories) ? json.directories : [];

  const rewrittenFiles = files
    .map((entry) => {
      const rawUrl = typeof entry === 'string' ? entry : entry?.url;
      if (!rawUrl) return null;
      const absolute = new URL(rawUrl, baseUrl).href;
      const url = localUrl('download', source, absolute);
      // On conserve les autres champs (size, etc.) tels quels.
      return typeof entry === 'string' ? { url } : { ...entry, url };
    })
    .filter(Boolean);

  const rewrittenDirectories = directories
    .map((entry) => {
      const rawUrl = typeof entry === 'string' ? entry : entry?.url;
      if (!rawUrl) return null;
      const absolute = new URL(rawUrl, baseUrl).href;
      const url = localUrl('index', source, absolute);
      return typeof entry === 'string' ? url : { ...entry, url };
    })
    .filter(Boolean);

  return { files: rewrittenFiles, directories: rewrittenDirectories };
}

/**
 * Récupère + réécrit l'index d'une source. Ne jette pas : en cas d'échec,
 * renvoie un index vide et loggue l'erreur (l'autre source reste servie).
 */
export async function getRewrittenIndex(source, indexUrl = source.indexUrl()) {
  if (!source.configured()) {
    logger.warn(`⚠️  [${source.name}] ${source.missingMsg} : source ignorée`);
    return { files: [], directories: [] };
  }
  try {
    const json = await fetchSourceIndex(source, indexUrl);
    const rewritten = rewriteIndex(source, json, indexUrl);
    logger.info(
      `✅ Index [${source.name}] : ${rewritten.files.length} fichier(s), ` +
        `${rewritten.directories.length} dossier(s)`,
    );
    return rewritten;
  } catch (err) {
    logger.error(`Échec de l'index [${source.name}] : ${err.message}`);
    return { files: [], directories: [] };
  }
}

/**
 * Construit l'index Tinfoil unifié en fusionnant les deux sources.
 * Les deux récupérations sont menées en parallèle.
 */
export async function buildMergedIndex() {
  const [ultra, magic] = await Promise.all([
    getRewrittenIndex(SOURCES.ultranx),
    getRewrittenIndex(SOURCES.magicmonkei),
  ]);

  return {
    success: 'Dépôt unifié UltraNX + Magic Monkei 🎮',
    files: [...ultra.files, ...magic.files],
    directories: [...ultra.directories, ...magic.directories],
  };
}
