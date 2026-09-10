# Anime-Sama → Jellyfin Sync

Système en deux parties pour récupérer automatiquement les épisodes regardés
sur **Anime-Sama** et les intégrer dans **Jellyfin** :

1. **Extension de navigateur** (Chrome / Firefox, Manifest V3) — capture les
   métadonnées de l'épisode et l'URL du flux vidéo, puis les envoie au backend.
2. **Backend local léger** (Python FastAPI + SQLite) — enregistre l'épisode,
   télécharge la vidéo (yt-dlp / aria2c), la range selon la convention Jellyfin,
   puis déclenche un rafraîchissement de la bibliothèque.

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

Exemple de payload attendu :
```json
{
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

### Comportement
1. Réception → vérification des doublons (**anime + saison + épisode + langue**).
2. Sauvegarde en base (`status = pending`).
3. Tâche de fond : téléchargement via yt-dlp/aria2c en réutilisant `User-Agent`
   et `Referer`.
4. Rangement au format Jellyfin :
   ```
   {MEDIA_ROOT}/{Anime Title}/Season {SS}/{Anime Title} - S{SS}E{EE} [{Language}].mp4
   ```
5. Appel `POST {JELLYFIN_URL}/Library/Refresh?api_key=...` pour actualiser la
   bibliothèque.

Un épisode dont le téléchargement a échoué (`status = failed`) est
automatiquement relancé si l'extension le renvoie.

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
- **`content.js`** s'exécute sur les pages `anime-sama.fr`. Il lit le titre, la
  saison (`saisonN` dans l'URL), le numéro d'épisode (sélecteur `#selectEpisodes`)
  et la langue (`vostfr`/`vf`), puis transmet ces métadonnées au service worker à
  chaque changement d'épisode ou de lecteur.
- **`background.js`** intercepte les requêtes réseau via `chrome.webRequest`.
  Dès qu'un flux `.mp4`/`.m3u8` (ou une requête vers Sibnet/Sendvid/Vidmoly...)
  est détecté pour l'onglet, il récupère les en-têtes utiles (notamment le
  `Referer`), les associe aux métadonnées et envoie le tout au backend.

Un badge sur l'icône indique le résultat de l'envoi (`OK` / `ERR`).

### Permissions demandées
| Permission        | Raison                                                        |
|-------------------|---------------------------------------------------------------|
| `webRequest`      | Observer les requêtes pour capturer l'URL du flux + en-têtes  |
| `storage`         | Mémoriser l'URL du backend                                    |
| `tabs`            | Nettoyer l'état lié à un onglet fermé                         |
| `host_permissions`| anime-sama + hébergeurs vidéo + `localhost` (envoi du POST)   |

> L'interception réseau est **observationnelle** (pas de `webRequestBlocking`),
> pleinement compatible Manifest V3.

---

## Bout-en-bout, en résumé
```
[anime-sama.fr] --(content.js: métadonnées)--> [service worker]
[lecteur vidéo] --(webRequest: URL + Referer)--> [service worker]
                                                     |
                                        POST /api/episode
                                                     v
                                  [FastAPI] --enregistre--> [SQLite]
                                       |
                              tâche de fond (yt-dlp/aria2c)
                                       v
                    {MEDIA_ROOT}/{Anime}/Season {SS}/... .mp4
                                       |
                            POST /Library/Refresh
                                       v
                                  [Jellyfin] 🎬
```
