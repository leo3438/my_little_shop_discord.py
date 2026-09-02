import { Router } from 'express';
import { logger } from '../logger.js';
import { getSource, resolveTarget } from '../services/sources.js';
import { streamRemote } from '../services/proxy.js';

export const downloadRouter = Router();

/**
 * GET /download/:source/:token
 * Route de téléchargement unifiée pour les deux sources.
 *
 * Le token encode l'URL amont réelle du fichier (masquée dans l'index). On la
 * reconstruit, on vérifie que l'hôte est autorisé, puis on stream/pipe le
 * fichier vers le client avec l'authentification propre à la source :
 *   - UltraNX     : identifiants dans le chemin (aucun en-tête).
 *   - MagicMonkei : en-tête Authorization: Basic.
 *
 * Le header `Range` est propagé (reprise de téléchargement) par streamRemote.
 */
downloadRouter.get('/:source/:token', async (req, res) => {
  const source = getSource(req.params.source);
  if (!source) {
    return res.status(404).json({ error: 'Source inconnue', source: req.params.source });
  }
  if (!source.configured()) {
    logger.error(`[${source.name}] ${source.missingMsg}`);
    return res.status(500).json({ error: source.missingMsg });
  }

  let url;
  try {
    url = resolveTarget(source, req.params.token);
  } catch (err) {
    logger.error(`[${source.name}] Token invalide : ${err.message}`);
    return res.status(err.statusCode || 400).json({ error: err.message });
  }

  await streamRemote({
    source: source.name,
    id: req.params.token.slice(0, 12) + '…',
    url,
    headers: source.upstreamHeaders(),
    req,
    res,
  });
});
