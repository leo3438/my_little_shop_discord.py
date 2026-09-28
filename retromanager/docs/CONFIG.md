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
  }
}
```

| Champ | Défaut | Rôle |
|---|---|---|
| `shop.type` | `"ftp"` | `"ftp"` : un vrai serveur. `"mock"` : la boutique de démonstration intégrée (aucun réseau). |
| `shop.url` | `""` | Emplacement de l'index. `ftp://` ou `ftps://` (FTPS **explicite**, AUTH TLS, comme la plupart des NAS). Une URL qui finit par `/` désigne `<url>index.json`. Port 21 par défaut. Le chemin est relatif au dossier de connexion FTP (la racine du partage sur un NAS). |
| `shop.username` / `shop.password` | `""` | Identifiants. Vides : `anonymous`, ou ceux inclus dans l'URL (`ftp://user:pass@hote/`). |
| `shop.verifyTls` | `false` | Vérification du certificat en FTPS. Voir ci-dessous. |

## Comportement

- **Champ manquant** : valeur par défaut. **Champ inconnu** : ignoré.
- **Mauvais type** (mot de passe numérique, `verifyTls: "non"`…) ou JSON
  invalide : la configuration est **refusée** et le fichier n'est **jamais
  réécrit**. La boutique affiche l'erreur (avec la ligne et la colonne), à
  corriger sur la carte SD.
- URL vide, invalide ou non supportée (`http://`, `smb://` pour l'instant) :
  l'application démarre quand même ; l'écran Boutique explique quoi corriger.

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
  au partage des ROMs.
- Les identifiants ne sont envoyés qu'au serveur configuré (même hôte et
  même port). Une entrée d'index pointant ailleurs est refusée
  (`PermissionDenied`) : un index piégé ne peut pas récupérer le mot de passe.
- Les URLs affichées ou écrites dans les logs ne contiennent jamais le mot de
  passe.
