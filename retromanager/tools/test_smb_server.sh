#!/bin/sh
# Local Samba server for the SMB integration tests (needs root: it creates a
# system user and starts smbd). Samba keeps its defaults, notably
# "server min protocol = SMB2_02", exactly like a NAS (ZimaOS, Synology...).
#
#   sudo tools/test_smb_server.sh 4450 /tmp/rm-smb
#   RM_TEST_SMB_PORT=4450 ctest --preset tests
#
# Shares (user "retro" / password "manager"):
#   HDD-Storage1  read-only copy of tests/fixtures/ftp_root, plus big.bin
#                 (3 MiB, deterministic) for streaming / resume tests
#   Public        guest access, read-only
#   Saves         read-write, for uploads
set -eu
PORT="${1:-4450}"
WORK="${2:-$(mktemp -d /tmp/rm-smb.XXXXXX)}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"

rm -rf "$WORK"
mkdir -p "$WORK/state" "$WORK/shares/Public" "$WORK/shares/Saves" /run/samba
cp -r "$HERE/tests/fixtures/ftp_root" "$WORK/shares/HDD-Storage1"
python3 -c "import random,sys; r=random.Random(7); sys.stdout.buffer.write(r.randbytes(3*1024*1024))" \
    > "$WORK/shares/HDD-Storage1/big.bin"
printf 'public file\n' > "$WORK/shares/Public/hello.txt"
id retro >/dev/null 2>&1 || useradd -M -s /usr/sbin/nologin retro
chown -R retro "$WORK/shares/Saves"
chmod -R a+rX "$WORK/shares"
chmod 755 "$WORK"

cat > "$WORK/smb.conf" <<CONF
[global]
  workgroup = WORKGROUP
  server role = standalone server
  smb ports = $PORT
  bind interfaces only = yes
  interfaces = lo
  disable netbios = yes
  map to guest = Bad User
  private dir = $WORK/state
  lock directory = $WORK/state
  state directory = $WORK/state
  cache directory = $WORK/state
  pid directory = $WORK/state
  ncalrpc dir = $WORK/state/ncalrpc
  log file = $WORK/state/log.%m
[HDD-Storage1]
  path = $WORK/shares/HDD-Storage1
  read only = yes
  valid users = retro
[Public]
  path = $WORK/shares/Public
  guest ok = yes
  read only = yes
[Saves]
  path = $WORK/shares/Saves
  read only = no
  valid users = retro
CONF
printf 'manager\nmanager\n' | smbpasswd -c "$WORK/smb.conf" -s -a retro >/dev/null
smbd -D -s "$WORK/smb.conf"
for _ in $(seq 1 50); do
    if smbclient -p "$PORT" -U retro%manager //127.0.0.1/Public -c 'ls' >/dev/null 2>&1; then
        echo "SMB READY $PORT ($WORK)"
        exit 0
    fi
    sleep 0.2
done
echo "smbd did not start; see $WORK/state" >&2
exit 1
