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
    headers: { Accept: 'application/json', ...source.upstreamHeaders() },
  });
  if (!res.ok) {
    throw Object.assign(new Error(`Index ${source.name} : HTTP ${res.status}`), {
      statusCode: res.status,
    });
  }
  return res.json();
}

/**
 * Construit un chemin local STRICTEMENT RELATIF (ex. /download/magicmonkei/<token>)
 * pour une URL amont absolue. On n'injecte volontairement aucun hôte : Tinfoil
 * comme le navigateur résolvent le chemin contre l'origine réellement utilisée,
 * ce qui évite les liens cassés du type « .../0.0.0.0/download/... » quand
 * PUBLIC_BASE_URL est mal renseigné.
 * `toRef` masque au passage les identifiants UltraNX présents dans le chemin.
 */
const localUrl = (route, source, absoluteUrl) =>
  `/${route}/${source.key}/${encodeRef(toRef(source, absoluteUrl))}`;

/**
 * Déduit un nom de fichier lisible à partir de l'URL amont. Les boutiques
 * Tinfoil placent souvent le nom lisible après un « # » (fragment) ; sinon on
 * prend le dernier segment du chemin. On décode les %xx au passage.
 */
function deriveName(rawUrl) {
  try {
    const hash = rawUrl.indexOf('#');
    let candidate = hash >= 0 ? rawUrl.slice(hash + 1) : rawUrl.split('?')[0];
    candidate = candidate.split('/').filter(Boolean).pop() || '';
    try { candidate = decodeURIComponent(candidate); } catch { /* garde brut */ }
    return candidate.trim();
  } catch {
    return '';
  }
}

/**
 * Déduit le nom de la console à partir du dossier PARENT du fichier dans l'URL
 * amont (ex. « Atari 2600 » dans /.../Atari%202600/jeu.zip). Utilisé pour Magic
 * Monkei, dont le catalogue est organisé par console.
 */
function deriveConsole(absoluteUrl) {
  try {
    const segs = new URL(absoluteUrl).pathname.split('/').filter(Boolean);
    if (segs.length < 2) return '';
    const parent = segs[segs.length - 2];
    try { return decodeURIComponent(parent).trim(); } catch { return parent; }
  } catch {
    return '';
  }
}

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
      const isString = typeof entry === 'string';
      const rawUrl = isString ? entry : entry?.url;
      if (!rawUrl) return null;
      const absolute = new URL(rawUrl, baseUrl).href;
      const url = localUrl('download', source, absolute);
      // Nom : on garde name/title s'ils existent, sinon on le déduit de l'URL
      // amont. Le lien local étant un token opaque, le nom DOIT être fourni ici
      // (le client ne peut pas le retrouver depuis /download/<src>/<token>).
      const name = (!isString && (entry.name || entry.title)) || deriveName(absolute);
      // Console : dossier parent pour Magic Monkei (catalogue multi-consoles),
      // « Nintendo Switch » pour UltraNX.
      const consoleName =
        source.key === 'magicmonkei' ? deriveConsole(absolute) : 'Nintendo Switch';
      // On conserve les autres champs (size, etc.) et on ajoute name/console.
      return isString
        ? { url, name, console: consoleName }
        : { ...entry, url, name, console: consoleName };
    })
    .filter(Boolean);

  // `directories` reste au format Tinfoil : un simple tableau de chaînes (URLs).
  const rewrittenDirectories = directories
    .map((entry) => {
      const rawUrl = typeof entry === 'string' ? entry : entry?.url;
      if (!rawUrl) return null;
      return localUrl('index', source, new URL(rawUrl, baseUrl).href);
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
