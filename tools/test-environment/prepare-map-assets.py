#!/usr/bin/env python3
"""Prepare private map and bot-definition packs from user-supplied archives.

Original assets are used locally; do not commit or redistribute the map pack.
The botfiles ZIP is supplied directly to run.py, unchanged.
"""
import argparse
from pathlib import Path
import re
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--maps", type=Path, required=True)
    parser.add_argument("--botfiles", type=Path, required=True)
    parser.add_argument("--map", default="japanesecastles")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.map):
        parser.error("invalid map name")
    with zipfile.ZipFile(args.maps) as archive:
        data = {f"maps/{args.map}.{suffix}": archive.read(f"maps/{args.map}.{suffix}")
                for suffix in ("bsp", "aas")}
    with zipfile.ZipFile(args.botfiles) as archive:
        characters = sorted(name for name in archive.namelist()
                            if re.fullmatch(r"botfiles/bots/[A-Za-z0-9_-]+_c\.c", name))
    if not characters:
        parser.error("no original bot character files found")
    definitions = "\n".join('{\nname "' + Path(name).name[:-4] + '"\naifile "' +
                              name.removeprefix("botfiles/") + '"\nmodel "visor/default"\n}\n'
                              for name in characters)
    args.output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output / f"{args.map}.pk3", "w", zipfile.ZIP_DEFLATED) as archive:
        for name, content in data.items(): archive.writestr(name, content)
    with zipfile.ZipFile(args.output / "bot-definitions.pk3", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("scripts/bots.txt", definitions)
        archive.writestr("scripts/arenas.txt", '{ map "' + args.map + '" type "ctf" }\n')
    print("Prepared private packs in", args.output.resolve())


if __name__ == "__main__":
    main()
