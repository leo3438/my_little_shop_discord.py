import 'dotenv/config';

/**
 * Centralise et valide la configuration issue des variables d'environnement.
 * Toute la configuration sensible (tokens, identifiants) transite par le .env
 * afin de ne jamais apparaître dans le code.
 */

const stripTrailingSlash = (value = '') => value.replace(/\/+$/, '');

export const config = {
  port: Number(process.env.PORT) || 3000,
  publicBaseUrl: stripTrailingSlash(
    process.env.PUBLIC_BASE_URL || `http://localhost:${Number(process.env.PORT) || 3000}`,
  ),

  ultranx: {
    apiBase: stripTrailingSlash(process.env.ULTRANX_API_BASE || 'https://api.ultranx.example.com'),
    authToken: process.env.ULTRANX_AUTH_TOKEN || '',
  },

  magicMonkei: {
    apiBase: stripTrailingSlash(
      process.env.MAGIC_MONKEI_API_BASE || 'https://cyberfoil.magicmonkei.com',
    ),
    user: process.env.MAGIC_MONKEI_USER || '',
    pass: process.env.MAGIC_MONKEI_PASS || '',
  },
};

/**
 * Avertit (sans bloquer) si des secrets ne sont pas renseignés : pratique pour
 * tester l'architecture de routage avec les jeux mockés avant d'avoir les
 * vrais identifiants.
 */
export function warnMissingSecrets(logger) {
  const missing = [];
  if (!config.ultranx.authToken) missing.push('ULTRANX_AUTH_TOKEN');
  if (!config.magicMonkei.user) missing.push('MAGIC_MONKEI_USER');
  if (!config.magicMonkei.pass) missing.push('MAGIC_MONKEI_PASS');

  if (missing.length > 0) {
    logger.warn(
      `Secrets manquants dans .env : ${missing.join(', ')}. ` +
        "L'agrégation d'index fonctionne, mais les téléchargements réels échoueront.",
    );
  }
}
