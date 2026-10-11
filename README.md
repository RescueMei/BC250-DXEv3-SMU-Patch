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
       ├─ inject the compiled payload (SmuPatchPayload.h) at SMU SRAM 0x3B000
       │    (verified all-zero first; the free region above the governor)
       └─ apply the two manual hooks (byte-granular, read-back verified):
            0x29A10  A0 06      movi a13,6   cap the stock per-core metrics
                                             loop (FUN_00029848) at 6 cores
            0x29A1D  j 0x3B00C  jump into smu_patch_entry (replacing the
                                             firmware's "l32r a7,0xCA54")
```

The payload routine (`payload/src/smu_patch.c`) then, on the metrics fill path:
1. writes **`Spare` (Current+0x72) = 0x67** (magic), and
2. emits cores 6/7 into the table **tail**, mirroring the stock layout.

### Why the two hooks are manual
The cap and the jump are single-instruction edits into *existing* firmware code
at fixed addresses; they have no compilable representation. Everything else
(the tail computation, scaling, magic) is the compiled payload. Both hook sites
are documented above and in `BC250DXEv3SMUPatchDxe.h`.

## Resulting `SmuMetrics_t.Current` ABI (@0xCBC8)
Stock fields unchanged (`CoreFreq` +0x00, `CorePower` +0x0C, `CoreTemp` +0x24,
`C0Residency` +0x38, `Gfxclk` +0x44, … `Throttle` +0x70).  Added tail:
`CoreFrequency2[2]` +0x74, `CorePower2[2]` +0x80, `CoreTemperature2[2]` +0x98,
`C0Residency2[2]` +0xAC; `Spare` +0x72 = `0x0067`.

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
  src/smu_patch.c             tail emit + Spare magic (the compiled patch)
  src/entry.S                 smu_patch_entry: save a0-a7, call C, resume 0x29A20
  src/smu_patch_map.h         decoded SMU addresses
  src/smu_patch_layout.h      scratch/stack block + resume address
  src/sram.h                  SMU-SRAM access macro
  linker.ld                   link base 0x3B000
  Makefile, build.sh
BC250DXEv3SMUPatchPkg/        edk2 DXE driver (inject + hooks)
BC250DXEv3SMUPatchDxe.h       manual-hook addresses (SMU_PATCH_CAP_ADDR/HOOK_ADDR)
```

## Validation (staged live via SMUProbe, read from Linux)
stock 6 cores intact, `Spare=0x0067`, tail `freq2 [1555,3500] MHz`,
`power2 [60,283] mW`, `temp2 [43.0,47.25] C`, `resid2 [8,16]`.
