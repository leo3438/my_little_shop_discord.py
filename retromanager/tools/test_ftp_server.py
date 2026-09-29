#!/usr/bin/env python3
"""Local FTP (and HTTP) servers for the integration tests (requires pyftpdlib).

Serves a temporary copy of tests/fixtures/ftp_root, read-only, with user
"retro" / password "manager", on 127.0.0.1, plus a writable /saves folder
for the cloud-saves tests. On top of the fixture it
generates a large deterministic ROM ("Big Test ROM", --big-mb MiB) and adds
it, with its size and CRC32, to shop/index.json: this is what the streaming
and memory tests download.

With --ftps-port-file, a second server speaking explicit FTPS (AUTH TLS)
with a freshly generated self-signed certificate runs alongside (requires
pyOpenSSL and the openssl CLI).

    python3 tools/test_ftp_server.py --port-file /tmp/ftp.port --ftps-port-file /tmp/ftps.port &
    RM_TEST_FTP_PORT=$(cat /tmp/ftp.port) RM_TEST_FTPS_PORT=$(cat /tmp/ftps.port) ctest --preset tests

With --http-port-file (or --http-port), the same tree is also served over
HTTP (HttpClient tests):
  - Range requests ("bytes=N-") answered with 206; under /norange/ the Range
    header is ignored (200 + whole file), like a server without resume;
  - /private/<path>: Basic auth retro/manager, else 401;
  - /status/<code>: that status; /redirect/<path>: 302 to /<path>;
  - /thumbnails/: a fake thumbnails.libretro.com (Named_Boxarts PNGs).

For the desktop app (config.json of the mock SD points to port 2121 for FTP
and 8121 for HTTP), with a slow link so the progress bar can be watched:

    python3 tools/test_ftp_server.py --port 2121 --http-port 8121 --throttle-kbps 8192
"""

import argparse
import base64
import json
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import logging
import random
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

try:
    from pyftpdlib.authorizers import DummyAuthorizer
    from pyftpdlib.handlers import FTPHandler
    from pyftpdlib.servers import FTPServer
except ImportError:
    sys.exit("pyftpdlib is required: pip install pyftpdlib")

FIXTURE = Path(__file__).resolve().parent.parent / "tests" / "fixtures" / "ftp_root"
USER, PASSWORD = "retro", "manager"
BIG_ROM_PATH = "shop/roms/gba/Big Test ROM (Synthetic).gba"


def build_root(big_mb: int) -> Path:
    root = Path(tempfile.mkdtemp(prefix="retromanager-ftp-"))
    shutil.copytree(FIXTURE, root, dirs_exist_ok=True)

    if big_mb > 0:
        big = root / BIG_ROM_PATH
        big.parent.mkdir(parents=True, exist_ok=True)
        rng = random.Random(42)  # deterministic content
        crc = 0
        with big.open("wb") as out:
            for _ in range(big_mb):
                block = rng.randbytes(1024 * 1024)
                crc = zlib.crc32(block, crc)
                out.write(block)
        index_path = root / "shop" / "index.json"
        index = json.loads(index_path.read_text(encoding="utf-8"))
        index["games"].append({
            "title": "Big Test ROM",
            "system": "gba",
            "size": big_mb * 1024 * 1024,
            "crc32": f"{crc & 0xFFFFFFFF:08x}",
            "url": "roms/gba/Big%20Test%20ROM%20(Synthetic).gba",
        })
        index_path.write_text(json.dumps(index, ensure_ascii=False, indent=2), encoding="utf-8")
    return root


def make_handler(root: Path, tls: bool, throttle_kbps: int = 0):
    authorizer = DummyAuthorizer()
    authorizer.add_user(USER, PASSWORD, str(root), perm="elr")  # the shop is read-only...
    saves = root / "saves"
    saves.mkdir(exist_ok=True)
    # ...but the cloud-saves area accepts uploads, renames and new folders.
    authorizer.override_perm(USER, str(saves), perm="elradfmwMT", recursive=True)
    if tls:
        from pyftpdlib.handlers import TLS_FTPHandler

        cert = root.parent / (root.name + ".pem")
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
             "-subj", "/CN=retromanager-test-nas", "-keyout", str(cert), "-out", str(cert)],
            check=True, capture_output=True)

        class Handler(TLS_FTPHandler):
            pass

        Handler.certfile = str(cert)
        Handler.tls_control_required = True
        Handler.tls_data_required = True
    else:

        class Handler(FTPHandler):
            pass

    Handler.authorizer = authorizer
    Handler.passive_ports = range(60000, 60200)
    if throttle_kbps > 0:  # slow link, to watch the progress bar in the desktop app
        from pyftpdlib.handlers import ThrottledDTPHandler

        class SlowDTP(ThrottledDTPHandler):
            read_limit = throttle_kbps * 1024
            write_limit = throttle_kbps * 1024

        Handler.dtp_handler = SlowDTP
    return Handler


