#!/bin/sh
# Build deadspace_nx.nsp in the AArch32 toolchain container: the runtime's
# docker_build.sh (arguments go to make: ./build.sh clean, DCR_GL_MESA=0,
# rt-files). libnx32 is found next to this folder (../libnx32/prefix and
# friends), or where DCR_LIBNX32 says.
exec "$(dirname "$0")/runtime/tools/docker_build.sh" "$@"
