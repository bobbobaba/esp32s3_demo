#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# KiCad/GTK can abort on some desktop sessions when started from automation.
# These options keep the project manager stable on this machine.
export NO_AT_BRIDGE=1
export GDK_BACKEND=x11
export LIBGL_ALWAYS_SOFTWARE=1
export __GLX_VENDOR_LIBRARY_NAME=mesa
export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe

exec kicad "$PWD/esp32s3_watch_carrier.kicad_pro"
