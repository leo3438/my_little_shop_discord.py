# Anime-Sama / Afterdark → Jellyfin Sync

Système en deux parties pour récupérer automatiquement les contenus regardés
sur **Anime-Sama** et **Afterdark** (`afd926.mom` et ses miroirs) et les
intégrer dans **Jellyfin** :

1. **Extension de navigateur** (Chrome / Firefox, Manifest V3) — capture les
   métadonnées du contenu (avec son **type** : `series`, `movie`, `manga`,
   `live_tv`) et l'URL du flux vidéo, puis les envoie au backend.
2. **Backend local léger** (Python FastAPI + SQLite) — enregistre le contenu,
   télécharge la vidéo (yt-dlp / aria2c) ou inscrit la chaîne en Live TV, range
   le fichier selon la convention Jellyfin, puis rafraîchit la bibliothèque.

**Types de média pris en charge**

| Type      | Source                       | Traitement backend                          |
|-----------|------------------------------|---------------------------------------------|
| `series`  | Anime-Sama, Afterdark        | Téléchargement → `Shows/`                    |
| `manga`   | Afterdark                    | Téléchargement → `Shows/`                    |
| `movie`   | Afterdark                    | Téléchargement → `Movies/`                   |
| `live_tv` | Afterdark                    | Ajout / mise à jour dans `iptv.m3u`          |

> ⚠️ **Usage** : à réserver au téléchargement de contenus que vous êtes en droit
> de récupérer. Respectez la législation applicable et les conditions
> d'utilisation des sites concernés.

---

## Arborescence du projet

```
anime-sama-jellyfin-sync/
├── README.md
├── extension/                  # Extension navigateur (Manifest V3)
│   ├── manifest.json
│   ├── background.js           # Service worker : interception réseau + POST
│   ├── content.js              # Extraction des métadonnées de la page
│   ├── popup.html              # Réglage de l'URL du backend
│   └── popup.js
└── backend/                    # API + worker de téléchargement
    ├── app/
    │   ├── __init__.py
    │   ├── main.py             # FastAPI : endpoint POST /api/episode
    │   ├── config.py           # Configuration (.env)
    │   ├── database.py         # Moteur SQLite + session
    │   ├── models.py           # Modèle SQLModel `Episode` (+ contrainte d'unicité)
    │   ├── schemas.py          # Schémas Pydantic (entrée/sortie)
    │   ├── downloader.py        # Worker async : yt-dlp/aria2c + rangement Jellyfin
    │   └── jellyfin.py         # Appel /Library/Refresh
    ├── requirements.txt
    ├── run.sh                  # Lancement en dev
    ├── .env.example
    └── .gitignore
```

---

## 1. Backend

### Prérequis
- **Python 3.10+**
- **[yt-dlp](https://github.com/yt-dlp/yt-dlp)** (recommandé) **+ ffmpeg** (nécessaire
  pour fusionner les flux `.m3u8`). Alternative : **aria2c** (uniquement pour les
  téléchargements directs `.mp4`).

Sous Debian/Ubuntu :
```bash
sudo apt install ffmpeg aria2
pipx install yt-dlp        # ou: pip install yt-dlp
```

### Installation
```bash
cd backend
python -m venv .venv
source .venv/bin/activate            # Windows : .venv\Scripts\activate
pip install -r requirements.txt

cp .env.example .env                 # puis éditez .env (clé Jellyfin, MEDIA_ROOT...)
```

### Configuration (`.env`)
Les principales variables (voir `.env.example` pour la liste complète) :

| Variable                 | Rôle                                                        |
|--------------------------|-------------------------------------------------------------|
| `HOST` / `PORT`          | Adresse d'écoute (par défaut `127.0.0.1:8000`)              |
| `MEDIA_ROOT`             | Racine de la bibliothèque Jellyfin où ranger les fichiers   |
| `DOWNLOADER`             | `yt-dlp` (défaut) ou `aria2c`                               |
| `JELLYFIN_URL`           | URL de votre serveur Jellyfin                               |
| `JELLYFIN_API_KEY`       | Clé API Jellyfin (Tableau de bord → Clés API)              |
| `JELLYFIN_REFRESH_ENABLED` | `true`/`false` pour (dé)activer le scan automatique       |
| `IPTV_PLAYLIST_PATH`     | Playlist M3U Live TV (défaut : `{MEDIA_ROOT}/iptv.m3u`)     |

