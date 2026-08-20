#!/usr/bin/env python3
"""Cross-check the C patch table embedded in BC250DXEv3SMUPatchDxe.h against
patches.hex.

Usage:
  python3 Build/verify_patch_table.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEX = ROOT / "Build" / "patches.hex"
HDR = ROOT / "BC250DXEv3SMUPatchPkg" / "BC250DXEv3SMUPatchDxe" / "BC250DXEv3SMUPatchDxe.h"


def read_hex(path):
    sites, base = [], 0
    for line in path.open():
        line = line.strip()
        if not line.startswith(":"):
            continue
        count = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        typ = int(line[7:9], 16)
        data = bytes.fromhex(line[9:9 + count * 2])
        if typ == 2:
            base = int(line[9:13], 16) << 4
        elif typ == 0:
            sites.append((base + addr, data))
    return sorted(sites)


def parse_c_table(path):
    text = path.read_text()
    # Match entries like: { 0x29863, 3, { 0xA2, 0x14, 0x70 } },
    entries = []
    for m in re.finditer(
        r"\{\s*0x([0-9A-Fa-f]+)\s*,\s*([0-9]+)\s*,\s*\{\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*)*0x[0-9A-Fa-f]{2}\s*|)\s*\}\s*\}", text):
        addr = int(m.group(1), 16)
        size = int(m.group(2))
        data = bytes(int(x, 16) for x in m.group(3).split(",") if x.strip())
        entries.append((addr, size, data))
    # Sort the same way the reference patcher does (patches are sorted by addr).
    entries.sort(key=lambda e: e[0])
    return entries


def main():
    expected = [(addr, len(data), data) for addr, data in read_hex(HEX)]
    actual = parse_c_table(HDR)

    if len(expected) != len(actual):
        print(f"FAIL: expected {len(expected)} sites, found {len(actual)}")
        sys.exit(1)

    ok = True
    for i, (e, a) in enumerate(zip(expected, actual)):
        if e != a:
            print(f"FAIL site {i}: expected {e} got {a}")
            ok = False

    if ok:
        print(f"OK: {len(actual)} sites match patches.hex exactly")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()