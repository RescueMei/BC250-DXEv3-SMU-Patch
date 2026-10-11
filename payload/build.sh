#!/usr/bin/env bash
#
# Build the SMU byte-patch payload: generate SMUPatchTable.h (the patch sites
# the DXE driver injects) from patches.hex.
#
# This is a pure-Python step (no toolchain needed).
#
# Usage: ./build.sh
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

echo "== SMUPatchTable.h (from patches.hex)"
( cd "$REPO_DIR" && python3 payload/gen_patch_table.py )
