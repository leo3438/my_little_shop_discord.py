#!/bin/sh
# Builds the RetroManager forwarder stub (ExeFS "main" + "main.npdm") from
# switchbrew's nx-hbloader (ISC licence), patched to launch
# romfs:/nextNroPath with romfs:/nextArgv. Needs devkitA64 + libnx
# (the devkitpro/devkita64 image).
#
#   tools/forwarder-stub/build.sh <output dir>
#
# Output: <output dir>/stub/{main,main.npdm,LICENSE-nx-hbloader.md,README.txt}
set -eu

NX_HBLOADER_COMMIT=82b95122c5ae8dc059bf23893ba7623c72c86773  # v2.4.5
here=$(cd "$(dirname "$0")" && pwd)
out=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

git clone --quiet https://github.com/switchbrew/nx-hbloader.git "$work/hbl"
git -C "$work/hbl" checkout --quiet "$NX_HBLOADER_COMMIT"
git -C "$work/hbl" apply "$here/nx-hbloader.patch"
cp "$here/source/romfs_lookup.c" "$here/source/romfs_lookup.h" "$work/hbl/source/"

# NPDM: an application (hbloader itself is declared as a library applet).
# RetroManager rewrites the program id of each forwarder; the range below
# only matters for the stub as shipped.
json="$work/hbl/hbl.json"
sed -i \
    -e 's/"name": "hbloader"/"name": "forwarder"/' \
    -e 's/"title_id": "0x010000000000100D"/"title_id": "0x0100000000001000"/' \
    -e 's/"title_id_range_min": "0x010000000000100D"/"title_id_range_min": "0x0100000000000000"/' \
    -e 's/"title_id_range_max": "0x010000000000100D"/"title_id_range_max": "0x01FFFFFFFFFFFFFF"/' \
    "$json"
# application_type 2 (applet) -> 1 (application): the value is on the line
# after the "application_type" key.
sed -i '/"type": "application_type"/{n;s/"value": 2/"value": 1/}' "$json"
grep -q '"title_id_range_max": "0x01FFFFFFFFFFFFFF"' "$json"
grep -A1 '"type": "application_type"' "$json" | grep -q '"value": 1'

make -C "$work/hbl" -j"$(nproc)"

mkdir -p "$out/stub"
cp "$work/hbl/hbl.nso" "$out/stub/main"
cp "$work/hbl/hbl.npdm" "$out/stub/main.npdm"
cp "$work/hbl/LICENSE.md" "$out/stub/LICENSE-nx-hbloader.md"
cat > "$out/stub/README.txt" <<TXT
RetroManager forwarder stub
Built from switchbrew/nx-hbloader $NX_HBLOADER_COMMIT (ISC licence, see
LICENSE-nx-hbloader.md) with retromanager/tools/forwarder-stub/nx-hbloader.patch.

Copy main and main.npdm to /switch/RetroManager/stub/ on the SD card.
On launch it runs romfs:/nextNroPath with romfs:/nextArgv (written by
RetroManager into each forwarder NSP), then returns to HOME.
TXT
ls -l "$out/stub"
