#!/usr/bin/env bash
# Reflash the connected board with the latest GB app (build/proteus_gb.uf2).
# The per-board engraved name lives in its own flash sector and is NOT touched.
# Tries to reboot a running board into BOOTSEL; if it is hung/unreachable, plug it
# in holding BOOTSEL and re-run.
set -e
cd "$(dirname "$0")/.."

PT=(docker run --rm --privileged -v /dev/bus/usb:/dev/bus/usb -v "$PWD":/work -w /work proteus-picotool picotool)

if lsusb | grep -q 2e8a:000f; then
    echo "board already in BOOTSEL"
else
    echo "rebooting board to BOOTSEL..."
    "${PT[@]}" reboot -f -u >/dev/null 2>&1 || true
    for i in $(seq 1 12); do lsusb | grep -q 2e8a:000f && break; sleep 1; done
fi

if ! lsusb | grep -q 2e8a:000f; then
    echo "NOT in BOOTSEL — unplug, hold BOOTSEL, replug, then re-run this script."
    exit 1
fi

"${PT[@]}" load -x build/proteus_gb.uf2 2>&1 | tail -1
echo "done — GB (250 MHz) flashed; engraved name preserved."
