import express from 'express';
import { config, warnMissingSecrets } from './config.js';
import { logger } from './logger.js';
import { indexRouter } from './routes/index.js';
import { downloadRouter } from './routes/download.js';

const app = express();

// Log minimal de chaque requête entrante.
app.use((req, res, next) => {
  logger.info(`➡️  ${req.method} ${req.originalUrl} (${req.ip})`);
  next();
});

// Route d'agrégation de l'index Tinfoil.
app.use('/', indexRouter);

// Routes de proxy/streaming des téléchargements.
app.use('/download', downloadRouter);

// Sonde de santé pratique pour vérifier que le serveur tourne.
app.get('/health', (req, res) => res.json({ status: 'ok' }));

// Gestion des routes inconnues.
app.use((req, res) => {
  res.status(404).json({ error: 'Not Found', path: req.originalUrl });
});

// Filet de sécurité pour les erreurs non interceptées.
app.use((err, req, res, next) => {
  logger.error(`Erreur non gérée : ${err.message}`);
  if (!res.headersSent) res.status(500).json({ error: 'Internal Server Error' });
});

app.listen(config.port, () => {
  logger.info('==================================================================');
  logger.info('  Proxy/agrégateur Tinfoil démarré');
  logger.info(`  Index Tinfoil   : ${config.publicBaseUrl}/`);
  logger.info(`  UltraNX          : ${config.publicBaseUrl}/download/ultranx/:id`);
  logger.info(`  Magic Monkei     : ${config.publicBaseUrl}/download/magicmonkei/:id`);
  logger.info(`  Écoute locale    : port ${config.port}`);
  logger.info('==================================================================');
  warnMissingSecrets(logger);
});
