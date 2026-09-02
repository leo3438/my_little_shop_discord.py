import 'dotenv/config';

/**
 * Centralise et valide la configuration issue des variables d'environnement.
 * Tous les identifiants transitent par le .env afin de ne jamais apparaître
 * dans le code.
 *
 * Nouveau modèle d'authentification (connexion directe, sans cookie) :
 *   - UltraNX     : identifiants injectés dans le chemin de l'URL (façon DBI).
 *   - MagicMonkei : en-tête HTTP Basic (façon Tinfoil).
 */

const stripTrailingSlash = (value = '') => value.replace(/\/+$/, '');

/**
 * Parse un jeu d'en-têtes HTTP fourni via une variable d'environnement.
 * Accepte du JSON ({"Device-Id":"..."}) ou des lignes « Clé: Valeur »
 * séparées par des retours à la ligne ou des points-virgules.
 */
function parseHeadersEnv(raw) {
  if (!raw) return {};
  try {
    const obj = JSON.parse(raw);
    if (obj && typeof obj === 'object' && !Array.isArray(obj)) return obj;
  } catch { /* pas du JSON : on tente le format ligne */ }
  const out = {};
  for (const line of raw.split(/[\n;]+/)) {
    const i = line.indexOf(':');
    if (i > 0) out[line.slice(0, i).trim()] = line.slice(i + 1).trim();
  }
  return out;
}

export const config = {
  port: Number(process.env.PORT) || 3000,
  publicBaseUrl: stripTrailingSlash(
    process.env.PUBLIC_BASE_URL || `http://localhost:${Number(process.env.PORT) || 3000}`,
  ),

  // User-Agent Tinfoil, utilisé pour Magic Monkei (et par défaut). Le UA Node
  // par défaut est souvent rejeté (Cloudflare, filtrage) ; surchargeable par un
  // UA de navigateur si une source l'exige.
  userAgent: process.env.PROXY_USER_AGENT || 'Tinfoil/17.0',

  ultranx: {
    // Base DBI : les identifiants font partie du chemin.
    //   https://dbi.ultranx.ru/link/{LOGIN}/{PASSWORD}/
    baseUrl: stripTrailingSlash(process.env.ULTRANX_BASE_URL || 'https://dbi.ultranx.ru/link'),
    login: process.env.ULTRANX_LOGIN || '',
    password: process.env.ULTRANX_PASSWORD || '',
    // UltraNX vérifie que la requête provient de l'app DBI (« Failed to get
    // device info! » sinon). On simule DBI : User-Agent dédié + en-têtes
    // d'identification. Valeurs surchargeables/complétables via ULTRANX_HEADERS.
    userAgent: process.env.ULTRANX_USER_AGENT || 'DBI/755',
    extraHeaders: parseHeadersEnv(process.env.ULTRANX_HEADERS),
  },

  magicMonkei: {
    // Index Tinfoil protégé par authentification HTTP Basic. Le fichier JSON
    // s'appelle « shop.tfl » (le dossier /tinfoil/ renvoie un listing HTML).
    indexUrl: process.env.MAGIC_MONKEI_INDEX_URL || 'https://shop.magicmonkei.com/tinfoil/shop.tfl',
    user: process.env.MAGIC_MONKEI_USER || '',
    pass: process.env.MAGIC_MONKEI_PASS || '',
  },
};

/**
 * Avertit (sans bloquer) si des identifiants ne sont pas renseignés.
 */
export function warnMissingSecrets(logger) {
  const missing = [];
  if (!config.ultranx.login) missing.push('ULTRANX_LOGIN');
  if (!config.ultranx.password) missing.push('ULTRANX_PASSWORD');
  if (!config.magicMonkei.user) missing.push('MAGIC_MONKEI_USER');
  if (!config.magicMonkei.pass) missing.push('MAGIC_MONKEI_PASS');

  if (missing.length > 0) {
    logger.warn(
      `Identifiants manquants dans .env : ${missing.join(', ')}. ` +
        'Les sources concernées renverront une erreur tant qu\'ils ne sont pas fournis.',
    );
  }
}
