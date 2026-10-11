#!/usr/bin/env bash
#
# BC-250 SMU Patch - full build.
#
#   1. generate the SMU byte-patch payload (payload/patches.hex ->
#      SMUPatchTable.h) via payload/build.sh
#   2. build the DXE .efi/.ffs via the shared DXEBUILD build script
#
# Usage: ./build.sh
# Env:   CONTAINER_ENGINE (podman), XTENSA_IMAGE, NO_CONTAINER=1,
#        plus any build_ffs.sh overrides (ARCH, TARGET, TOOL_CHAIN_TAG, ...)
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

echo "== 1. SMU patch payload"
"$ROOT/payload/build.sh"

echo "== 2. DXE driver"
"$ROOT/build_ffs.sh"

echo
echo "== artifacts"
ls -1 "$ROOT/Build/Output/" | sed 's/^/   Build\/Output\//'
