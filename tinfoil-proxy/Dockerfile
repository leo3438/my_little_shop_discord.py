# ============================================================================
#  Image Docker du proxy/agrégateur Tinfoil — Node.js 22
#  Build multi-étapes : dépendances de prod uniquement, image finale légère.
# ============================================================================

# --- Étape 1 : installation des dépendances de production ---------------------
FROM node:22-alpine AS deps
WORKDIR /app
# On copie d'abord les manifestes pour profiter du cache de couches Docker :
# tant que package*.json ne change pas, npm ci n'est pas relancé.
COPY package.json package-lock.json ./
RUN npm ci --omit=dev

# --- Étape 2 : image finale --------------------------------------------------
FROM node:22-alpine
ENV NODE_ENV=production
WORKDIR /app

# node_modules déjà résolus + code source.
COPY --from=deps /app/node_modules ./node_modules
COPY package.json ./
COPY src ./src

# Exécution en utilisateur non-root (fourni par l'image node) pour la sécurité.
USER node

# Port applicatif (surchargé par la variable d'env PORT au besoin).
EXPOSE 3001

CMD ["node", "src/index.js"]
