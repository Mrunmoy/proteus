#!/usr/bin/env python3
"""Write a per-board recipient name into the personalization flash sector.

The Game Boy gift firmware (src/app/gb) reads this block at boot and shows
"For <name>" on the splash. The block lives in its OWN flash sector (0x3FD000),
so it survives app reflashes and the per-boot diagnostics capsule rewrites — it
is written once per board and left alone.

Board must be in BOOTSEL mode. Usage:
    python3 tools/personalize.py "Adam"
    python3 tools/personalize.py "Adam" --ser <chip-uid>   # pick one of several
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "mfg"))
import personalization as pers   # noqa: E402  (shared block builder, same as the station)


def main():
    ap = argparse.ArgumentParser(description="Personalize a Proteus GB board (BOOTSEL).")
    ap.add_argument("name", help="recipient name to show on the splash")
    ap.add_argument("--ser", help="target one board by chip UID when several are in BOOTSEL")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    binname = ".personalize.bin"
    binpath = os.path.join(repo, binname)
    with open(binpath, "wb") as f:
        f.write(pers.build(args.name))

    picotool = [
        "docker", "run", "--rm", "--privileged",
        "-v", "/dev/bus/usb:/dev/bus/usb", "-v", f"{repo}:/work", "-w", "/work",
        "proteus-picotool", "picotool",
    ]
    sel = ["--ser", args.ser] if args.ser else []
    try:
        print(f"writing '{args.name}' -> {hex(pers.XIP_ADDR)}")
        subprocess.run(picotool + ["load", binname, "-o", hex(pers.XIP_ADDR)] + sel, check=True)
    finally:
        os.remove(binpath)
    print(f"done — '{args.name}' written. Reset the board; the splash shows \"For {args.name}\".")


if __name__ == "__main__":
    main()
