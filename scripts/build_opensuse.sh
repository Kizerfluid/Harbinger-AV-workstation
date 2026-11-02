#!/usr/bin/env bash
# Build the Harbinger 0.6.3 GUI on openSUSE (Leap with Qt>=6.4 or Tumbleweed).
# Unit tests are optional, not a prerequisite for building the desktop app.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

RUN_TESTS=OFF
case "${1:-}" in
    "") ;;
    --with-tests) RUN_TESTS=ON ;;
    *) echo "Usage: bash scripts/build_opensuse.sh [--with-tests]" >&2; exit 2 ;;
esac
if (( $# > 1 )); then
    echo "Usage: bash scripts/build_opensuse.sh [--with-tests]" >&2
    exit 2
fi

for bin in cmake ninja c++; do
    if ! command -v "$bin" >/dev/null 2>&1; then
        echo "Missing $bin. Install the packages listed in OPENSUSE_HIVE.md" >&2
        exit 1
    fi
done

cmake -S . -B build-opensuse -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING="$RUN_TESTS"
if [[ "$RUN_TESTS" == ON ]]; then
    # Developer verification mode: build both Harbinger and all C++ test targets.
    cmake --build build-opensuse --parallel 2
    ctest --test-dir build-opensuse --output-on-failure
else
    # End-user default: do not let a unit-test compilation failure block the GUI.
    cmake --build build-opensuse --target Harbinger --parallel 2
fi

printf '\nGUI built: %s/build-opensuse/Harbinger\n' "$ROOT"
printf 'Launch from your openSUSE desktop: ./build-opensuse/Harbinger\n'
printf 'Harbinger Hive worker sharing is enabled within the graphical interface.\n'
printf 'Developer tests (optional): bash scripts/build_opensuse.sh --with-tests\n'
