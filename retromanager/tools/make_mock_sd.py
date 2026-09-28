#!/usr/bin/env python3
"""(Re)creates the desktop mock SD card from tests/fixtures/sd_card.

The desktop build of RetroManager reads and writes this folder as if it were
the console's sdmc:/. The fixture itself is never modified: it is copied, so
the app can freely mutate its copy.

    python3 tools/make_mock_sd.py            # -> retromanager/sdmc
    python3 tools/make_mock_sd.py /tmp/sd    # custom destination
    RETROMANAGER_SD_ROOT=/tmp/sd ./build/desktop/RetroManager
"""

import argparse
import shutil
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
FIXTURE = PROJECT_ROOT / "tests" / "fixtures" / "sd_card"
DEFAULT_DEST = PROJECT_ROOT / "sdmc"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dest", nargs="?", type=Path, default=DEFAULT_DEST, help="destination folder")
    parser.add_argument("--force", action="store_true", help="replace the destination without asking")
    args = parser.parse_args()

    dest: Path = args.dest.resolve()
    if dest == FIXTURE.resolve() or FIXTURE.resolve() in dest.parents:
        print("refusing to write inside the fixture", file=sys.stderr)
        return 1

    if dest.exists():
        if not args.force:
            answer = input(f"{dest} exists and will be replaced. Continue? [y/N] ")
            if answer.strip().lower() != "y":
                return 1
        shutil.rmtree(dest)

    shutil.copytree(FIXTURE, dest, ignore=shutil.ignore_patterns(".gitkeep"))
    files = sum(1 for p in dest.rglob("*") if p.is_file())
    print(f"mock SD card ready: {dest} ({files} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
