#!/usr/bin/env python3
"""Proteus build orchestrator — EVERYTHING runs in Docker, nothing on the host.

Targets (combine freely, e.g. ./build.py --firmware --host-tests):

  --images         Build all Docker images (toolchain + mfg + picotool). Do once.
  --firmware       rp2350 .uf2 images: proteus_gb, proteus_mfg + bring-up apps → ./build/
  --host-tests     C++ gtest suite: portable logic + drivers (PORT=host, fast)
  --asan           The same suite under AddressSanitizer + UBSan (correctness pass)
  --mfg-tools      Build the manufacturing-station image (PySide6 GUI + tooling) → proteus-mfg
  --mfg-tests      The station GUI/controller test suite
  --picotool       The flashing-tool image → proteus-picotool
  --run-mfg        Launch the manufacturing-station GUI on your desktop (X11)
  --format         Format the code: clang-format (C/C++) + black (Python)
  --all            --images + --firmware + --host-tests + --mfg-tests
  --clean          Remove ./build, ./build-host, ./build-asan and __pycache__ (NOT images/DB)

No host deps beyond Python 3 + Docker — this just orchestrates `docker compose`.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.abspath(__file__))
C, G, R, RST = "\033[36m", "\033[32m", "\033[31m", "\033[0m"


def _run(cmd, shell=False, check=True):
    print(f"\n{C}$ {cmd if shell else ' '.join(cmd)}{RST}", flush=True)
    subprocess.run(cmd, cwd=REPO, shell=shell, check=check)


def _dc(*args):
    _run(["docker", "compose", *args])


def _ctest(build_dir, sanitize):
    san = " -DPROTEUS_SANITIZE=ON" if sanitize else ""
    _dc("run", "--rm", "build", "bash", "-lc",
        f"cmake -S . -B {build_dir} -DPORT=host{san} && "
        f"cmake --build {build_dir} -j$(nproc) && "
        f"ctest --test-dir {build_dir} --output-on-failure")


# ── targets ──────────────────────────────────────────────────────────────────
def picotool():
    _run(["docker", "build", "-f", "Dockerfile.picotool", "-t", "proteus-picotool", "."])


def images():
    _dc("build", "build")        # proteus-toolchain (firmware + host C++ tests)
    _dc("build", "mfg-gui")      # proteus-mfg       (GUI + tests + formatters)
    picotool()                   # proteus-picotool  (flashing)


def firmware():
    _dc("run", "--rm", "build")


def host_tests():
    _ctest("build-host", sanitize=False)


def asan():
    _ctest("build-asan", sanitize=True)


def mfg_tools():
    _dc("build", "mfg-gui")


def mfg_tests():
    _dc("run", "--rm", "mfg-tests")


def run_mfg():
    _run("xhost +local:root", shell=True, check=False)   # best-effort X11 grant
    _dc("run", "--rm", "mfg-gui")


def fmt():
    cmd = (r"clang-format -i $(find src test -type f "
           r"\( -name '*.cpp' -o -name '*.h' -o -name '*.c' \)) && "
           r"black -q tools/mfg build.py")
    _dc("run", "--rm", "mfg-tests", "bash", "-lc", cmd)


def clean():
    for d in ("build", "build-host", "build-asan"):
        path = os.path.join(REPO, d)
        if os.path.isdir(path):
            shutil.rmtree(path, ignore_errors=True)
            print(f"removed {d}/")
    for cache in glob.glob(os.path.join(REPO, "**", "__pycache__"), recursive=True):
        shutil.rmtree(cache, ignore_errors=True)
    print("cleaned __pycache__")


_STEPS = [  # (arg attr, func) in execution order
    ("images", images),
    ("picotool", picotool),
    ("mfg_tools", mfg_tools),
    ("format", fmt),
    ("firmware", firmware),
    ("host_tests", host_tests),
    ("asan", asan),
    ("mfg_tests", mfg_tests),
]


def main():
    ap = argparse.ArgumentParser(
        description="Proteus build — all in Docker",
        formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    for flag in ("--images", "--firmware", "--host-tests", "--asan", "--mfg-tools",
                 "--mfg-tests", "--picotool", "--run-mfg", "--format", "--all", "--clean"):
        ap.add_argument(flag, action="store_true")
    args = ap.parse_args()

    if not any(vars(args).values()):
        ap.print_help()
        return 0
    if args.all:
        args.images = args.firmware = args.host_tests = args.mfg_tests = True

    try:
        if args.clean:
            clean()
        for attr, func in _STEPS:
            if getattr(args, attr):
                func()
        if args.run_mfg:
            run_mfg()
    except subprocess.CalledProcessError as exc:
        print(f"\n{R}BUILD FAILED (exit {exc.returncode}){RST}")
        return 1
    print(f"\n{G}done.{RST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
