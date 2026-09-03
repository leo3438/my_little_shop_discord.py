# Proxy / Agrégateur Tinfoil — UltraNX + Magic Monkei

Serveur **Node.js / Express** qui unifie deux sources privées
(**UltraNX** et **Magic Monkei**) en un **seul dépôt Tinfoil transparent**
pour Nintendo Switch.

Le proxy se connecte aux sources **exactement comme les applications
officielles** (DBI pour UltraNX, Tinfoil pour Magic Monkei) :

- Récupère l'index JSON réel de **chaque** source, **réécrit** tous les liens
  pour qu'ils pointent vers ce proxy, puis **fusionne** les tableaux `files` et
  `directories` en un seul index Tinfoil.
- Fait un **stream/pipe direct, octet par octet**, des téléchargements, en
  conservant les en-têtes d'origine (`Content-Length`, `Content-Disposition`…)
  et en supportant le header `Range` (reprise de téléchargement).
- Authentification directe injectée automatiquement, propre à chaque source :
  - **UltraNX (façon DBI)** → identifiants dans le chemin de l'URL amont :
    `https://dbi.ultranx.ru/link/{LOGIN}/{PASSWORD}/`
  - **Magic Monkei (façon Tinfoil)** → en-tête `Authorization: Basic …`

La Switch ne voit qu'un seul dépôt ; les identifiants restent sur le serveur et
ne sont **jamais** exposés dans les liens réécrits.

## Architecture

```
tinfoil-proxy/
├── package.json
├── .env.example            # modèle de configuration
└── src/
    ├── index.js            # point d'entrée Express
    ├── config.js           # chargement + validation du .env
    ├── logger.js           # logs console horodatés
    ├── routes/
    │   ├── index.js        # GET /  (index unifié) + GET /index/:src/:token
    │   └── download.js     # GET /download/:src/:token  (streaming)
    └── services/
        ├── sources.js      # définition des 2 sources, auth, tokens, SSRF guard
        ├── catalog.js      # récupération + réécriture + fusion des index
        └── proxy.js        # streaming générique (fetch → pipe vers le client)
```

### Comment les liens sont réécrits

L'URL amont réelle de chaque fichier/dossier est encodée dans un **token
base64url** placé dans le lien local (`/download/<src>/<token>`). Au
téléchargement, le proxy décode ce token, reconstruit l'URL amont, **vérifie que
l'hôte est bien celui de la source** (protection anti-SSRF/fuite d'identifiants)
puis stream le fichier avec l'authentification adéquate. Pour UltraNX, le
segment `login/password` est retiré du token afin de ne pas divulguer les
identifiants côté client.

## Prérequis

- **Node.js ≥ 18** (testé sous Node 22). Utilise le `fetch` natif, aucune
  dépendance HTTP tierce n'est nécessaire.

## Installation

```bash
cd tinfoil-proxy
npm install
cp .env.example .env
# éditez .env pour renseigner vos tokens / identifiants
```

## Configuration (`.env`)

| Variable                  | Rôle                                                        |
| ------------------------- | ----------------------------------------------------------- |
| `PORT`                    | Port d'écoute local (par défaut `3000`).                   |
| `PUBLIC_BASE_URL`         | URL joignable depuis la Switch (ex. `http://192.168.1.50:3001`). |
| `ULTRANX_LOGIN`           | Login UltraNX (injecté dans le chemin façon DBI).          |
| `ULTRANX_PASSWORD`        | Mot de passe UltraNX.                                       |
| `MAGIC_MONKEI_USER`       | Identifiant Basic Auth Magic Monkei.                       |
| `MAGIC_MONKEI_PASS`       | Mot de passe Basic Auth Magic Monkei.                      |
| `ULTRANX_BASE_URL`        | *(optionnel)* base amont, défaut `https://dbi.ultranx.ru/link`. |
| `MAGIC_MONKEI_INDEX_URL`  | *(optionnel)* défaut `…/tinfoil/shop.tfl`.                 |
| `IGDB_CLIENT_ID`          | *(optionnel)* app Twitch pour les jaquettes IGDB.          |
| `IGDB_CLIENT_SECRET`      | *(optionnel)* secret de l'app Twitch.                      |

> Si les identifiants d'une source manquent, cette source est simplement
> **ignorée** (loggée en warning) et l'autre reste servie.

## Lancement

```bash
npm start        # démarrage standard
npm run dev      # rechargement auto (node --watch)
```

Au démarrage, le serveur affiche les URLs à utiliser.

## Utilisation avec Tinfoil

1. Ouvrez **Tinfoil** → **File Browser** → ajoutez un nouvel emplacement.
2. Protocole : `http` (ou `https` derrière un reverse proxy).
3. Host / Path : l'hôte et le port de `PUBLIC_BASE_URL` (ex. `192.168.1.50:3000`).
4. Laissez Username / Password **vides** : l'authentification est gérée par le
   proxy, pas par Tinfoil.

