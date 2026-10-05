#!/bin/sh
# Build deadspace_nx.nro (the launcher) with the runtime's launcher build
# (devkitPro's 64-bit toolchain container). Build the wrapper first
# (../build.sh): the NRO carries ../deadspace_nx.nsp and ../deadspace_nx.build.
HERE="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER_DIR="$HERE" PAYLOAD=deadspace_nx exec "$HERE/../runtime/launcher/build.sh" "$@"
