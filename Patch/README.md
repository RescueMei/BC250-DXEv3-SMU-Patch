# BC-250 SMU metrics 8-core patch

Patches `smu_fw_robin3.bin` (BIOS 3.00 / Robin 3) so the SMU metrics table
reports all 8 CPU cores (with the 8-core unlock active) and stamps a magic.

## Problem
`FUN_00029848` (maps SMU hardware addresses to the Ghidra base) fills
`SmuMetrics_t.Current` at SMU SRAM **0xCBC8** (= `0xCB54 + 0x74`) in a loop
bounded by `PTR_DAT_00017050[0x1a]` = the core count.  With the 8-core unlock
that byte is **8**, but the `SmuMetrics_t` arrays (`CoreFrequency[6]`,
`CorePower[6]`, `CoreTemperature[6]`, `C0Residency[6]`) only hold 6, so
cores 6/7 overrun into the next arrays and corrupt the table.

## Patch (layout "b": stock 6-core layout untouched, extras in the tail)
Three edits:

| Site | Original | Patched | Meaning |
|---|---|---|---|
| `0x29a0f` | `d2 06 1a` | `d2 a0 06` | `movi a13,6` — cap the stock per-core loop at 6 (no overrun) |
| `0x29a1d` | `b1 b9 b5` | `c6 7c 45` | `j 0x3B014` — hook into the added block |
| `0x3B000..` | (zero) | 136-byte block | emits cores 6/7 into the tail + `Spare=0x67` |

The block is placed in the documented free region (`0x3AE00+`) at **0x3B000**
(code at `0x3B014`); it preserves `a4`, restores `a7=0xCA54`, and returns via
`j 0x29a20`.

## Validated ABI (read from `SmuMetrics_t.Current` @0xCBC8)
Stock (unchanged): `CoreFreq` +0x00, `CorePower` +0x0C, `CoreTemp` +0x24,
`C0Residency` +0x38, `Gfxclk` +0x44, ... , `Throttle` +0x70, `Spare` +0x72.

Added (tail, mirrors the layout):
| field | offset | width |
|---|---|---|
| `CoreFrequency2[2]` | +0x74 | u16 |
| `CorePower2[2]` | +0x80 | u32 |
| `CoreTemperature2[2]` | +0x98 | u16 |
| `C0Residency2[2]` | +0xAC | u16 |
| `Spare` (magic) | +0x72 | u16 = `0x0067` |

## Live validation (SMUProbe staging -> boot Linux, read 0xCBC8)
- stock 6-core freq/temp intact (no overrun);
- `Spare = 0x0067`;
- tail freq2 `[1555,3500] MHz`, power2 `[60,283] mW`, temp2 `[43.0,47.25] C`,
  resid2 `[8,16]`.

## Files
- `smu_metrics_8core_patch.S` — the added block (assemble with
  `-T pld.ld`, base 0x3B000; code entry 0x3B014).
- `smu_fw_robin3.patched.bin` — final patched image (262400 bytes).
- `stage.json` / `patch6b.bin` — assembled block + edit addresses.

## Staging (live, volatile — SMUProbe)
Write blob @0x3B000, cap @0x29a0f, hook @0x29a1d (4-byte aligned writes),
cache-publish SMU regions (helpers 0x30B3/0x30EB + 0x0E), then boot.

## Flash (persistent)
Write `smu_fw_robin3.patched.bin` into the SPI ROM's SMU firmware region.
