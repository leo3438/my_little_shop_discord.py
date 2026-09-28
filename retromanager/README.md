# RetroManager

Gestionnaire d'émulation rétro pour Nintendo Switch, « un Tinfoil pour le
rétro ». C++17, libnx, [Borealis](https://github.com/xfangfang/borealis).

![Boutique (build desktop)](docs/shop-desktop.png)
![Téléchargement (build desktop)](docs/download-desktop.png)

L'organisation du code est décrite dans [ARCHITECTURE.md](ARCHITECTURE.md).

## Prérequis

- CMake ≥ 3.22, Ninja, un compilateur C++17 (GCC ≥ 9, Clang ≥ 10).
- Build desktop : en-têtes OpenGL et X11/Wayland. Sous Debian/Ubuntu :
  `sudo apt install libgl-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxkbcommon-dev libwayland-dev libdbus-1-dev pkg-config`
- libcurl (client FTP) : `sudo apt install libcurl4-openssl-dev` conseillé. À
  défaut, CMake compile une libcurl minimale (FTP/HTTP, TLS via OpenSSL si
  présent). `-DRM_WITH_CURL=OFF` désactive le client FTP.
- Build Switch : [devkitPro](https://devkitpro.org/wiki/Getting_Started) avec
  `switch-dev switch-glfw switch-mesa switch-glm switch-curl`, et `DEVKITPRO` défini (ou
  le conteneur Docker `devkitpro/devkita64`).

Borealis, GoogleTest et nlohmann/json (et libcurl si besoin) sont récupérés automatiquement par
CMake (FetchContent, versions épinglées).

## Commandes

Toutes les commandes se lancent depuis `retromanager/`.

```sh
# Tests unitaires seuls : rapide, sans Borealis ni dépendances GPU
cmake --preset tests && cmake --build --preset tests && ctest --preset tests

# App desktop + tests, sur une fausse carte SD dont config.json pointe vers
# ftp://127.0.0.1:2121 (serveur de test, limité à 8 Mo/s pour voir la progression)
python3 tools/make_mock_sd.py          # crée ./sdmc à partir de tests/fixtures/sd_card
python3 tools/test_ftp_server.py --port 2121 --throttle-kbps 8192 &
cmake --preset desktop && cmake --build --preset desktop
./build/desktop/RetroManager           # -d : logs debug, -v : vue de debug

# Autre fausse SD
RETROMANAGER_SD_ROOT=/chemin/vers/sd ./build/desktop/RetroManager

# Boutique de démonstration sans réseau : "type": "mock" dans sdmc/switch/RetroManager/config.json
# Écrans de chargement et d'erreur de la boutique (source mock)
RETROMANAGER_MOCK_LATENCY_MS=3000 ./build/desktop/RetroManager
RETROMANAGER_MOCK_ERROR=network ./build/desktop/RetroManager   # network|auth|notfound|format
RETROMANAGER_MOCK_SPEED_KBPS=2048 ./build/desktop/RetroManager  # vitesse des téléchargements

# Tests d'intégration FTP + FTPS contre de vrais serveurs locaux
# (pip install pyftpdlib pyopenssl ; openssl en ligne de commande)
python3 tools/test_ftp_server.py --port-file /tmp/ftp.port --ftps-port-file /tmp/ftps.port &
RM_TEST_FTP_PORT=$(cat /tmp/ftp.port) RM_TEST_FTPS_PORT=$(cat /tmp/ftps.port) ctest --preset tests

# Switch
cmake --preset switch && cmake --build --preset switch
# → build/switch/RetroManager.nro, à copier dans sdmc:/switch/
```

La source de la boutique se règle dans `sdmc:/switch/RetroManager/config.json`
([docs/CONFIG.md](docs/CONFIG.md)) ; le format des index est décrit dans
[docs/INDEX_FORMAT.md](docs/INDEX_FORMAT.md). Les ROMs sont installées dans
`/roms/<système>/`.

Sur desktop, `sdmc/` joue le rôle de la racine `sdmc:/` de la console : l'app
y lit et y écrit exactement comme elle le ferait sur la carte SD.
