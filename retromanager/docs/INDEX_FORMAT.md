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
  ]
}
```

## Racine

| Champ | Type | Obligatoire | Rôle |
|---|---|---|---|
| `version` | entier ≥ 1 | non (défaut 1) | Version du format. Une version supérieure à celle supportée est refusée (`Unsupported`). |
| `name` | chaîne | non | Nom affiché de la boutique. |
| `success` | chaîne | non | Message d'accueil (même champ que Tinfoil). |
| `games` | tableau | `games` et/ou `files` | Entrées de jeux. |
| `files` | tableau | `games` et/ou `files` | Alias Tinfoil, même format d'entrée. Traité après `games`. |
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
| `boxart` | chaîne (URL) | vide |
| `crc32` | 8 chiffres hexadécimaux, **vérifié après téléchargement** (fichier rejeté si différent) | vide |
| `year` | entier entre 1950 et 2100 | absent |
| `description` | chaîne | vide |
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

### Identifiants de système

`nes`, `snes`, `n64`, `gb`, `gbc`, `gba`, `nds`, `mastersystem`, `megadrive`,
`gamegear`, `pcengine`, `psx`, `arcade`. Insensibles à la casse. Un id inconnu
est accepté (affiché tel quel, en fin de liste). Les extensions ambiguës
(`.zip`, `.7z`, `.iso`, `.chd`…) ne permettent pas de déduire le système :
renseignez `system`. Liste de référence : `src/models/Systems.cpp`.

## Gestion des erreurs

| Situation | Effet |
|---|---|
| JSON invalide, UTF-8 invalide, document vide | Rejet (`ParseError`, avec ligne et colonne) |
| Racine autre qu'un objet, ni `games` ni `files`, `games` non tableau | Rejet (`ParseError`) |
| `version` supérieure à 1 | Rejet (`Unsupported`) |
| Entrée invalide : pas d'URL, mauvais type, taille négative, CRC mal formé, URL relative sans base… | **Entrée ignorée** et avertissement : le reste de la boutique s'affiche |
| Deux entrées avec le même `id` (ou le même fichier final dans le même système) | La première est gardée, avertissement |

Les avertissements sont écrits dans les logs (`-d` sur desktop) sous la forme
`games[3] skipped: "size" must be a non-negative integer`.
