import { Readable } from 'node:stream';
import { pipeline } from 'node:stream/promises';
import { config } from '../config.js';
import { logger } from '../logger.js';

/**
 * En-têtes de la réponse distante que l'on recopie vers le client Tinfoil.
 * On conserve notamment Content-Length et Content-Disposition pour que la
 * Switch connaisse la taille exacte et le nom du fichier.
 */
const FORWARDED_HEADERS = [
  'content-length',
  'content-disposition',
  'content-type',
  'accept-ranges',
  'last-modified',
  'etag',
];

/**
 * Effectue une requête GET vers une source distante et fait un pipe direct,
 * octet par octet, de la réponse vers le client Tinfoil.
 *
 * @param {object}   params
 * @param {string}   params.source   Nom de la source (pour les logs).
 * @param {string}   params.id       Identifiant du jeu demandé.
 * @param {string}   params.url      URL distante complète à interroger.
 * @param {object}   params.headers  En-têtes à injecter (Cookie ou Authorization).
 * @param {import('express').Request}  params.req
 * @param {import('express').Response} params.res
 */
export async function streamRemote({ source, id, url, headers, req, res }) {
  const label = `[${source}] id=${id}`;
  logger.info(`▶️  Début du stream ${label} → ${url}`);

  const controller = new AbortController();
  // Si le client (Tinfoil) coupe la connexion, on annule la requête distante.
  req.on('close', () => controller.abort());

  let upstream;
  try {
    upstream = await fetch(url, {
      method: 'GET',
      headers: {
        // User-Agent fixe (et non celui du client) : les sources filtrent selon
        // le User-Agent, et un téléchargement lancé depuis /web enverrait sinon
        // un UA de navigateur potentiellement rejeté en amont.
        'User-Agent': config.userAgent,
        // On propage l'en-tête Range pour permettre la reprise de téléchargement.
        ...(req.get('range') ? { Range: req.get('range') } : {}),
        ...headers,
      },
      signal: controller.signal,
    });
  } catch (err) {
    if (controller.signal.aborted) {
      logger.warn(`⏹️  Client déconnecté avant la réponse ${label}`);
      return;
    }
    logger.error(`Échec de la requête distante ${label} : ${err.message}`);
    if (!res.headersSent) res.status(502).json({ error: 'Bad Gateway', source });
    return;
  }

  if (!upstream.ok) {
    logger.error(`Réponse distante ${label} : HTTP ${upstream.status}`);
    if (!res.headersSent) {
      res.status(upstream.status).json({ error: 'Upstream error', status: upstream.status, source });
    }
    return;
  }

  // Recopie des en-têtes d'origine importants.
  for (const name of FORWARDED_HEADERS) {
    const value = upstream.headers.get(name);
    if (value) res.setHeader(name, value);
  }
  // 206 Partial Content si la source a honoré un Range, sinon 200.
  res.status(upstream.status === 206 ? 206 : 200);

  if (!upstream.body) {
    logger.warn(`Aucun corps de réponse ${label}`);
    return res.end();
  }

  // Conversion du ReadableStream web (fetch) en flux Node, puis pipe vers le client.
  const nodeStream = Readable.fromWeb(upstream.body);
  try {
    await pipeline(nodeStream, res);
    logger.info(`✅ Fin du stream ${label}`);
  } catch (err) {
    if (controller.signal.aborted || err?.code === 'ERR_STREAM_PREMATURE_CLOSE') {
      logger.warn(`⏹️  Stream interrompu (client déconnecté) ${label}`);
    } else {
      logger.error(`Erreur pendant le stream ${label} : ${err.message}`);
      if (!res.headersSent) res.status(502).end();
    }
  }
}
