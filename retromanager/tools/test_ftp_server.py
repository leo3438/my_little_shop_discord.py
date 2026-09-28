#!/usr/bin/env python3
"""Local FTP server for the FtpClient integration tests (requires pyftpdlib).

Serves tests/fixtures/ftp_root read-only with user "retro" / password
"manager", on 127.0.0.1 and a free port. Prints "READY <port>" once
listening, so scripts can wait for it:

    python3 tools/test_ftp_server.py --port-file /tmp/ftp.port &
    RM_TEST_FTP_PORT=$(cat /tmp/ftp.port) ctest --preset tests
"""

import argparse
import logging
import sys
from pathlib import Path

try:
    from pyftpdlib.authorizers import DummyAuthorizer
    from pyftpdlib.handlers import FTPHandler
    from pyftpdlib.servers import FTPServer
except ImportError:
    sys.exit("pyftpdlib is required: pip install pyftpdlib")

ROOT = Path(__file__).resolve().parent.parent / "tests" / "fixtures" / "ftp_root"
USER, PASSWORD = "retro", "manager"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=0, help="0 = pick a free port")
    parser.add_argument("--port-file", type=Path, help="write the listening port to this file")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING)

    authorizer = DummyAuthorizer()
    authorizer.add_user(USER, PASSWORD, str(ROOT), perm="elr")  # read-only
    handler = FTPHandler
    handler.authorizer = authorizer
    handler.passive_ports = range(60000, 60100)

    server = FTPServer(("127.0.0.1", args.port), handler)
    port = server.address[1]
    if args.port_file:
        args.port_file.write_text(str(port))
    print(f"READY {port}", flush=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
