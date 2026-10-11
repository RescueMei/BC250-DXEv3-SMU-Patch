# BC250-DXEv3-SMU-Patch  (branch: TheBigOne)

DXE driver that applies a byte-patch table to the **BC-250 SMU firmware** at
boot (through the gated write-through interface). Requires the SMU to be
unlocked first (the SMU Unlock driver, enforced by `[Depex]`).

This branch replaces the previous "8-core reporting" patch with the
**8-core SMU metrics patch** (`Patch/`):

## What it does
* Caps the SMU metrics per-core fill loop (`FUN_00029848`) at **6** cores so
  the stock `SmuMetrics_t.Current` (SMU SRAM `0xCBC8`) is no longer overrun
  when the core count (`PTR_DAT_00017050[0x1a]`) is 8 with the 8-core unlock.
* Emits the 2 extra cores into the table **tail**, mirroring the layout.
* Stamps **`Spare` (offset 0x72) = 0x67** as a magic marker.

## Edits (the byte-patch table, `Build/patches.hex` / `mSmuPatchSites[]`)
| SMU address | bytes | meaning |
|---|---|---|
| `0x29A10` | `A0 06` | `movi a13,6` (cap fill loop at 6) |
| `0x29A1D` | `C6 7C 45` | `j 0x3B014` (hook) |
| `0x3B000..` | 136-B block | added routine (free region) |

## Resulting `SmuMetrics_t.Current` ABI (@0xCBC8)
Stock fields unchanged (`CoreFreq` +0x00, `CorePower` +0x0C, `CoreTemp` +0x24,
`C0Residency` +0x38, `Gfxclk` +0x44, ... `Throttle` +0x70).  Added tail:
`CoreFrequency2[2]` +0x74, `CorePower2[2]` +0x80, `CoreTemperature2[2]` +0x98,
`C0Residency2[2]` +0xAC, `Spare` +0x72 = `0x0067`.

## Validation
Staged live via SMUProbe and read from Linux: stock 6 cores intact,
`Spare=0x0067`, tail `freq2 [1555,3500] MHz`, `power2 [60,283] mW`,
`temp2 [43.0,47.25] C`, `resid2 [8,16]`.

## Layout
```
BC250DXEv3SMUPatchPkg/   edk2 DXE driver (applies mSmuPatchSites[])
Build/patches.hex        patch table source
Build/gen_patch_table.py generate the C table from patches.hex
Patch/                   reference artifacts (source .S, patched .bin, README)
```
