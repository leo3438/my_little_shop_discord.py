import { Router } from 'express';
import { getCover, getGameDetails, igdbConfigured } from '../services/igdb.js';

export const apiRouter = Router();

/**
 * GET /api/cover?name=<nom du jeu>
 * Renvoie l'URL de la jaquette IGDB : { cover: "https://…"|null }.
 * Appelée à la demande par le frontend (lazy-loading) pour éviter le
 * rate-limiting sur des dizaines de milliers de jeux.
 */
apiRouter.get('/cover', async (req, res) => {
  const name = (req.query.name || '').toString();
  if (!name) return res.status(400).json({ error: 'paramètre name requis' });
  if (!igdbConfigured()) return res.json({ cover: null, reason: 'igdb-not-configured' });

  try {
    const cover = await getCover(name);
    // Cache navigateur : une jaquette ne change pas ; on évite de re-solliciter.
    res.set('Cache-Control', 'public, max-age=604800'); // 7 jours
    res.json({ cover });
  } catch {
    res.json({ cover: null });
  }
});

/**
 * GET /api/game?name=<nom du jeu>
 * Détails IGDB pour la modale : { name, cover, summary, released, rating, genres }.
 * Renvoie { found: false } si non trouvé ou IGDB non configuré.
 */
apiRouter.get('/game', async (req, res) => {
  const name = (req.query.name || '').toString();
  if (!name) return res.status(400).json({ error: 'paramètre name requis' });
  if (!igdbConfigured()) return res.json({ found: false, reason: 'igdb-not-configured' });

  try {
    const details = await getGameDetails(name);
    res.set('Cache-Control', 'public, max-age=604800');
    res.json(details ? { found: true, ...details } : { found: false });
  } catch {
    res.json({ found: false });
  }
});
