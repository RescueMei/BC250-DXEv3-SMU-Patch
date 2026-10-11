# BC250-DXEv3-SMU-Patch  (branch: TheBigOne)

DXE driver that patches the **BC-250 SMU firmware** at boot to make its metrics
table report all 8 CPU cores (with the 8-core unlock active). Requires the SMU
to be unlocked first (the SMU Unlock driver; enforced by `[Depex]`).

## Design
The patch is **compiled Xtensa C** (like `BC250-DXEv3-SMU-Governor` /
`BC250-DXEv3-CU-Unlock`) plus two **manual hooks** that cannot be compiled:

```
boot (DXE)
  └─ MeiMeiDXEv3_SMU_Patch
       ├─ inject the compiled payload (SmuPatchPayload.h) at SMU SRAM 0x3D000
       │    (verified all-zero first; a free slot above governor/CU-Unlock)
       └─ apply the two manual hooks (byte-granular, read-back verified):
            0x29A10  A0 06      movi a13,6   cap the stock per-core metrics
                                             loop (FUN_00029848) at 6 cores
            0x29A1D  j 0x3D00C  jump into smu_patch_entry (replacing the
                                             firmware's "l32r a7,0xCA54")
```

The payload routine (`payload/src/smu_patch.c`) then, on the metrics fill path:
1. writes **`Spare` (Current+0x72) = 0x67** (magic), and
2. emits cores 6/7 into a dedicated **free‑SRAM block at `0x3D400`** (see
   *Metrics layout / the `0xCC48` collision* below) — *not* into the metrics
   table, which has no spare room.

### Why the two hooks are manual
The cap and the jump are single-instruction edits into *existing* firmware code
at fixed addresses; they have no compilable representation. Everything else
(the tail computation, scaling, magic) is the compiled payload. Both hook sites
are documented above and in `BC250DXEv3SMUPatchDxe.h`.

## SMU SRAM allocation (shared with the other DXEv3 SMU drivers)
The documented free region is `0x3AE00..0x40000`. To coexist, the three drivers
each own a disjoint, spaced slice (checked with no overlaps):

| Region                 | Occupant                                   |
|------------------------|--------------------------------------------|
| `0x3AE00..0x3B9B0`     | Governor payload image (2992 B)            |
| `0x3C000..0x3C0C8`     | Governor state                             |
| `0x3C200..0x3C470`     | CU-Unlock payload image (624 B)            |
| `0x3C4C0..0x3C670`     | CU-Unlock save/stack/state                 |
| `0x3D000..0x3D1D4`     | **this** payload image (468 B)             |
| `0x3D200..0x3D340`     | **this** scratch (save/stack)              |
| `0x3D400..0x3D418`     | **this** extra-core block (cores 6/7)      |

The patch base is intentionally **not** `0x3B000`: the Governor payload reaches
`0x3B9B0`, so `0x3B000` overlapped it (and the old scratch at `0x3B100`
overlapped the patch's own 447-byte image). Both are fixed by the `0x3D000` /
`0x3D200` placement.

## Metrics layout / the `0xCC48` collision
The stock metrics block lives at `0xCB54`; `SmuMetrics_t.Current` is at
`0xCB54 + 0x74 = 0xCBC8` and is **0x74 bytes**, so it ends at `0xCC3C`. But the
SMU's *real* metrics block is **0xF4 bytes** (`0xCB54..0xCC48`) and is followed
immediately by **live firmware data**:

| Address | Content |
|---|---|
| `0xCC48` | device/topology descriptor, symbol `s_AMD_BC_250` — name `"AMD BC-250"` at +0x00, flags at +0x18/+0x1C/+0x2C/+0x30/+0x34/+0x38/+0x40 |
| `0xCC90` | the descriptor's core→slot byte map (+0x48); the stock metrics fill reads it as `PTR_DAT_00017f38` |
| `0xCCA8` | a function‑pointer table |

So appending a "tail" at `Current+0x74` (the original design) wrote
`0xCC3C..0xCC78`, i.e. **onto the descriptor** — `power2` landed on the name at
`0xCC48`, `temp2`/`resid2` on its `+0x18`/`+0x1C`/`+0x2C`/`+0x30` fields (which
functions near `0x1c5cc`/`0x27108` read). There is **no free space** to extend
`Current` in place.

### Current ABI (`0xCBC8`)
Stock fields unchanged (`CoreFreq` +0x00, `CorePower` +0x0C, `CoreTemp` +0x24,
`C0Residency` +0x38, `Gfxclk` +0x44, … `Throttle` +0x70); **`Spare` +0x72 = 0x0067**
is the "patched" marker (unused by stock). The extra cores are published
separately, gated by that magic:

### Extra-core block ABI (`SMU_EXTRA_BASE = 0x3D400`, free SRAM)
| Offset | Field |
|---|---|
| `+0x00` | magic `0x38434F52` (`"ROC8"`) |
| `+0x04` | `CoreFrequency2[2]` (u16 ×2, MHz) |
| `+0x08` | `CorePower2[2]` (u32 ×2, mW) |
| `+0x10` | `CoreTemperature2[2]` (u16 ×2, centi‑C) |
| `+0x14` | `C0Residency2[2]` (u16 ×2) |

A consumer reads cores 6/7 here when `Current.Spare == 0x0067`.

## Build
```
git submodule update --init --recursive   # DXEBUILD
./build.sh                                # payload (Xtensa) -> DXE .efi/.ffs
```
`build.sh` runs `payload/build.sh` (bc250-xtensa container): compiles
`payload/src/*` -> `smu_patch.bin` + `SmuPatchPayload.h` and emits
`SmuPatchMeta.h` (link base + entry offset).  `build_ffs.sh` alone also
regenerates them via `PREBUILD_CMD` in `build.conf`.

## Layout
```
build.sh                      full build (payload + DXE)
build_ffs.sh / build.conf     DXEBUILD wrapper + per-project config
payload/
  src/smu_patch.c             extra-core emit + Spare magic (the compiled patch)
  src/entry.S                 smu_patch_entry: save a0-a7, call C, resume 0x29A20
  src/smu_patch_map.h         decoded SMU addresses
  src/smu_patch_layout.h      scratch/stack block + resume address
  src/sram.h                  SMU-SRAM access macro
  linker.ld                   link base 0x3D000
  Makefile, build.sh
BC250DXEv3SMUPatchPkg/        edk2 DXE driver (inject + hooks)
BC250DXEv3SMUPatchDxe.h       manual-hook addresses (SMU_PATCH_CAP_ADDR/HOOK_ADDR)
```

## Validation (staged live via SMUProbe, read from Linux)
Earlier: stock 6 cores intact, `Spare=0x0067`, and the extra cores read from the
(old) table tail (`freq2 [1555,3500] MHz`, `power2 [60,283] mW`,
`temp2 [43.0,47.25] C`, `resid2 [8,16]`) — at the cost of clobbering the
`0xCC48` descriptor. After the fix the extra cores are read from `0x3D400`
(gated by `Spare`) and the descriptor is left intact.
