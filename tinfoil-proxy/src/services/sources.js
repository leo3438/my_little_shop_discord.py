import { config } from '../config.js';

/**
 * Définition des deux sources et de leur logique d'authentification directe.
 *
 * Chaque source expose :
 *   - key        : identifiant utilisé dans les routes locales (/download/<key>/...).
 *   - name       : nom lisible (logs).
 *   - indexUrl() : URL amont de l'index à récupérer.
 *   - authHeaders() : en-têtes HTTP à injecter (Basic pour Magic Monkei, aucun
 *                     pour UltraNX dont l'auth est dans le chemin).
 *   - secretPrefix() : préfixe d'URL contenant des identifiants à masquer dans
 *                      les liens réécrits (UltraNX uniquement).
 *   - allowedHosts() : hôtes autorisés (dérivés de l'index amont). Empêche
 *                      qu'un lien forgé ne fasse fuiter les identifiants vers un
 *                      hôte tiers (SSRF). Une entrée « .exemple.com » autorise
 *                      tout sous-domaine (CDN) ; sinon correspondance exacte.
 */

const basicAuth = (user, pass) => `Basic ${Buffer.from(`${user}:${pass}`).toString('base64')}`;

const ULTRANX_INDEX_URL = () =>
  `${config.ultranx.baseUrl}/${encodeURIComponent(config.ultranx.login)}/` +
  `${encodeURIComponent(config.ultranx.password)}/`;

/**
 * Dérive la liste d'hôtes autorisés à partir de l'hôte de l'index amont :
 * l'hôte exact, et (s'il ne s'agit pas d'une IP ni d'un nom simple) son domaine
 * parent en wildcard pour couvrir un éventuel CDN (cdn.exemple.com).
 * `extra` : hôtes supplémentaires (variable d'env, séparés par des virgules).
 */
function deriveAllowedHosts(indexUrl, extra = '') {
  const host = new URL(indexUrl).hostname.toLowerCase();
  const allowed = new Set([host]);
  const isIp = /^[\d.]+$/.test(host) || host.includes(':');
  const labels = host.split('.');
  if (!isIp && labels.length >= 2) {
    const parent = labels.slice(-2).join('.'); // ex. ultranx.ru
    allowed.add(parent);
    allowed.add(`.${parent}`);
  }
  for (const h of extra.split(',').map((s) => s.trim().toLowerCase()).filter(Boolean)) {
    allowed.add(h);
  }
  return [...allowed];
}

export const SOURCES = {
  ultranx: {
    key: 'ultranx',
    name: 'UltraNX',
    indexUrl: ULTRANX_INDEX_URL,
    // Auth portée par le chemin de l'URL : aucun en-tête supplémentaire.
    authHeaders: () => ({}),
    // Préfixe (login/password inclus) retiré des liens réécrits pour ne pas
    // exposer les identifiants au client. Reconstruit côté serveur au download.
    secretPrefix: ULTRANX_INDEX_URL,
    allowedHosts: () => deriveAllowedHosts(ULTRANX_INDEX_URL(), process.env.ULTRANX_ALLOWED_HOSTS),
    configured: () => Boolean(config.ultranx.login && config.ultranx.password),
    missingMsg: 'ULTRANX_LOGIN / ULTRANX_PASSWORD non configurés',
  },

  magicmonkei: {
    key: 'magicmonkei',
    name: 'MagicMonkei',
    indexUrl: () => config.magicMonkei.indexUrl,
    // Auth par en-tête HTTP Basic.
    authHeaders: () => ({ Authorization: basicAuth(config.magicMonkei.user, config.magicMonkei.pass) }),
    // Rien à masquer : les identifiants ne sont pas dans l'URL.
    secretPrefix: () => null,
    allowedHosts: () =>
      deriveAllowedHosts(config.magicMonkei.indexUrl, process.env.MAGIC_MONKEI_ALLOWED_HOSTS),
    configured: () => Boolean(config.magicMonkei.user && config.magicMonkei.pass),
    missingMsg: 'MAGIC_MONKEI_USER / MAGIC_MONKEI_PASS non configurés',
  },
};

export const getSource = (key) => SOURCES[key];

// --- Encodage des références vers des tokens URL-safe -----------------------

/** Encode une référence (chemin relatif ou URL absolue) en token base64url. */
export const encodeRef = (ref) => Buffer.from(ref, 'utf8').toString('base64url');

/** Décode un token base64url en sa référence d'origine. */
export const decodeRef = (token) => Buffer.from(token, 'base64url').toString('utf8');

/**
 * Réécrit une URL amont (résolue en absolu) en référence stockable dans le
 * token local. Pour UltraNX on retire le préfixe secret afin de ne pas
 * divulguer les identifiants ; le reste reste absolu.
 */
export function toRef(source, absoluteUrl) {
  const prefix = source.secretPrefix();
  if (prefix && absoluteUrl.startsWith(prefix)) {
    // Chemin relatif (sans slash initial) reconstructible via indexUrl().
    return absoluteUrl.slice(prefix.length);
  }
  return absoluteUrl;
}

/**
 * Reconstruit l'URL amont réelle à partir d'un token local, puis vérifie que
 * l'hôte cible est bien dans la liste autorisée de la source.
 * @returns {string} URL amont absolue et validée.
 * @throws {Error} avec .statusCode=400 si l'hôte n'est pas autorisé.
 */
export function resolveTarget(source, token) {
  const ref = decodeRef(token);
  // `new URL(ref, base)` : si ref est absolu, base est ignorée ; si ref est
  // relatif, il est résolu contre l'index amont (qui porte l'auth UltraNX).
  const url = new URL(ref, source.indexUrl());

  const host = url.hostname.toLowerCase();
  const ok = source.allowedHosts().some(
    (h) => (h.startsWith('.') ? host.endsWith(h) : host === h),
  );
  if (!ok) {
    const err = new Error(`Hôte non autorisé pour ${source.name} : ${host}`);
    err.statusCode = 400;
    throw err;
  }
  return url.href;
}