Tinfoil interroge `GET /` (l'index unifié) puis télécharge via
`/download/<source>/<token>` (les liens sont déjà réécrits dans l'index).

## Interface web / médiathèque (navigateur, sans Tinfoil)

Une médiathèque (HTML/CSS/Vanilla JS, mode sombre, aucun build) est servie sur
**`/web`** — ex. `http://IP_DU_ZIMAOS:3001/web`. Elle appelle l'index unifié
(`GET /`) et affiche un tableau : **jaquette, nom, console, source, taille** et
un bouton **Télécharger** (routes de proxy relatives). Fonctionnalités :

- **Filtre par nom** (debounced) et **menu déroulant de consoles** alimenté
  dynamiquement (nom de la console = dossier parent côté Magic Monkei).
- **Regroupement par Title ID** (jeux Switch) : les variantes d'un même jeu
  (base, mises à jour v65536/v131072…, DLC) sont empilées sous **une seule
  ligne** avec des badges (dernière MàJ, nb de DLC/versions) ; un **tiroir
  cliquable** déplie chaque variante (Base / MàJ / DLC) avec son propre bouton
  de téléchargement. L'index Tinfoil (`GET /`) reste **plat** : la Switch voit
  toujours tous les fichiers.
- **Jaquettes IGDB** chargées en **lazy-loading** (`IntersectionObserver`) :
  seules les lignes visibles interrogent `/api/cover`, pour éviter le
  rate-limiting sur des dizaines de milliers de jeux. Un **placeholder** sobre
  s'affiche pendant le chargement ou si IGDB ne trouve rien.
- **Fiche détaillée** : un clic sur une jaquette ou un titre ouvre une **modale**
  IGDB (synopsis, date de sortie, note, genres) via `/api/game`.
- **Tri par colonne** : clic sur les en-têtes **Nom** (A→Z / Z→A) et **Taille**
  (lourd→léger / léger→lourd) ; un 3ᵉ clic revient à l'ordre naturel.
- Au-delà de ~1 500 lignes, l'affichage est tronqué (le navigateur ne tient pas
  72 000 lignes DOM) : affinez via le filtre ou la console.

### Jaquettes IGDB

Renseignez `IGDB_CLIENT_ID` / `IGDB_CLIENT_SECRET` (application Twitch, voir
`.env.example`). Sans ces variables, la médiathèque reste fonctionnelle et
affiche le placeholder. Le backend gère l'auth Twitch, la mise en cache du token
et des résultats, et un limiteur de débit.

## Déploiement Docker (ex. ZimaOS, 24h/24)

Le projet inclut un `Dockerfile` (Node.js 22) et un `docker-compose.yml`.

Le `docker-compose.yml` ne dépend **pas** d'un fichier `.env` (qui n'est pas
versionné) : les variables sont listées explicitement et injectées à
l'exécution. Renseignez leurs valeurs selon votre méthode de déploiement.

### Via Portainer (Stacks)

1. **Stacks** → **Add stack**, chargez ce dépôt (ou collez le compose).
2. Dans **Environment variables**, ajoutez les 5 variables :
   `PUBLIC_BASE_URL` (ex. `http://IP_DU_ZIMAOS:3001`), `ULTRANX_LOGIN`,
   `ULTRANX_PASSWORD`, `MAGIC_MONKEI_USER`, `MAGIC_MONKEI_PASS`.
3. **Deploy the stack**.

### Via la ligne de commande

```bash
cd tinfoil-proxy
PUBLIC_BASE_URL=http://IP_DU_ZIMAOS:3001 \
ULTRANX_LOGIN=xxx ULTRANX_PASSWORD=xxx \
MAGIC_MONKEI_USER=xxx MAGIC_MONKEI_PASS=xxx \
docker compose up -d --build
```

> Astuce : `docker compose` charge aussi automatiquement un fichier `.env`
> présent dans le dossier, si vous préférez y mettre ces variables.

Le conteneur écoute en interne sur `3001` et est publié sur `3001` de l'hôte
(pour éviter tout conflit avec une application déjà sur le port 3000).
`restart: unless-stopped` le relance automatiquement au démarrage de la machine
ou après un crash.

Commandes utiles :

```bash
docker compose logs -f      # suivre les logs
docker compose down         # arrêter
docker compose up -d --build   # rebuild + relancer après modification
```

> Dans Tinfoil, pointez le dépôt vers `IP_DU_ZIMAOS:3001`.

## Routes

| Méthode | Route                          | Description                                        |
| ------- | ------------------------------ | -------------------------------------------------- |
| `GET`   | `/`                            | Index Tinfoil unifié (fusion des 2 sources).       |
| `GET`   | `/web`                         | Médiathèque (jaquettes, consoles, téléchargement). |
| `GET`   | `/api/cover?name=<jeu>`        | Jaquette IGDB : `{ cover: "https://…"|null }`.      |
| `GET`   | `/api/game?name=<jeu>`         | Détails IGDB (synopsis, date, note, genres).        |
| `GET`   | `/index/:source/:token`        | Sous-index re-proxifié (navigation dans les dossiers). |
| `GET`   | `/download/:source/:token`     | Streaming du fichier (auth injectée côté serveur). |
| `GET`   | `/health`                      | Sonde de santé (`{ "status": "ok" }`).             |

`:source` vaut `ultranx` ou `magicmonkei`.

## Test rapide

```bash
# Index unifié (récupère et fusionne les deux sources)
curl http://localhost:3000/

# Les liens de téléchargement sont fournis directement dans cet index ;
# il suffit de les suivre (curl -L) pour lancer le stream.
```

## Notes

- Le proxy propage l'en-tête `Range` et renvoie `206 Partial Content` quand la
  source le supporte : la **reprise de téléchargement** fonctionne.
- Si le client Tinfoil coupe la connexion, la requête distante est **annulée**
  (`AbortController`) pour ne pas gaspiller de bande passante.
- Aucun fichier n'est stocké sur le serveur : tout est streamé à la volée.
