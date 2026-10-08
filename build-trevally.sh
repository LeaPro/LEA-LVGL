#!/bin/bash
#
# Cross-compiles liblvgl/liblvgl_linux directly against the Trevally SDK
# toolchain and installs straight into the SDK's sysroot -- bypassing bitbake
# entirely, so this can be run every time you change something here without
# needing a commit/push/SRCREV bump or a full `make sdk`.
#
# Forces .config to the Trevally target defconfig (DRM + evdev + HID encoder,
# pulled from meta-lea-trevally) every run, since .config is shared with the
# host build.sh above.
#
# Requires toolchainSetup to be a symlink to the Trevally SDK's
# environment-setup-aarch64-oe-linux script (not kept in git since the
# location may vary by user):
#   ln -s ../yocto-scarthgap/sdk/trevally/environment-setup-aarch64-oe-linux toolchainSetup
#
# lvgl-trevally_git.bb's pinned SRCREV is untouched by any of this -- it's
# still what anyone else gets from a plain `make sdk`.

set -euo pipefail
SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
cd "$SCRIPT_DIR"

TREVALLY_DEFCONFIG="$SCRIPT_DIR/../yocto-scarthgap/meta-lea-trevally/recipes-graphics/lvgl/files/trevally.defconfig"

if [[ ! -e toolchainSetup ]]; then
  echo "toolchainSetup does not exist: it should be a symbolic link to the Trevally SDK's environment-setup script"
  echo "e.g.: ln -s ../yocto-scarthgap/sdk/trevally/environment-setup-aarch64-oe-linux toolchainSetup"
  echo "(note that the location may vary by user, so the link is not kept in the git repo)"
  exit 1
fi

unset LD_LIBRARY_PATH
source toolchainSetup
if [[ -z ${SDKTARGETSYSROOT:-} ]] ; then
  echo "SDKTARGETSYSROOT is not set; something is amiss with your sdk environment :("
  exit 1
fi

cp "$TREVALLY_DEFCONFIG" .config

cmake -S . -B build-trevally \
  -DCMAKE_TOOLCHAIN_FILE="$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake" \
  -DBUILD_SHARED_LIBS=ON \
  -DLV_BUILD_INSTALL=ON \
  -DCMAKE_INSTALL_PREFIX="$SDKTARGETSYSROOT/usr"

cmake --build build-trevally -- -j"$(nproc)"
cmake --install build-trevally
