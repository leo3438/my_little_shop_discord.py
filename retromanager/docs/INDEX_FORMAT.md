# Format d'index de boutique (v1)

Une boutique RetroManager est un fichier JSON (par convention `index.json`)
posé sur le NAS, à côté des ROMs. Le format s'inspire des index Tinfoil et
en accepte la forme minimale (`files` + `success`), en y ajoutant les
métadonnées utiles à l'émulation.

Parser : `src/parsers/RepoIndexParser.cpp` · Tests : `tests/unit/RepoIndexParserTest.cpp`.

## Exemple

```json
{
  "version": 1,
  "name": "NAS de Léo",
  "success": "Bienvenue sur ma boutique !",
  "games": [
    {
      "title": "Pokémon Platine",
      "system": "nds",
      "region": "EUR",
      "size": 134217728,
      "url": "roms/nds/Pokemon%20Platine%20(France).nds",
      "boxart": "boxart/nds/pokemon-platine.png",
      "crc32": "9a2d6a7e",
      "year": 2009,
      "description": "Version française."
    },
    { "url": "roms/snes/Super%20Mario%20World%20(USA).sfc", "size": 524288 }
  ],
  "emulators": [
    { "title": "melonDS", "author": "Arisotura", "version": "0.9.5",
      "description": "Émulateur Nintendo DS.", "url_nro": "apps/melonDS/melonDS.nro" }
  ],
  "apps": [
    { "title": "RetroArch", "author": "libretro", "version": "1.19.1",
      "url_nro": "apps/RetroArch/retroarch_switch.nro", "url_icon": "apps/RetroArch/icon.jpg" }
  ],
  "bios": [
    { "file": "scph5501.bin", "system": "psx", "url": "bios/scph5501.bin",
      "md5": "490f666e1afb15b7362b406ed1cea246", "size": 524288 },
    { "url": "bios/gba_bios.bin" }
  ]
}
```

## Racine

| Champ | Type | Obligatoire | Rôle |
|---|---|---|---|
| `version` | entier ≥ 1 | non (défaut 1) | Version du format. Une version supérieure à celle supportée est refusée (`Unsupported`). |
| `name` | chaîne | non | Nom affiché de la boutique. |
| `success` | chaîne | non | Message d'accueil (même champ que Tinfoil). |
| `games` | tableau | au moins une section parmi `games`, `files`, `apps`, `emulators` | Entrées de jeux. |
| `files` | tableau | idem | Alias Tinfoil, même format d'entrée. Traité après `games`. |
| `apps`, `emulators` | tableaux | idem | Homebrews Switch (`.nro`), affichés dans « Émulateurs & Homebrews » en deux sections. Voir plus bas. |
| `bios` | tableau | non | Fichiers BIOS que la boutique peut fournir (écran « Vérification des BIOS »). Voir plus bas. |
| `directories` | tableau | non | Sous-index Tinfoil : **pas encore supporté**, ignoré avec un avertissement. |

Tout autre champ est ignoré (compatibilité ascendante).

## Entrée de jeu

| Champ | Type | Défaut si absent |
|---|---|---|
| `url` | chaîne, **obligatoire** | — |
| `title` | chaîne non vide | nom du fichier sans extension |
| `system` | chaîne (id, voir plus bas) | déduit de l'extension, sinon `unknown` |
| `region` | chaîne libre (`EUR`, `USA`, `JPN`…) | vide |
| `size` | entier ≥ 0, en octets ; sert au contrôle d'espace libre avant téléchargement | 0 (inconnue : pas de contrôle) |
| `boxart` (alias `url_boxart`) | chaîne (URL) d'une jaquette **PNG**, installée pour RetroArch après la ROM (voir plus bas). Si les deux sont présents, `boxart` l'emporte | vide |
| `crc32` | 8 chiffres hexadécimaux, **vérifié après téléchargement** (fichier rejeté si différent) | vide |
| `year` | entier entre 1950 et 2100 | absent |
| `description` | chaîne | vide |
| `cheat_url` | chaîne (URL, relative acceptée) d'un fichier de triche RetroArch `.cht`, téléchargé après la ROM | vide |
| `id` | chaîne unique dans l'index | `<system>/<nom de fichier>` |

### URLs

- Absolues (`ftp://`, `http(s)://`, `smb://`…) ou **relatives à l'emplacement
  de l'index** (`roms/a.sfc`, `../art/a.png`, `/roms/a.sfc`), résolues selon
  la RFC 3986. Une URL relative permet de déplacer la boutique sans réécrire
  l'index.
- Les caractères spéciaux doivent être encodés (`%20` pour un espace).
- Convention Tinfoil : un fragment `#Nom%20du%20fichier.gba` fixe le nom du
  fichier sur la SD, utile quand l'URL n'en contient pas (`dl?id=42`). Le
  fragment est retiré de l'URL de téléchargement.

### Hébergement des ROMs

Les identifiants du NAS ne sont envoyés qu'au serveur de l'index (même hôte,
même port) : une entrée pointant vers un autre serveur est refusée au
téléchargement. Les ROMs sont installées dans `/roms/<system>/<nom>` ; les
caractères interdits sur FAT/exFAT (`<>:"|?*`) deviennent `_`.

### Codes de triche

Le `.cht` est installé dans
`/retroarch/cheats/<système libretro>/<nom de la ROM sans extension>.cht`
(par exemple `Nintendo - Nintendo DS/Pokemon Platine (France).cht`), ou
sous le `cheat_database_path` de `retroarch.cfg` s'il est absolu. Il doit
contenir une ligne `cheats = N`, sinon il est refusé (page d'erreur HTML,
fichier tronqué…). Au maximum 1 Mio.

