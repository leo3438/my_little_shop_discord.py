import { Router } from 'express';
import { config } from '../config.js';
import { logger } from '../logger.js';

export const indexRouter = Router();

/**
 * GET /
 * Génère l'index Tinfoil combiné (format JSON : { files, directories }).
 *
 * Chaque entrée « files » pointe vers une route LOCALE de ce proxy ; c'est
 * ce proxy qui, à la demande, ira chercher le fichier sur la bonne source
 * privée en injectant les identifiants requis. Tinfoil ne voit qu'un seul
 * dépôt transparent.
 *
 * Les deux jeux ci-dessous sont MOCKÉS pour valider l'architecture de routage.
 */
indexRouter.get('/', (req, res) => {
  logger.info('📄 Génération de l\'index Tinfoil combiné');

  const base = config.publicBaseUrl;

  const index = {
    // Message affiché par Tinfoil à la connexion au dépôt.
    success: 'Bienvenue sur le dépôt unifié (UltraNX + Magic Monkei) 🎮',
    files: [
      {
        // Jeu mocké n°1 — routé vers UltraNX.
        url: `${base}/download/ultranx/12345`,
        size: 0, // 0 = taille inconnue tant que la source n'a pas répondu.
      },
      {
        // Jeu mocké n°2 — routé vers Magic Monkei.
        url: `${base}/download/magicmonkei/67890`,
        size: 0,
      },
    ],
    directories: [],
  };

  res.json(index);
});
