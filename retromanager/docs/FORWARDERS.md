# Raccourcis HOME (forwarders)

Un *forwarder* est une petite application installée sur la console : son
icône apparaît sur l'écran d'accueil (HOME) et, une fois lancée, elle démarre
directement RetroArch avec le bon core et la bonne ROM.

![Raccourci généré (build desktop)](forwarder-desktop.png)

## Créer un raccourci

1. Installez le jeu depuis la boutique (tag **INSTALLÉ**).
2. Dans la liste, sélectionnez-le et appuyez sur **X** : « Créer un raccourci
   (Forwarder) ».
3. Au bout de quelques secondes :
   *« Raccourci généré dans /nsp/. Veuillez l'installer via DBI ou Tinfoil. »*
4. Installez `/nsp/<titre>.nsp` avec DBI ou Tinfoil (installation depuis la
   carte SD). L'icône apparaît sur l'accueil.

Regénérer le raccourci d'un même jeu produit le même *title id* : la
nouvelle installation remplace l'ancienne.

Le *title id* a la forme d'une application standard, `0100xxxxxxxx0000`
(8 chiffres tirés du chemin de la ROM) : c'est ce que le menu HOME affiche.
Les raccourcis générés avant ce correctif (`05…`) ne s'affichaient pas :
désinstallez-les (DBI) et regénérez-les.

## Ce qu'il faut sur la carte SD

| Fichier | Rôle | Où le trouver |
|---|---|---|
| `/switch/prod.keys` | Clés de **votre** console (`header_key`, `key_area_key_application_00`), pour chiffrer les NCA | Dump avec Lockpick_RCM |
| `/switch/RetroManager/stub/main` | Exécutable du forwarder (ExeFS) | Artefact CI **`stub`** (voir ci-dessous) |
| `/switch/RetroManager/stub/main.npdm` | Métadonnées de l'exécutable (son title id est remplacé) | Artefact CI **`stub`** |
| `/switch/RetroManager/stub/logo/` *(facultatif)* | `NintendoLogo.png`, `StartupMovie.gif` affichés au lancement | Livrés avec certains stubs |
| `/retroarch/cores/<core>_libretro_libnx.nro` | Le core RetroArch du système | Mise à jour en ligne de RetroArch |

Un stub rangé en `stub/exefs/main` et `stub/exefs/main.npdm` est aussi
accepté. RetroManager ne fournit pas les clés.

### Le stub (`main` + `main.npdm`)

La CI le compile à chaque build (job « Forwarder stub ») et le publie dans
l'artefact **`stub`** (`stub.zip`), à côté de `RetroManager-nro`. Dézippez-le
et copiez `main` et `main.npdm` dans `/switch/RetroManager/stub/`.

Il est construit depuis les sources de
[nx-hbloader](https://github.com/switchbrew/nx-hbloader) (licence ISC,
version figée), avec `tools/forwarder-stub/nx-hbloader.patch` :

- au lancement, il lit `romfs:/nextNroPath` et `romfs:/nextArgv` dans la
  RomFS du NSP (au lieu de lancer `hbmenu.nro`) ;
- quand RetroArch se ferme, il revient à l'accueil (sauf si RetroArch
  enchaîne un autre `.nro`, par exemple un changement de core) ;
- son NPDM déclare une *application* (hbloader seul se déclare applet).

Pour le recompiler soi-même (devkitA64 + libnx) :
`tools/forwarder-stub/build.sh <dossier de sortie>`.

Si un fichier manque, une fenêtre bloquante le dit précisément (le détail
reste affiché sur l'écran après « Fermer »), par exemple :

![Erreur : core absent (build desktop)](forwarder-error-desktop.png)

## Où est le NSP ? Que s'est-il passé ?

- Le fichier est écrit dans **`sdmc:/nsp/<titre du jeu>.nsp`** (dossier `nsp`
  à la racine de la carte, créé s'il n'existe pas). Le chemin exact et la
  taille sont affichés à l'écran et dans le journal. Après l'écriture,
  RetroManager force l'écriture sur la carte (`fsdevCommitDevice`) puis la
  relit : un fichier absent ou tronqué est une erreur.
- Le **nom du fichier est en ASCII** pour que DBI, Tinfoil et le système le
  voient : accents retirés, ponctuation remplacée par des espaces
  (« Pokémon Black Version » → `Pokemon Black Version.nsp`, « Tom & Jerry »
  → `Tom Jerry.nsp`). Le titre affiché sur l'accueil garde ses accents. Un
  titre sans aucune lettre latine prend le nom de la ROM, sinon
  `Forwarder <title id>.nsp`.
- Chaque essai est écrit dans **`sdmc:/switch/RetroManager/logs/retromanager.log`** :
  - `Forwarder: building <jeu> (...)` au début ;
  - `Forwarder: <jeu> (<title id>) written to sdmc:/nsp/<jeu>.nsp (<taille>)` en cas de succès ;
  - `Forwarder for <jeu> not created: <raison>` en cas d'échec ;
  - `Forwarder for <jeu>: cancelled` si l'écran a été quitté (B) avant la fin.
- Le journal est remplacé à chaque lancement : celui du lancement précédent
  est gardé dans `retromanager.old.log`. Lisez-le sur PC (lecteur de carte,
  FTP ou MTP avec DBI) ou avec l'éditeur de texte de NX-Shell.

Les clés ne sont jamais recopiées, envoyées ni écrites dans les journaux.

## Ce que contient le NSP

- **Icône** : la jaquette du jeu installée pour RetroArch
  (`/retroarch/thumbnails/<système>/Named_Boxarts/<rom>.png`), mise à
  l'échelle sans être recadrée dans un carré 256x256 (bandes de la couleur du
  bord), en JPEG. Sans jaquette : une icône unie.
- **Titre** : le nom du jeu (toutes les langues) ; éditeur « RetroArch -
  <système> ».
- **Chaîne de l'icône** : le CNMT (type *Application*, `0x80`) référence la
  NCA *Control*, qui contient `control.nacp` et un `icon_<Langue>.dat` par
  langue (JPEG 256x256). Le NACP n'a pas de champ « type » : c'est le CNMT
  et le type de contenu des NCA qui font de l'ensemble une application.
- **Lancement** : `romfs:/nextNroPath` = le core, `romfs:/nextArgv` =
  `"sdmc:/retroarch/cores/<core>.nro" "sdmc:/roms/<système>/<rom>"`.
- **Core** : le premier installé parmi, par exemple, `mgba`, `vba_next`,
  `gpsp` (GBA), `snes9x`, `snes9x2010` (SNES), `mupen64plus_next` (N64),
  `genesis_plus_gx` (Sega), `pcsx_rearmed` (PlayStation), `desmume`,
  `melonds` (DS).

## Limites

- **Sigpatches obligatoires** : les NCA et le NPDM ne peuvent pas porter les
  signatures de Nintendo. Sans signature patches, l'installation ou le
  lancement est refusé.
- Le raccourci pointe vers les chemins de la carte : déplacer la ROM ou
  désinstaller le core le casse (regénérez-le).
- Génération de clés 0 : le NSP s'installe sur tous les firmwares.
