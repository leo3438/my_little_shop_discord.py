import { Router } from 'express';
import { logger } from '../logger.js';
import { getSource, resolveTarget } from '../services/sources.js';
import { buildMergedIndex, fetchSourceIndex, rewriteIndex } from '../services/catalog.js';

export const indexRouter = Router();

/**
 * GET /
 * Récupère les index des deux sources (UltraNX via DBI, Magic Monkei via
 * Tinfoil/Basic), fusionne leurs tableaux `files` et `directories`, et renvoie
 * un unique index Tinfoil dont tous les liens pointent vers ce proxy.
 */
indexRouter.get('/', async (req, res, next) => {
  try {
    logger.info('📄 Construction de l\'index Tinfoil unifié');
    const index = await buildMergedIndex();
    res.json(index);
  } catch (err) {
    next(err);
  }
});

/**
 * GET /index/:source/:token
 * Sous-index re-proxifié : permet la navigation dans les dossiers d'une source
 * de façon transparente. Le token encode l'URL amont du sous-index ; on la
 * récupère (avec auth), on réécrit ses liens, puis on renvoie le JSON.
 */
indexRouter.get('/index/:source/:token', async (req, res, next) => {
  const source = getSource(req.params.source);
  if (!source) return res.status(404).json({ error: 'Source inconnue', source: req.params.source });
  if (!source.configured()) return res.status(500).json({ error: source.missingMsg });

  try {
    const targetUrl = resolveTarget(source, req.params.token);
    const json = await fetchSourceIndex(source, targetUrl);
    res.json(rewriteIndex(source, json, targetUrl));
  } catch (err) {
    if (err.statusCode === 400) return res.status(400).json({ error: err.message });
    next(err);
  }
});
