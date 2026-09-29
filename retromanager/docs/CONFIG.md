# Configuration (`config.json`)

RetroManager lit sa configuration dans `sdmc:/switch/RetroManager/config.json`.
Au premier lancement, s'il n'existe pas, un modèle vide est écrit : il suffit
de le compléter. Code : `src/parsers/ConfigParser.cpp`,
`src/services/ConfigManager.cpp`, `src/network/SourceFactory.cpp`.

```json
{
  "version": 1,
  "sources": [
    {
      "name": "NAS",
      "type": "ftp",
      "url": "ftp://192.168.1.20:21/shop/index.json",
      "username": "leo",
      "password": "mon-mot-de-passe",
      "verifyTls": false
    },
    {
      "name": "Boutique web",
      "type": "http",
      "url": "https://retro.example.org/shop.json",
      "verifyTls": true
    }
  ],
  "active_source": "NAS",
  "saves_url": "ftp://192.168.1.20:21/Saves/",
  "sysclk": { "enabled": true, "title_id": "010000000000100D" },
  "scraper": { "enabled": true, "base_url": "https://thumbnails.libretro.com/" },
  "ca_bundle": ""
}
```

Les sources se gèrent aussi depuis l'écran **Sources** de l'accueil (X : un
formulaire Nom / URL de l'index / Utilisateur / Mot de passe, chaque valeur
restant affichée en entier ; choix de la source active ; suppression) : chaque
changement est appliqué tout de suite et réécrit ce fichier. La version
remplacée est gardée dans `config.json.bak`.

![Formulaire d'ajout d'une source (build desktop)](source-form-desktop.png) L'ancien format
à une seule boutique (`"shop": {...}`) est toujours lu : il devient la source
« NAS » et le fichier est réécrit au nouveau format à la première
modification.

| Champ | Défaut | Rôle |
|---|---|---|
| `sources[].name` | `"Source N"` | Nom affiché, unique (casse ignorée). |
| `sources[].type` | déduit de l'URL | `"ftp"` (NAS, FTP/FTPS), `"http"` (boutique web, HTTP/HTTPS), `"mock"` (démo intégrée, aucun réseau). |
| `sources[].url` | `""` | Emplacement de l'index : `ftp://`, `ftps://` (FTPS **explicite**, AUTH TLS), `http://` ou `https://`. Une URL qui finit par `/` désigne `<url>index.json`. En FTP, le chemin est relatif au dossier de connexion (la racine du partage sur un NAS). |
| `sources[].username` / `password` | `""` | Identifiants (FTP, ou authentification Basic en HTTP). Vides : anonyme, ou ceux inclus dans l'URL (`ftp://user:pass@hote/`). |
| `sources[].verifyTls` | FTP : `false` ; HTTP : `true` | Vérification du certificat. Voir ci-dessous. |
| `active_source` | la première | Source affichée par la boutique et l'App Store. Un nom inconnu (source supprimée) revient à la première. |
| `saves_url` | `""` | Dossier du NAS pour les sauvegardes cloud (`ftp://` ou `ftps://`). Vide : bouton « Synchroniser les sauvegardes » inactif (message explicatif). Identifiants : ceux de l'URL (`ftp://user:pass@nas/Saves/`), sinon ceux d'une source FTP **sur le même hôte et le même port**, sinon `anonymous`. Le compte doit pouvoir **écrire** dans ce dossier. |
| `sysclk.enabled` | `true` | Après l'installation d'un jeu N64 / PlayStation, règle sys-clk sur 1785 MHz (CPU, portable et dock) pour RetroArch. La 3DS n'est pas concernée : elle tourne dans Citra (autonome), pas dans RetroArch. |
| `sysclk.title_id` | `"010000000000100D"` | Section de `/config/sys-clk/config.ini` à modifier : 16 chiffres hexadécimaux, sinon la configuration est refusée. Voir ci-dessous. |
| `scraper.enabled` | `true` | Jaquette de secours sur le serveur de vignettes libretro quand l'index n'en donne pas (ou qu'elle est introuvable). Voir ci-dessous. |
| `scraper.base_url` | `"https://thumbnails.libretro.com/"` | Serveur de vignettes (un miroir, ou un serveur local de test). |
| `ca_bundle` | `""` | Fichier PEM d'autorités de certification pour HTTPS (chemin de l'hôte : `sdmc:/switch/RetroManager/cacert.pem` sur Switch). Vide : celui de libcurl. |

