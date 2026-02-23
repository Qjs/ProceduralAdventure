#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 [native|web]"
    echo "  native  - Build for desktop (default)"
    echo "  web     - Build for browser via Emscripten"
    exit 1
}

TARGET="${1:-native}"

case "$TARGET" in
    native)
        cmake -B build -DCMAKE_BUILD_TYPE=Release
        cmake --build ./build/ -j4
        ;;
    web)
        if ! command -v emcmake &>/dev/null; then
            echo "Error: emcmake not found. Install and activate the Emscripten SDK first."
            echo "  https://emscripten.org/docs/getting_started/downloads.html"
            exit 1
        fi
        emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
        cmake --build ./build-web/
        echo ""
        echo "Web build complete: build-web/SDL3Starter.html"
        echo "Serve with:  python3 serve.py"
        ;;
    *)
        usage
        ;;
esac
