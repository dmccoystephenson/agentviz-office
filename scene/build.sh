#!/usr/bin/env bash
# Builds scene/main.cpp into web/office-engine.{js,wasm} and copies the Strata
# web kit's page scripts beside them. The outputs are committed, so serving
# needs only Python. Needs emsdk 6.0.11 (see /root/agentviz/docs/BUILDING.md).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(dirname "$here")"
STRATA_VERSION=0.10.0
STRATA_SHA256=bb86b974c7e0309db91625ba5ee157273704c77d93f4338c8a63ee625e3d56de
tarball="$repo/.strata/strata-$STRATA_VERSION-wasm32-emscripten.tar.gz"
if [ -z "${EMSDK_PYTHON:-}" ] || ! command -v emcc >/dev/null; then
  # shellcheck disable=SC1091
  . "${AGENTVIZ_DIR:-/root/agentviz}/tools/emsdk-env.sh"
fi
mkdir -p "$repo/.strata"
if [ ! -f "$tarball" ]; then
  gh release download "v$STRATA_VERSION" -R Preponderous-Software/strata \
    -p "strata-$STRATA_VERSION-wasm32-emscripten.tar.gz" -D "$repo/.strata"
fi
echo "$STRATA_SHA256  $tarball" | sha256sum -c --quiet
[ -d "$repo/.strata/kit" ] || tar -xzf "$tarball" -C "$repo/.strata"
STRATA_TARBALL="$tarball" STRATA_SHA256="$STRATA_SHA256" \
  "$repo/.strata/kit/build-kit.sh" "$here/kit.conf"