### Lancement
```bash
./run.sh
# ou directement :
uvicorn app.main:app --host 127.0.0.1 --port 8000 --reload
```

- Documentation interactive : <http://localhost:8000/docs>
- Santé : <http://localhost:8000/health>

### Endpoints

| Méthode | Chemin                   | Description                                   |
|---------|--------------------------|-----------------------------------------------|
| `POST`  | `/api/episode`           | Webhook appelé par l'extension                |
| `GET`   | `/api/episodes`          | Liste des épisodes enregistrés                |
| `GET`   | `/api/episodes/{id}`     | Détail d'un épisode                           |
| `GET`   | `/health`                | Vérification de disponibilité                 |

Exemples de payload attendu :
```json
// Série (Anime-Sama)
{
  "media_type": "series",
  "anime_title": "One Piece",
  "season": 1,
  "episode_number": 1071,
  "language": "VOSTFR",
  "video_url": "https://video.sibnet.ru/v/.../123456.mp4",
  "headers": {
    "Referer": "https://anime-sama.fr/catalogue/one-piece/saison1/vostfr/",
    "User-Agent": "Mozilla/5.0 ..."
  }
}
```
```json
// Film (Afterdark)
{ "media_type": "movie", "anime_title": "Blade Runner", "year": 1982,
  "language": "VO", "video_url": "https://.../movie.mp4",
  "headers": { "Referer": "https://afd926.mom/..." } }
```
```json
// TV en direct (Afterdark)
{ "media_type": "live_tv", "channel_name": "TF1", "anime_title": "TF1",
  "language": "VF", "video_url": "https://.../tf1.m3u8",
  "headers": { "Referer": "https://afd926.mom/live" } }
```

### Comportement
1. Réception → vérification des doublons (**anime + saison + épisode + langue**).
2. Sauvegarde en base (`status = pending`).
3. Tâche de fond : téléchargement via yt-dlp/aria2c en réutilisant `User-Agent`
   et `Referer`.
4. Rangement au format Jellyfin, **selon le type de média** :
   ```
   # movie
   {MEDIA_ROOT}/Movies/{Titre} ({Année})/{Titre} ({Année}) [{Langue}].mp4
   # series / manga
   {MEDIA_ROOT}/Shows/{Titre}/Season {SS}/{Titre} - S{SS}E{EE} [{Langue}].mp4
   ```
5. Appel `POST {JELLYFIN_URL}/Library/Refresh?api_key=...` pour actualiser la
   bibliothèque.

Un contenu dont le téléchargement a échoué (`status = failed`) est
automatiquement relancé si l'extension le renvoie.

### Live TV (`media_type = live_tv`)
Les chaînes ne sont **pas téléchargées** : elles sont ajoutées / mises à jour
(upsert par nom de chaîne) dans une playlist M3U locale :

```
{IPTV_PLAYLIST_PATH}   (défaut : {MEDIA_ROOT}/iptv.m3u)
```

Chaque entrée conserve les en-têtes utiles au lecteur :
```m3u
#EXTINF:-1 tvg-name="TF1" group-title="Live TV",TF1
#EXTVLCOPT:http-referrer=https://afd926.mom/live
#EXTVLCOPT:http-user-agent=Mozilla/5.0 ...
https://.../stream.m3u8
```

> Dans Jellyfin : **Tableau de bord → Live TV → Tuners → M3U Tuner**, puis
> pointez le tuner vers ce fichier `iptv.m3u`. Le statut de l'entrée en base
> passe à `registered`.

---

## 2. Extension de navigateur

### Chargement (Chrome / Edge)
1. Ouvrez `chrome://extensions`.
2. Activez le **Mode développeur**.
3. **Charger l'extension non empaquetée** → sélectionnez le dossier `extension/`.

