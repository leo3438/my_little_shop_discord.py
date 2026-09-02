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

export const config = {
  port: Number(process.env.PORT) || 3000,
  publicBaseUrl: stripTrailingSlash(
    process.env.PUBLIC_BASE_URL || `http://localhost:${Number(process.env.PORT) || 3000}`,
  ),

  // User-Agent injecté sur TOUS les appels amont. Le User-Agent Node par défaut
  // est souvent rejeté (Cloudflare, filtrage). On se fait passer pour Tinfoil ;
  // surchargeable par un UA de navigateur si une source l'exige.
  userAgent: process.env.PROXY_USER_AGENT || 'Tinfoil/17.0',

  ultranx: {
    // Base DBI : les identifiants font partie du chemin.
    //   https://dbi.ultranx.ru/link/{LOGIN}/{PASSWORD}/
    baseUrl: stripTrailingSlash(process.env.ULTRANX_BASE_URL || 'https://dbi.ultranx.ru/link'),
    login: process.env.ULTRANX_LOGIN || '',
    password: process.env.ULTRANX_PASSWORD || '',
  },

  magicMonkei: {
    // Index Tinfoil protégé par authentification HTTP Basic.
    indexUrl: process.env.MAGIC_MONKEI_INDEX_URL || 'https://shop.magicmonkei.com/tinfoil',
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
