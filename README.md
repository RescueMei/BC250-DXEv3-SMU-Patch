# BC250-DXEv3-SMU-Patch  (branch: TheBigOne)

DXE driver that patches the **BC-250 SMU firmware** at boot so its metrics table
**correctly reports all 8 CPU cores** (with the 8-core unlock active). Requires
the SMU to be unlocked first (the SMU Unlock driver; enforced by `[Depex]`).

## Problem
The stock metrics fill loops are **count-bounded** and write fixed **6-entry**
per-core arrays (`CoreFrequency[6]`, `CorePower[6]`, `CoreTemperature[6]`,
`C0Residency[6]`). With the 8-core unlock the SMU core-count byte
(`0x7AD8+0x1A`) is 8, so each loop runs 8 iterations and the last two overrun
into the following fields. `C0Residency[6]` lands exactly on
`Current.GfxclkFrequency` (`Current+0x38 + 6*2 = Current+0x44`) — the field the
amdgpu driver reports as the GPU clock (`pp_dpm_sclk` / `rocm-smi` / `gpu_metrics
.current_gfxclk`).

## Fix (proper 8-core handling)
Output the **stock struct unchanged** for cores 0..5, put the magic in `Spare`,
and append cores 6/7 in an expanded block right after `Spare`. Two manual hooks
cap the fills; the compiled payload emits the extra cores:

```
0x29A10  A0 06        movi a13,6   cap the Average fill (FUN_00029848) at 6
0x29BE6  A0 06        movi a13,6   cap the Current fill (FUN_00029aec) at 6
0x29A1D  j 0x3D00C    jump into smu_patch_entry (replacing "l32r a7,0xCA54")
```

The payload then:
1. writes **`Current.Spare (+0x72) = 0x67`** (magic), and
2. mirrors the stock per-core offsets **+0x74**: `CoreFrequency2[2] +0x74`,
   `CorePower2[2] +0x80`, `CoreTemperature2[2] +0x98`, `C0Residency2[2] +0xAC`.

## `SmuMetrics_t` layout (driver `smu11_driver_if_cyan_skillfish.h`)
```
SmuMetricsTable_t (0x74):   Current @ 0xCB54,   Average @ 0xCBC8
  0x00 CoreFrequency[6]   0x30 L3Frequency[2]    0x44 GfxclkFrequency
  0x0C CorePower[6]       0x34 L3Temperature[2]  0x46 GfxTemperature
  0x24 CoreTemperature[6] 0x38 C0Residency[6]    ...
  0x72 Spare
```
`Current.Spare` = 0xCBC6, `Current.GfxclkFrequency` = 0xCB98. The expanded
cores follow `Spare` at `0xCBC8 / 0xCBD4 / 0xCBEC / 0xCC00`.

## SMU SRAM allocation (shared with the other DXEv3 SMU drivers)
Free region `0x3AE00..0x40000`. Disjoint slices (checked, no overlaps):

| Region             | Occupant                              |
|--------------------|---------------------------------------|
| `0x3AE00..0x3B9B0` | Governor payload image (2992 B)       |
| `0x3C000..0x3C0C8` | Governor state                        |
| `0x3C200..0x3C470` | CU-Unlock payload image (624 B)       |
| `0x3C4C0..0x3C670` | CU-Unlock save/stack/state            |
| `0x3D000..0x3D1C0` | **this** payload image (448 B)        |
| `0x3D200..0x3D340` | **this** scratch (save/stack)         |

## Layout
```
payload/
  src/smu_patch.c             extra-core emit + Spare magic (the compiled patch)
  src/entry.S                 smu_patch_entry: save a0-a7, call C, resume 0x29A20
  src/smu_patch_map.h         decoded SMU addresses (Current @0xCB54, ...)
  src/linker.ld / Makefile / build.sh
BC250DXEv3SMUPatchPkg/        edk2 DXE driver (inject + hooks)
BC250DXEv3SMUPatchDxe.h       SMU_PATCH_CAP_ADDR / SMU_PATCH_CAP2_ADDR / SMU_PATCH_HOOK_ADDR
```

## Build
```
git submodule update --init --recursive   # DXEBUILD
./build.sh                                # payload (Xtensa) -> DXE .efi/.ffs
```

## Validation (live via SMUProbe + Linux `gpu_metrics`)
8 cores / 16 threads, 40 CUs; `pp_dpm_sclk` line 1 and `gpu_metrics`
`current_gfxclk` = `average_gfxclk` = **350 MHz** (the governor's forced
target) — previously the `C0Residency[6]` overflow read back as ~6.
