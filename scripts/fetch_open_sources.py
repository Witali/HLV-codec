#!/usr/bin/env python3
"""Download openly licensed movie sources for the extended HLV test suite.

The files are intentionally not bundled because they are large; this script
records explicit public source URLs so benchmark inputs remain reproducible.
"""

from __future__ import annotations

import argparse
import shutil
import urllib.request
from pathlib import Path

SOURCES = {
    "big_buck_bunny": {
        "url": "https://upload.wikimedia.org/wikipedia/commons/4/41/Big_Buck_Bunny_medium.ogv",
        "filename": "Big_Buck_Bunny_medium.ogv",
        "license": "CC BY 3.0; (c) Blender Foundation | www.bigbuckbunny.org",
    },
    "charge": {
        "url": "https://upload.wikimedia.org/wikipedia/commons/7/7a/Charge_-_Blender_Open_Movie-full_movie.webm",
        "filename": "Charge_-_Blender_Open_Movie-full_movie.webm",
        "license": "CC BY 4.0; Blender Studio",
    },
}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", type=Path, default=Path("bench/external"))
    ap.add_argument("--only", choices=sorted(SOURCES), action="append")
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    names = args.only or list(SOURCES)
    for name in names:
        item = SOURCES[name]
        target = args.output / item["filename"]
        if target.exists() and target.stat().st_size:
            print(f"Already present: {target}")
            continue
        print(f"Downloading {name}: {item['license']}")
        with urllib.request.urlopen(item["url"]) as src, target.open("wb") as dst:
            shutil.copyfileobj(src, dst, length=1024 * 1024)
        print(f"Saved {target} ({target.stat().st_size} bytes)")


if __name__ == "__main__":
    main()