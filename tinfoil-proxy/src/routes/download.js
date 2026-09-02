import { Router } from 'express';
import { config } from '../config.js';
import { logger } from '../logger.js';
import { streamRemote } from '../services/proxy.js';

export const downloadRouter = Router();

/**
 * GET /download/ultranx/:id
 * Proxy vers UltraNX. Injecte OBLIGATOIREMENT le cookie réseau :
 *     Cookie: auth_token=VALEUR_DU_TOKEN
 */
downloadRouter.get('/ultranx/:id', async (req, res) => {
  const { id } = req.params;

  if (!config.ultranx.authToken) {
    logger.error('[UltraNX] ULTRANX_AUTH_TOKEN absent : téléchargement impossible');
    return res.status(500).json({ error: 'ULTRANX_AUTH_TOKEN non configuré' });
  }

  const url = `${config.ultranx.apiBase}/download/${encodeURIComponent(id)}`;

  await streamRemote({
    source: 'UltraNX',
    id,
    url,
    headers: {
      Cookie: `auth_token=${config.ultranx.authToken}`,
    },
    req,
    res,
  });
});

/**
 * GET /download/magicmonkei/:id
 * Proxy vers Magic Monkei (cyberfoil.magicmonkei.com). Injecte OBLIGATOIREMENT
 * l'authentification HTTP Basic construite à partir de MAGIC_MONKEI_USER/PASS.
 */
downloadRouter.get('/magicmonkei/:id', async (req, res) => {
  const { id } = req.params;
  const { user, pass } = config.magicMonkei;

  if (!user || !pass) {
    logger.error('[MagicMonkei] Identifiants absents : téléchargement impossible');
    return res.status(500).json({ error: 'MAGIC_MONKEI_USER / MAGIC_MONKEI_PASS non configurés' });
  }

  const url = `${config.magicMonkei.apiBase}/download/${encodeURIComponent(id)}`;
  const basic = Buffer.from(`${user}:${pass}`).toString('base64');

  await streamRemote({
    source: 'MagicMonkei',
    id,
    url,
    headers: {
      Authorization: `Basic ${basic}`,
    },
    req,
    res,
  });
});
