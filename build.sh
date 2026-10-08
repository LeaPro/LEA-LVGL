#!/bin/bash
#
# Host/simulator build (SDL backend). Forces .config to application-server's
# host defconfig every run, since .config is shared with build-trevally.sh's
# target build -- this guarantees a plain ./build.sh always gets you the
# host config back.
#
# Also builds as shared libs and installs into ~/.local, since that's what
# application-server's host (native) build links against -- see
# Trevally/application-server/README.md. Re-run this script whenever LEA-LVGL
# changes and you want those changes reflected in a host application-server
# build.

set -euo pipefail
SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
cd "$SCRIPT_DIR"

HOST_DEFCONFIG="$SCRIPT_DIR/../Trevally/application-server/lvgl/trevally_host.defconfig"

cp "$HOST_DEFCONFIG" .config
cmake -S . -B build -G Ninja -DBUILD_SHARED_LIBS=ON -DLV_BUILD_INSTALL=ON -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j"$(nproc)"
cmake --install build
