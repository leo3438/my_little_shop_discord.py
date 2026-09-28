# Configuration (`config.json`)

RetroManager lit sa configuration dans `sdmc:/switch/RetroManager/config.json`.
Au premier lancement, s'il n'existe pas, un modèle vide est écrit : il suffit
de le compléter. Code : `src/parsers/ConfigParser.cpp`,
`src/services/ConfigManager.cpp`, `src/network/SourceFactory.cpp`.

```json
{
  "version": 1,
  "shop": {
    "type": "ftp",
    "url": "ftp://192.168.1.20:21/shop/index.json",
    "username": "leo",
    "password": "mon-mot-de-passe",
    "verifyTls": false
  },
  "saves_url": "ftp://192.168.1.20:21/Saves/",
  "sysclk": {
    "enabled": true,
    "title_id": "05B9D58000000000"
  }
}
```

| Champ | Défaut | Rôle |
|---|---|---|
| `shop.type` | `"ftp"` | `"ftp"` : un vrai serveur. `"mock"` : la boutique de démonstration intégrée (aucun réseau). |
| `shop.url` | `""` | Emplacement de l'index. `ftp://` ou `ftps://` (FTPS **explicite**, AUTH TLS, comme la plupart des NAS). Une URL qui finit par `/` désigne `<url>index.json`. Port 21 par défaut. Le chemin est relatif au dossier de connexion FTP (la racine du partage sur un NAS). |
| `shop.username` / `shop.password` | `""` | Identifiants. Vides : `anonymous`, ou ceux inclus dans l'URL (`ftp://user:pass@hote/`). |
| `shop.verifyTls` | `false` | Vérification du certificat en FTPS. Voir ci-dessous. Vaut aussi pour `saves_url`. |
| `saves_url` | `""` | Dossier du NAS pour les sauvegardes cloud (`ftp://` ou `ftps://`). Vide : bouton « Synchroniser les sauvegardes » inactif (message explicatif). Identifiants : ceux de l'URL (`ftp://user:pass@nas/Saves/`), sinon ceux de la boutique **si et seulement si** même hôte et même port, sinon `anonymous`. Le compte doit pouvoir **écrire** dans ce dossier (envoi, renommage, création de sous-dossiers). |
| `sysclk.enabled` | `true` | Après l'installation d'un jeu N64 / PlayStation / 3DS, règle sys-clk sur 1785 MHz (CPU, portable et dock) pour RetroArch. |
| `sysclk.title_id` | `"05B9D58000000000"` | Section de `/config/sys-clk/config.ini` à modifier : 16 chiffres hexadécimaux, sinon la configuration est refusée. Voir ci-dessous. |

## Comportement

- **Champ manquant** : valeur par défaut. **Champ inconnu** : ignoré.
- **Mauvais type** (mot de passe numérique, `verifyTls: "non"`…) ou JSON
  invalide : la configuration est **refusée** et le fichier n'est **jamais
  réécrit**. La boutique affiche l'erreur (avec la ligne et la colonne), à
  corriger sur la carte SD.
- URL vide, invalide ou non supportée (`http://`, `smb://` pour l'instant) :
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

- `05B9D58000000000` (défaut) : l'identifiant attribué à RetroArch lancé
  comme titre (forwarder installé, etc.). **Non vérifié sur console** :
  contrôlez celui qu'affiche l'overlay de sys-clk pendant un jeu.
- `010000000000100D` : l'applet Album, c'est-à-dire tout homebrew `.nro`
  lancé depuis le menu hbmenu par l'Album (le profil s'applique alors à
  tous les homebrews ainsi lancés).

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
