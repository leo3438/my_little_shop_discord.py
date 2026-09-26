# PKForgeNX

Homebrew Nintendo Switch (.nro) pour lire, éditer et générer des Pokémon directement dans les sauvegardes
d'Épée/Bouclier (PK8) et d'Écarlate/Violet (PK9), en passant par les services `fs` de libnx.

## Build

```sh
git submodule update --init --recursive
sudo dkp-pacman -S switch-dev switch-sdl2 switch-mesa switch-libdrm_nouveau
make                  # -> PKForgeNX.nro
make PKF_DEBUG=1 run  # build debug + envoi nxlink (stdout sur le PC)
```

Sans toolchain installée : `docker run --rm -v "$PWD":/src -w /src devkitpro/devkita64 make`

Copier `PKForgeNX.nro` dans `sdmc:/switch/PKForgeNX/`. Les backups sont écrits dans
`sdmc:/switch/PKForgeNX/backups/<TitleID>/<date>_<uid>/main` (+ `main.sha256`).
Fermer le jeu avant de lancer l'app (une save ouverte par un jeu suspendu ne peut pas être montée).

## Arborescence

```
PKForgeNX/
├── Makefile                 # devkitA64 + libnx + SDL2 + GLES3 (Mesa) + ImGui
├── source/
│   ├── main.cpp             # init Horizon/SDL/ImGui, boucle, arrêt
│   ├── core/                # SaveSession, BackupManager, comptes, config (dépend de libnx)
│   ├── crypto/              # SwishCrypto, SCBlock, xorshift, LCRNG PKM (C++ pur, sans libnx)
│   ├── pkm/                 # PK8 / PK9, PersonalTable, checksums, légalité de base (C++ pur)
│   └── ui/                  # vues ImGui : boîtes PC, éditeur stats IV/EV, sprites
├── include/pkf/{core,crypto,pkm,ui}/   # headers publics, miroir de source/
├── lib/imgui/               # sous-module ocornut/imgui @ v1.91.9b
├── romfs/                   # embarqué dans le .nro, monté en romfs:/
│   ├── fonts/  sprites/pokemon/  sprites/items/  data/   # tables binaires générées
├── data/                    # petits blobs linkés dans l'ELF (bin2o)
├── assets/icon/icon.jpg     # icône 256x256 (optionnelle, sinon icône libnx)
├── tools/                   # scripts d'extraction des tables PKHeX -> romfs/data
├── tests/host/              # tests unitaires natifs (PC) du code crypto/pkm
├── tests/fixtures/          # .pk9 / saves de référence (non versionnés)
└── docs/PORTING_PKHEX.md    # plan de portage de la crypto/validation
```

`source/crypto` et `source/pkm` ne doivent jamais inclure `<switch.h>` : ils compilent aussi sur PC pour les tests.

## Licence

Le portage de la logique de PKHeX (GPLv3) fait de ce projet une œuvre dérivée : il doit être distribué sous GPLv3.