## Boutiques web (HTTP/HTTPS)

- L'index et les fichiers peuvent être n'importe où sur le web : une URL
  relative se résout par rapport à l'index, une URL absolue peut viser un
  autre site. Les **identifiants** ne partent que vers le schéma, l'hôte et
  le port de la source elle-même, jamais vers un autre site ni à travers une
  redirection.
- Téléchargements **reprenables** : `Range: bytes=<déjà reçu>-`. Un serveur
  qui ignore l'en-tête (réponse 200) ou le refuse (416) est détecté avant
  qu'un seul octet ne soit écrit, et le fichier repart de zéro.
- Les pages d'erreur ne sont jamais enregistrées comme ROM : le code HTTP est
  vérifié d'abord (404 → introuvable, 401 → identifiants, 403 → refusé).
- **Certificats sur Switch** : HTTPS est vérifié par défaut. Si la console
  refuse un site au certificat pourtant valide, déposez un `cacert.pem`
  (par exemple celui de https://curl.se/docs/caextract.html) et indiquez-le
  dans `ca_bundle`, ou, en dernier recours, `verifyTls: false` pour cette
  source (chiffré mais non authentifié).

## Scraper de jaquettes

Quand un jeu n'a pas de `boxart` dans l'index (ou qu'elle est introuvable),
RetroManager essaie
`<base_url><système libretro>/Named_Boxarts/<nom de la ROM sans extension>.png`,
par exemple
`https://thumbnails.libretro.com/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/Named_Boxarts/Super%20Mario%20World%20(USA).png`.
Cela fonctionne quand les ROMs portent leur nom **No-Intro** (celui des bases
libretro). Une absence (404) ou une console hors ligne sont ignorées sans
message. Désactivez-le avec `"scraper": {"enabled": false}`.

## File de téléchargements

La file est enregistrée dans `/switch/RetroManager/queue.json` à chaque
changement. Au lancement suivant, les éléments qui restaient (y compris celui
qui était en cours) sont remis en file automatiquement et reprennent grâce à
leur fichier `.tmp`. Supprimer `queue.json` vide la file.

## Écrire ses sources à la main

Le fichier peut être rédigé directement (Bloc-notes, éditeur du NAS…), sans
passer par le menu d'ajout ; il est relu à chaque lancement. RetroManager
accepte ce qu'on écrit en pratique :

```jsonc
{
  // commentaires // et /* */, virgules finales, BOM UTF-8 du Bloc-notes
  "sources": [
    { "name": "NAS du salon", "type": "FTP", "url": "ftp://192.168.1.20/shop/",
      "user": "leo", "pass": 1234, },
    { "name": "Web", "index_url": "https://retro.example.org/shop.json" },
    "ftp://192.168.1.21/jeux/",
  ],
  "active_source": "nas du salon",
}
```

- **Alias** : `user` / `login` pour `username`, `pass` pour `password`,
  `index_url` / `index` / `address` pour `url`, `label` pour `name`,
  `verify_tls` pour `verifyTls` ; `type` sans casse, `https` / `web` = `http`,
  `ftps` / `nas` = `ftp`.
- Une source peut n'être qu'une **URL** (nom « Source N ») ; `"sources"` peut
  être un seul objet au lieu d'un tableau ; un mot de passe ou un identifiant
  tapé sans guillemets (`1234`) est accepté ; `true` / `false` s'écrivent
  aussi `oui` / `non`, `yes` / `no`.
- Deux sources au même nom : la seconde devient « Nom (2) ».

## Comportement

- **Champ manquant** : valeur par défaut. **Champ inconnu** : ignoré.
- **Source incohérente** (type `smb`, mot de passe en tableau…) : **cette
  source seule** est ignorée, jamais remplacée par des valeurs par défaut ;
  les autres sont chargées. L'écran **Sources** liste ce qui a été ignoré et
  pourquoi. Un champ général de mauvais type (`saves_url: 3`…) garde sa
  valeur par défaut, avec le même avertissement.
- **JSON illisible** (accolade manquante…) : la configuration est refusée et
  le fichier n'est **jamais réécrit** ; les écrans Boutique et Sources
  affichent l'erreur avec la **ligne et la colonne**, à corriger sur la carte
  SD.
- URL vide, invalide ou non supportée (`smb://` pour l'instant) :
  l'application démarre quand même ; l'écran Boutique explique quoi corriger.

## Sauvegardes cloud

- Fichiers synchronisés : `.srm`, `.sav`, `.dsv` du dossier de sauvegardes de
  RetroArch (`savefile_directory` de `retroarch.cfg`, `/retroarch/saves`
  par défaut), sous-dossiers compris (3 niveaux).
- L'état de la dernière synchro est dans `/switch/RetroManager/sync-state.json`.
  Le supprimer est sans danger : la synchro suivante compare les fichiers
  comme au premier jour (identiques à 2 s près = à jour, sinon conflit).
- **Conflit** (modifié des deux côtés) : la version la plus récente est mise
  en place des deux côtés, l'autre est gardée à côté sous le nom
  `<fichier>.conflict-local-…` ou `.conflict-remote-…`. Pour revenir à
  l'autre version, renommez la copie. RetroArch ignore ces copies.
- **Suppressions non propagées** : un fichier supprimé d'un côté revient de
  l'autre. Pour supprimer une sauvegarde, supprimez-la des deux côtés.
- Chaque console devrait avoir l'heure juste (réglage automatique) : l'heure
  ne sert qu'à départager un conflit.

## sys-clk et title id

sys-clk applique un profil selon le **title id du programme au premier
plan**. Il dépend de la façon dont RetroArch est lancé :

- `010000000000100D` (défaut) : l'applet Album. RetroArch est un `.nro`
  lancé depuis hbmenu, qui tourne dans l'Album : c'est le cas courant. Le
  profil s'applique alors à tous les homebrews lancés de cette façon.
- Un autre identifiant si RetroArch est lancé autrement (forwarder installé,
  jeu détourné en mode application) : relevez celui qu'affiche l'overlay de
  sys-clk pendant un jeu et mettez-le dans `title_id`.

Le fichier n'est modifié que si sys-clk est installé (`/config/sys-clk`
présent) ; une copie `config.ini.rmbak` est faite avant la première
modification et le reste du fichier (commentaires, autres titres, section
`[values]`) est conservé tel quel.

## FTPS et certificats

Un NAS domestique présente en général un certificat **auto-signé**, que la
console ne peut pas valider. En outre, la libcurl de la Switch n'embarque
aucune autorité de certification. D'où `verifyTls: false` par défaut :

- la connexion reste **chiffrée** (mot de passe et ROMs ne circulent pas en
  clair sur le Wi-Fi) ;
- mais le serveur n'est **pas authentifié** : sur un réseau hostile, un
  attaquant pourrait se faire passer pour le NAS.

Passez `verifyTls` à `true` pour un serveur doté d'un certificat reconnu
(sur desktop ; sur Switch il faudra fournir un bundle CA, non géré pour
l'instant). Les deux cas sont couverts par des tests d'intégration contre un
serveur FTPS au certificat auto-signé (`tests/integration/FtpDownloadIntegrationTest.cpp`).

## Sécurité

- Le mot de passe est stocké **en clair** sur la carte SD. N'utilisez pas un
  compte administrateur du NAS : créez un compte en **lecture seule** limité
  au partage des ROMs, et, pour les sauvegardes, un compte (dans
  `saves_url`) qui n'a le droit d'écrire que dans leur dossier.
- Les identifiants ne sont envoyés qu'au serveur configuré (même hôte et
  même port). Une entrée d'index pointant ailleurs est refusée
  (`PermissionDenied`) : un index piégé ne peut pas récupérer le mot de passe.
- Les URLs affichées ou écrites dans les logs ne contiennent jamais le mot de
  passe.
