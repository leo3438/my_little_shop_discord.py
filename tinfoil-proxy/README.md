# Proxy / Agrégateur Tinfoil — UltraNX + Magic Monkei

Serveur **Node.js / Express** qui unifie deux sources privées
(**UltraNX** et **Magic Monkei**) en un **seul dépôt Tinfoil transparent**
pour Nintendo Switch.

- Génère un index JSON combiné compatible Tinfoil (`files` + `directories`).
- Fait un **stream/pipe direct, octet par octet**, des téléchargements, en
  conservant les en-têtes d'origine (`Content-Length`, `Content-Disposition`…).
- Injecte automatiquement les identifiants requis par chaque source :
  - **UltraNX** → en-tête `Cookie: auth_token=…`
  - **Magic Monkei** → en-tête `Authorization: Basic …`

La Switch ne voit qu'un seul dépôt ; les secrets restent sur le serveur.

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
    │   ├── index.js        # GET /            → index Tinfoil combiné
    │   └── download.js     # GET /download/*  → proxys UltraNX & Magic Monkei
    └── services/
        └── proxy.js        # streaming générique (fetch → pipe vers le client)
```

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

| Variable                | Rôle                                                        |
| ----------------------- | ----------------------------------------------------------- |
| `PORT`                  | Port d'écoute local (par défaut `3000`).                    |
| `PUBLIC_BASE_URL`       | URL joignable depuis la Switch (ex. `http://192.168.1.50:3000`). |
| `ULTRANX_API_BASE`      | Base de l'API distante UltraNX.                            |
| `ULTRANX_AUTH_TOKEN`    | Cookie réseau UltraNX (`auth_token`).                      |
| `MAGIC_MONKEI_API_BASE` | Base de l'API distante Magic Monkei.                       |
| `MAGIC_MONKEI_USER`     | Identifiant Basic Auth Magic Monkei.                       |
| `MAGIC_MONKEI_PASS`     | Mot de passe Basic Auth Magic Monkei.                      |

> Les jeux de l'index sont **mockés** pour tester le routage ; l'agrégation
> fonctionne même sans secrets, mais les téléchargements réels échoueront tant
> que les identifiants ne sont pas renseignés.

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

Tinfoil interroge `GET /` (l'index combiné) puis télécharge via
`/download/ultranx/:id` ou `/download/magicmonkei/:id`.

## Routes

| Méthode | Route                          | Description                                   |
| ------- | ------------------------------ | --------------------------------------------- |
| `GET`   | `/`                            | Index Tinfoil combiné (JSON).                 |
| `GET`   | `/download/ultranx/:id`        | Proxy UltraNX (injecte le Cookie).            |
| `GET`   | `/download/magicmonkei/:id`    | Proxy Magic Monkei (injecte le Basic Auth).   |
| `GET`   | `/health`                      | Sonde de santé (`{ "status": "ok" }`).        |

## Test rapide

```bash
# Index combiné
curl http://localhost:3000/

# Route mockée UltraNX (échoue proprement si le token n'est pas configuré)
curl -v http://localhost:3000/download/ultranx/12345
```

## Notes

- Le proxy propage l'en-tête `Range` et renvoie `206 Partial Content` quand la
  source le supporte : la **reprise de téléchargement** fonctionne.
- Si le client Tinfoil coupe la connexion, la requête distante est **annulée**
  (`AbortController`) pour ne pas gaspiller de bande passante.
- Aucun fichier n'est stocké sur le serveur : tout est streamé à la volée.
