#!/usr/bin/env python3
"""Generate the C patch table for the DXEv3 SMU Patch driver from patches.hex.

Replicates the parser in bc250-smu-unlock/patcher.py:
  - Type-02 records set the 16-bit extended linear base (base = addr << 4).
  - Type-00 records are data records placed at base + addr.
"""

from pathlib import Path

HEX = Path(__file__).resolve().parent.parent / "Build" / "patches.hex"


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


def main():
    patches = read_hex(HEX)
    print(f"// Total patch sites: {len(patches)}")
    print()
    for addr, data in patches:
        size = len(data)
        bytestr = ", ".join(f"0x{b:02X}" for b in data)
        print(f"    {{ 0x{addr:05X}, {size}, {{ {bytestr} }} }},")


if __name__ == "__main__":
    main()