def make_http_handler(root: Path, throttle_kbps: int = 0):
    expected_auth = "Basic " + base64.b64encode(f"{USER}:{PASSWORD}".encode()).decode()

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *args):  # quiet unless --verbose
            logging.info("http: " + fmt, *args)

        def send_plain(self, code, text=""):
            body = text.encode()
            self.send_response(code)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)

        def do_HEAD(self):
            self.do_GET()

        def do_GET(self):
            path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
            honour_range = True
            if path.startswith("/status/"):
                return self.send_plain(int(path.split("/")[2]), "status page")
            if path.startswith("/redirect/"):
                self.send_response(302)
                self.send_header("Location", urllib.parse.quote(path[len("/redirect"):]))
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if path.startswith("/norange/"):
                honour_range = False
                path = path[len("/norange"):]
            if path.startswith("/private/"):
                if self.headers.get("Authorization") != expected_auth:
                    self.send_response(401)
                    self.send_header("WWW-Authenticate", 'Basic realm="nas"')
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                path = path[len("/private"):]

            target = (root / path.lstrip("/")).resolve()
            if root.resolve() not in target.parents or not target.is_file():
                return self.send_plain(404, "<html>404 Not Found</html>")
            size = target.stat().st_size
            start = 0
            ranged = self.headers.get("Range")
            if ranged and honour_range:
                spec = ranged.strip()
                if not spec.startswith("bytes=") or not spec[6:].endswith("-"):
                    return self.send_plain(400, "only bytes=N- is supported")
                start = int(spec[6:-1])
                if start >= size:
                    self.send_response(416)
                    self.send_header("Content-Range", f"bytes */{size}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                self.send_response(206)
                self.send_header("Content-Range", f"bytes {start}-{size - 1}/{size}")
            else:
                self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(size - start))
            self.send_header("Accept-Ranges", "bytes" if honour_range else "none")
            self.end_headers()
            if self.command == "HEAD":
                return
            with target.open("rb") as f:
                f.seek(start)
                chunk = 64 * 1024
                while True:
                    block = f.read(chunk)
                    if not block:
                        break
                    try:
                        self.wfile.write(block)
                    except (BrokenPipeError, ConnectionResetError):
                        return
                    if throttle_kbps > 0:
                        time.sleep(len(block) / (throttle_kbps * 1024))

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=0, help="plain FTP port (0 = pick a free one)")
    parser.add_argument("--port-file", type=Path, help="write the plain FTP port to this file")
    parser.add_argument("--ftps-port-file", type=Path, help="also start an FTPS server and write its port here")
    parser.add_argument("--http-port", type=int, default=-1, help="also serve the tree over HTTP on this port (0 = free one)")
    parser.add_argument("--http-port-file", type=Path, help="also serve over HTTP and write its port here")
    parser.add_argument("--big-mb", type=int, default=64, help="size of the generated Big Test ROM (0 = none)")
    parser.add_argument("--throttle-kbps", type=int, default=0, help="limit plain FTP transfers (0 = unlimited)")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING)
    root = build_root(args.big_mb)

    # pyftpdlib servers share one IOLoop: create both, then run a single loop.
    tls_server = FTPServer(("127.0.0.1", 0), make_handler(root, tls=True)) if args.ftps_port_file else None
    server = FTPServer(("127.0.0.1", args.port), make_handler(root, tls=False, throttle_kbps=args.throttle_kbps))
    port = server.address[1]
    if args.http_port >= 0 or args.http_port_file:
        http = ThreadingHTTPServer(("127.0.0.1", max(args.http_port, 0)),
                                   make_http_handler(root, throttle_kbps=args.throttle_kbps))
        http.daemon_threads = True
        threading.Thread(target=http.serve_forever, daemon=True).start()
        if args.http_port_file:
            args.http_port_file.write_text(str(http.server_address[1]))
        print(f"HTTP READY {http.server_address[1]}", flush=True)
    if tls_server is not None:
        args.ftps_port_file.write_text(str(tls_server.address[1]))
        print(f"FTPS READY {tls_server.address[1]}", flush=True)
    if args.port_file:  # written last: its presence means everything is up
        args.port_file.write_text(str(port))
    print(f"READY {port} (root {root})", flush=True)
    try:
        server.serve_forever()
    finally:
        shutil.rmtree(root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