### Jaquettes

L'image est installée dans
`/retroarch/thumbnails/<système libretro>/Named_Boxarts/<nom de la ROM sans extension>.png`
(ou sous le `thumbnails_directory` de `retroarch.cfg` s'il est absolu) : c'est
là que RetroArch la cherche pour l'entrée de playlist que RetroManager crée
(`label` = nom de la ROM sans extension, les caractères `& * / : < > ? \ |`
et l'accent grave devenant `_`). **PNG uniquement** (RetroArch ne charge pas d'autre format en
vignette) ; 8 Mio au maximum. Elle doit être sur le serveur de la boutique
(mêmes règles que les ROMs).

### Playlists

Chaque jeu installé est ajouté (ou mis à jour, jamais dupliqué) dans
`/retroarch/playlists/<système libretro>.lpl`, par exemple
`Nintendo - Nintendo DS.lpl` : chemin de la ROM, libellé, cœur `DETECT`
(RetroArch utilise le cœur par défaut de la playlist ou le demande) et le
CRC-32 mesuré pendant le téléchargement. Une playlist existante est
complétée sans rien perdre de ce que RetroArch y a écrit.

### Identifiants de système

`nes`, `snes`, `n64`, `gb`, `gbc`, `gba`, `nds`, `mastersystem`, `megadrive`,
`gamegear`, `pcengine`, `psx`, `arcade`. Insensibles à la casse. Un id inconnu
est accepté (affiché tel quel, en fin de liste). Les extensions ambiguës
(`.zip`, `.7z`, `.iso`, `.chd`…) ne permettent pas de déduire le système :
renseignez `system`. Liste de référence : `src/models/Systems.cpp`.

## Sections `apps` et `emulators`

| Champ | Type | Défaut si absent |
|---|---|---|
| `title` | chaîne, **obligatoire** | — |
| `url_nro` (alias `url`) | chaîne (URL, relative acceptée), **obligatoire** | — |
| `author` | chaîne | vide |
| `version` | chaîne libre (`1.19.1`, `v2.0-beta`) : comparée à la version installée pour afficher « MISE À JOUR » | vide (pas de détection) |
| `description` | chaîne | vide |
| `url_icon` | chaîne (URL) d'une icône **JPEG** (256×256 conseillé), installée en `/switch/<dossier>/icon.jpg` | vide |
| `folder` | nom du dossier et du `.nro` | le titre, rendu sûr pour FAT |
| `size` | entier ≥ 0, en octets | demandée au serveur au moment du téléchargement |
| `crc32` | 8 chiffres hexadécimaux, vérifié | vide |

L'application est installée en `/switch/<dossier>/<dossier>.nro`, par
exemple `/switch/RetroArch/RetroArch.nro`. Le fichier doit être un vrai NRO
(en-tête `NRO0`) : sinon rien n'est installé. Deux entrées qui aboutiraient
au même dossier (casse ignorée, comme sur FAT) : la première est gardée.

## Section `bios`

| Champ | Type | Défaut si absent |
|---|---|---|
| `url` | chaîne, **obligatoire** (relative acceptée) | — |
| `file` | nom de fichier simple (ni dossier, ni `..`, ni fichier caché), installé dans le dossier système de RetroArch | dernier segment de l'URL |
| `system` | id de système | celui du catalogue si le fichier y est connu, sinon vide (« Autres ») |
| `md5` | 32 chiffres hexadécimaux : **le fichier téléchargé est refusé s'il ne correspond pas** | vide |
| `size` | entier ≥ 0, en octets | 0 |

Une entrée invalide (nom dangereux, MD5 mal formé, doublon…) est ignorée
avec un avertissement ; une section `bios` qui n'est pas un tableau aussi :
la boutique reste utilisable.

Catalogue des fichiers vérifiés (`src/models/Bios.cpp`, noms et MD5 de la
documentation libretro) :

| Système | Fichiers | Requis |
|---|---|---|
| Game Boy Advance | `gba_bios.bin` | non (gpSP en a besoin, mGBA a un BIOS intégré) |
| Nintendo DS | `bios7.bin`, `bios9.bin`, `firmware.bin` | non (melonDS ; DeSmuME s'en passe) |
| NES | `disksys.rom` | non (jeux Famicom Disk System) |
| PC Engine | `syscard3.pce` | non (jeux CD) |
| PlayStation | `scph5500.bin` (JP), `scph5501.bin` (US), `scph5502.bin` (EU) | `scph5501.bin` |

Un fichier présent dont le MD5 diffère de la référence est affiché
« version non reconnue » : autre révision ou dump douteux, il peut
fonctionner ou non selon le cœur. Un fichier que la boutique propose hors
catalogue (`neogeo.zip`…) apparaît aussi dans l'écran, sous son système.

## Gestion des erreurs

| Situation | Effet |
|---|---|
| JSON invalide, UTF-8 invalide, document vide | Rejet (`ParseError`, avec ligne et colonne) |
| Racine autre qu'un objet, aucune des sections `games` / `files` / `apps` / `emulators`, l'une d'elles pas un tableau | Rejet (`ParseError`) |
| `version` supérieure à 1 | Rejet (`Unsupported`) |
| Entrée invalide : pas d'URL, mauvais type, taille négative, CRC mal formé, URL relative sans base… | **Entrée ignorée** et avertissement : le reste de la boutique s'affiche |
| Deux entrées avec le même `id` (ou le même fichier final dans le même système) | La première est gardée, avertissement |

Les avertissements sont écrits dans les logs (`-d` sur desktop) sous la forme
`games[3] skipped: "size" must be a non-negative integer`.
