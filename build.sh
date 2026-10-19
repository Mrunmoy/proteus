#!/usr/bin/env bash
# Convenience wrapper around the Docker build.
#   ./build.sh           configure + build (builds image first time)
#   ./build.sh clean     remove build/
#   ./build.sh rebuild   clean then build
set -euo pipefail
cd "$(dirname "$0")"

case "${1:-build}" in
    clean)
        rm -rf build
        echo "cleaned build/"
        ;;
    rebuild)
        rm -rf build
        exec "$0" build
        ;;
    build)
        docker compose build
        docker compose run --rm build
        echo
        if [[ -f build/proteus.uf2 ]]; then
            echo "OK -> build/proteus.uf2"
            echo "Flash: hold BOOTSEL, plug USB, copy the .uf2 onto the RP2350 mass-storage drive."
        else
            echo "build finished but proteus.uf2 not found — check output above" >&2
            exit 1
        fi
        ;;
    *)
        echo "usage: $0 [build|clean|rebuild]" >&2
        exit 2
        ;;
esac
