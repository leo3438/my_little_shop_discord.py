/**
 * Petit logger console horodaté, sans dépendance externe.
 * Fournit des messages clairs pour suivre le cycle de vie des streams.
 */

const ts = () => new Date().toISOString();

export const logger = {
  info: (msg) => console.log(`[${ts()}] [INFO]  ${msg}`),
  warn: (msg) => console.warn(`[${ts()}] [WARN]  ${msg}`),
  error: (msg) => console.error(`[${ts()}] [ERROR] ${msg}`),
};
