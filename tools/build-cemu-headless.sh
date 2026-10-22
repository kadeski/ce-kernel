#!/bin/sh
# Builds cemu-headless (CEmu's emulator core with a stdin command interface,
# used by tests/smoke.py) and installs it next to the toolchain's binaries.
#
# usage: tools/build-cemu-headless.sh [install-dir]   (default: $CEDEV/bin or ~/CEdev/bin)
set -eu

# Commit the smoke test was written against. Newer ones should also work.
CEMU_COMMIT=e5ffbb2c80ff241390bc3cc45a2bf01ba9748225
DEST=${1:-${CEDEV:-$HOME/CEdev}/bin}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

git clone --quiet https://github.com/CE-Programming/CEmu.git "$work/CEmu"
git -C "$work/CEmu" checkout --quiet "$CEMU_COMMIT"
make -C "$work/CEmu/core" -j4
make -C "$work/CEmu/tests/autotester" -j4

mkdir -p "$DEST"
cp "$work/CEmu/tests/autotester/cemu-headless" "$DEST/"
echo "installed $DEST/cemu-headless"
