# Portage PKHeX (C#) -> C++ : crypto et validation PK8/PK9

## 1. Découpage

| Couche | Référence PKHeX.Core | Module PKForgeNX | Dépend de libnx |
|---|---|---|---|
| Crypto PKM (LCRNG + shuffle) | `PKM/Util/PokeCrypto.cs` | `crypto/PokeCrypto` | non |
| Structures PK8/PK9 | `PKM/PK8.cs`, `PKM/PK9.cs`, `G8PKM.cs` | `pkm/PK8`, `pkm/PK9` | non |
| Crypto save (SwishCrypto) | `Saves/Encryption/SwishCrypto/*` | `crypto/SwishCrypto`, `crypto/SCBlock` | non (SHA-256 injecté) |
| Accès blocs save | `Saves/Access/SaveBlockAccessor8SWSH.cs`, `...9SV.cs` | `core/BlockAccessor` | non |
| Boîtes / party | `Saves/Substructures/Gen8,Gen9/*` | `core/BoxStorage` | non |
| Tables (stats de base, capacités) | `Resources/byte/personal/*` | `romfs:/data/*.bin` + `pkm/PersonalTable` | non |
| E/S, backup, commit | - | `core/SaveSession`, `core/BackupManager` | oui |

Seul `core/SaveSession` touche aux services fs ; tout le reste reçoit un `std::span<uint8_t>`.

## 2. Crypto PKM (PK8/PK9, identique)

- Tailles : 0x148 (boîte, `SIZE_STORED`) / 0x158 (équipe, `SIZE_PARTY`). En-tête 8 octets :
  `EncryptionConstant` (u32 @0x00), `Sanity` (u16 @0x04), `Checksum` (u16 @0x06), puis 4 blocs de 0x50.
- Déchiffrement : XOR u16 à partir de 0x08 jusqu'à la fin du buffer avec un LCRNG
  `seed = seed * 0x41C64E6D + 0x6073`, clé = `seed >> 16`, seed initial = EC ;
  puis dé-mélange des blocs selon `sv = (EC >> 13) & 31` et la table `BlockPosition`.
- Chiffrement : mélange avec `BlockPositionInvert[sv]`, puis même XOR.
- Checksum : somme 16 bits des u16 de 0x08 à 0x148 exclus.
- Tables `BlockPosition` / `BlockPositionInvert` recopiées en `constexpr std::array` avec un `static_assert`
  sur leur taille.

## 3. Crypto save (SwishCrypto, SwSh et SV)

- Le fichier `main` = payload + SHA-256 (0x20 octets) en fin de fichier.
- Validation : `SHA256(IntroHashBytes || payload || OutroHashBytes) == hash final` (constantes statiques de
  `SwishCrypto.cs`, copiées octet par octet).
- Déchiffrement du payload : XOR avec le pad statique `StaticXorpad` (appliqué cycliquement).
- Découpage en `SCBlock` : `u32 key`, puis type (et sous-type, longueur pour les tableaux/objets) chiffrés par
  un keystream `SCXorShift32` initialisé depuis `key`. Types : Bool1/2/3, Object, Array, valeurs scalaires.
- Les blocs sont triés par clé : on garde un `std::vector<SCBlock>` trié et une recherche binaire (comme PKHeX),
  les données de chaque bloc étant des `std::span` sur un unique buffer (pas de copie par bloc).
- Écriture : sérialisation dans le même ordre -> XOR pad -> recalcul du hash -> ajout en fin.
- Les clés de blocs (boîtes, équipe, trainer, Pokédex...) viennent de `SaveBlockAccessor9SV.cs` /
  `SaveBlockAccessor8SWSH.cs` et sont regroupées dans un header `constexpr` par jeu.

## 4. Règles de traduction C# -> C++

| C# | C++20 |
|---|---|
| `Span<byte>`, `ReadOnlySpan<byte>` | `std::span<uint8_t>`, `std::span<const uint8_t>` |
| `BinaryPrimitives.ReadUInt16LittleEndian` | `readLE<uint16_t>(span, off)` via `std::memcpy` (pas de cast de pointeur : alignement + strict aliasing) |
| `static ReadOnlySpan<byte> X => [..]` | `inline constexpr std::array<uint8_t, N>` |
| `byte[]` retourné / alloué | buffer possédé par l'appelant, jamais d'allocation par Pokémon dans les boucles de boîtes |
| exceptions de validation | `std::expected`-like (`Result<T, Error>`) : pas d'exception dans le chemin crypto |
| `SHA256.HashData` | interface `Sha256Fn` : `sha256ContextUpdate` libnx (ARMv8 Crypto) sur Switch, impl portable sur PC |
| `Dictionary<uint, T>` | vecteur trié + `std::lower_bound` |
| propriétés (`PK9.Species`) | accesseurs inline sur un offset constant, la struct reste une vue sur 0x158 octets |

Switch est little-endian comme les formats de Game Freak : aucune conversion, mais les accès restent passés par
`readLE/writeLE` pour que le code soit portable et testable sur n'importe quel hôte.

## 5. Validation et non-régression

1. Tests natifs PC (`tests/host`, CMake + Catch2) compilant `source/crypto` et `source/pkm` sans libnx.
2. Fixtures exportées depuis PKHeX : `.pk9` chiffrés/déchiffrés, `main` de SV/SwSh.
3. Tests aller-retour obligatoires : `decrypt(encrypt(x)) == x` et `encrypt(decrypt(main)) == main` à l'octet près,
   hash final inclus. Un seul octet différent bloque toute écriture.
4. Test différentiel : petit outil console C# référençant PKHeX.Core qui dumpe en JSON espèce/IV/EV/nature/checksum
   de chaque slot ; comparaison avec la sortie de notre parseur.
5. Garde-fous à l'exécution sur Switch, dans cet ordre, avant toute écriture :
   backup vérifié SHA-256 (déjà implémenté) -> re-chiffrement complet en mémoire -> re-parse de ce buffer et
   comparaison des blocs non modifiés -> écriture -> `fsdevCommitDevice` -> relecture et comparaison du hash.
6. Légalité : périmètre initial limité (bornes IV 0-31, EV <= 252 et total <= 510, espèce/forme présentes dans
   la PersonalTable du jeu, checksum, shiny/PID cohérent). La légalité complète de PKHeX (encounters, ribbons,
   moves) est hors périmètre des premières versions.

## 6. Contraintes Switch spécifiques

- Une save SV fait plusieurs Mo : elle est chargée en un seul buffer sur le tas (pas de copies intermédiaires
  pendant le (dé)chiffrement, tout se fait en place).
- L'écriture passe par un montage `fsdevMountSaveData` (lecture/écriture) distinct du montage lecture seule
  utilisé pour l'inspection, sous `appletLockExit()`, puis `fsdevCommitDevice()`. Sans commit, rien n'est persisté.
- Le journal de la save a une taille fixe : on réécrit le fichier `main` en place avec la même taille
  (`r+b`), sans suppression/recréation.