### Chargement (Firefox)
1. Ouvrez `about:debugging#/runtime/this-firefox`.
2. **Charger un module complémentaire temporaire** → sélectionnez
   `extension/manifest.json`.

### Réglage
Cliquez sur l'icône de l'extension pour définir l'endpoint du backend
(par défaut `http://localhost:8000/api/episode`).

### Fonctionnement
- **`content.js`** s'exécute sur `anime-sama.fr` et sur `*.mom` (Afterdark). Un
  *dispatcher* choisit l'analyseur selon le domaine :
  - **Anime-Sama** → `series` : titre, saison (`saisonN`), épisode
    (`#selectEpisodes`), langue (`vostfr`/`vf`).
  - **Afterdark** → détecte le **type de média** depuis l'URL puis le DOM :
    - `movie` : titre + année ;
    - `series` / `manga` : titre + saison + épisode ;
    - `live_tv` : nom de la chaîne.
  Les métadonnées (avec `media_type`) sont transmises au service worker à chaque
  changement de sélection / lecteur.
- **`background.js`** intercepte les requêtes réseau via `chrome.webRequest`.
  Dès qu'un flux `.mp4`/`.m3u8` est détecté pour un onglet **pour lequel des
  métadonnées existent**, il récupère les en-têtes utiles (notamment le
  `Referer`), les associe aux métadonnées et envoie le tout au backend.

Un badge sur l'icône indique le résultat de l'envoi (`OK` / `ERR`).

> ⚙️ **Afterdark — sélecteurs** : la structure d'Afterdark n'étant pas
> standardisée (et variable selon les miroirs), l'analyse s'appuie d'abord sur
> l'URL puis sur des sélecteurs DOM génériques regroupés dans
> `AFTERDARK_SELECTORS` (en haut de `content.js`). Ajustez-les si un miroir
> diffère.

### Permissions demandées
| Permission        | Raison                                                        |
|-------------------|---------------------------------------------------------------|
| `webRequest`      | Observer les requêtes pour capturer l'URL du flux + en-têtes  |
| `storage`         | Mémoriser l'URL du backend                                    |
| `tabs`            | Nettoyer l'état lié à un onglet fermé                         |
| `host_permissions`| anime-sama, `*.mom`, `localhost` et `*://*/*`                 |

> `*://*/*` permet d'**observer** les requêtes vidéo servies par des CDN tiers
> (indispensable pour les hébergeurs variés d'Afterdark). La capture reste
> néanmoins limitée aux onglets où le content script s'exécute (anime-sama /
> `*.mom`), car le service worker n'envoie un payload que si des métadonnées
> existent pour l'onglet.
>
> Le domaine d'Afterdark change régulièrement ; le pattern `*://*.mom/*` couvre
> les miroirs `afdXXX.mom`. Pour un miroir sur un autre TLD, ajoutez son motif
> dans `manifest.json` (`content_scripts.matches`) et le test de domaine dans
> `content.js` (`isAfterdark`).
>
> L'interception réseau est **observationnelle** (pas de `webRequestBlocking`),
> pleinement compatible Manifest V3.

---

## Bout-en-bout, en résumé
```
[anime-sama.fr / *.mom] --(content.js: métadonnées + media_type)--> [service worker]
[lecteur vidéo]         --(webRequest: URL + Referer)------------->  [service worker]
                                                     |
                                        POST /api/episode
                                                     v
                                  [FastAPI] --enregistre--> [SQLite]
                                       |
                    ┌──────────────────┼───────────────────────────┐
              movie / series/manga                              live_tv
              (yt-dlp / aria2c)                          (upsert playlist)
                    v                                            v
   Movies/{Titre} ({Année})/...            {IPTV_PLAYLIST_PATH} (iptv.m3u)
   Shows/{Titre}/Season SS/...                                   |
                    |                                            |
              POST /Library/Refresh <───────────────────────────┘
                                       v
                                  [Jellyfin] 🎬
```
