# BC250-DXEv3-SMU-Patch

DXE driver that patches the BC-250 SMU firmware so its metrics table reports all
eight CPU cores correctly when the core unlock is active. It requires the SMU to
be unlocked first; the Depex enforces that.

> **Firmware warning:** this driver rewrites live SMU code and installs a tick
> hook. A bad write can leave the SMU unresponsive and require a cold power
> cycle.

## At a glance

| Item | Value |
| --- | --- |
| Package / module | `BC250DXEv3SMUPatchPkg` -> `MeiMeiDXEv3_SMU_Patch.efi` |
| Depex | `gEfiVariableArchProtocolGuid AND gMeiMeiDXEv3SmuUnlockProtocolGuid` |
| Produces | no protocol |
| Consumes | SMU-unlock protocol, `MeiMeiDXEv3SmuPatchVar` |
| Configuration | `MeiMeiDXEv3SmuPatchVar`, 1 byte |
| SMU SRAM | payload at `0x3AE00`, shared lane `0x1200` bytes |
| Compliance | [COMPLIANCE.md](COMPLIANCE.md) |

## Dependencies

`BC250-DXEv3-SMU-Unlock` must have opened the Q3 cluster. This driver also
re-probes privileged access before patching and skips the work if the gate is
closed. No other DXE driver is required.

## How it works

### Problem

The stock metrics fill loops are count-bounded and write fixed six-entry arrays
(`CoreFrequency[6]`, `CorePower[6]`, `CoreTemperature[6]`, `C0Residency[6]`).
With the eight-core unlock the SMU core-count byte (`0x7AD8 + 0x1A`) is 8, so
each loop runs eight iterations and the last two overrun the following fields.
`C0Residency[6]` lands exactly on `Current.GfxclkFrequency`
(`Current + 0x38 + 6*2 = Current + 0x44`), which the amdgpu driver reports as
the GPU clock.

### Fix

The driver keeps the stock struct unchanged for cores 0-5, stamps a magic value
in `Spare`, and emits cores 6/7 into an expanded block immediately after it.
Four hooks do the work:

```
0x29A0F  j smu_clamp_average    cap the Average fill at 6 (resume 0x29A12)
0x29BE5  j smu_clamp_current    cap the Current fill at 6 (resume 0x29BE8)
0x29A1D  j smu_patch_entry      emit the tail (replaces "l32r a7, 0xCA54")
0x29CB8  j smu_spare_entry      stamp Current.Spare after the Current fill
```

The compiled payload (`0x3AE00`, entry offset `0x28` -> `0x3AE28`):

1. writes `Current.Spare = 0x67` (`Current + 0x72`);
2. mirrors the stock per-core offsets at `+0x74`:
   `CoreFrequency2[2]`, `CorePower2[2]`, `CoreTemperature2[2]`,
   `C0Residency2[2]`.

The metrics base literal at `0x17F24` is relocated to `0x3B500`, so the extra
cores land in free SRAM rather than in unrelated firmware fields.

### Metrics layout

```
SmuMetricsTable_t (0x74):   Current @ 0xCB54,   Average @ 0xCBC8
  0x00 CoreFrequency[6]   0x30 L3Frequency[2]    0x44 GfxclkFrequency
  0x0C CorePower[6]       0x34 L3Temperature[2]  0x46 GfxTemperature
  0x24 CoreTemperature[6] 0x38 C0Residency[6]    ...
  0x72 Spare
```

`Current.Spare` = `0xCBC6`; the expanded cores follow it at
`0xCBC8 / 0xCBD4 / 0xCBEC / 0xCC00`.

## Configuration

- Header: `BC250-DXEv3-Shared/MeiMeiDXEv3SharedPkg/Include/Config/MeiMeiDXEv3Config.h`
- Variable: `MEIMEIDXEV3_SMU_PATCH_VAR_NAME` (`MeiMeiDXEv3SmuPatchVar`)
- Field: `SmuReportingPatch` at offset 0

A missing, oversized, unreadable, or wrong-attribute variable means no patch and
a normal boot.

## Protocols

Also publishes `gMeiMeiDXEv3DriverInfoProtocolGuid` with its name, version, module GUID, config schema, and payload version for the `BC250-DXEv3-Driver-Info` telemetry driver.

Produces no protocol. It consumes `gMeiMeiDXEv3SmuUnlockProtocolGuid` through
its Depex and re-probes privileged access before patching.

## SMU SRAM placement

Payload and scratch live in the SMU Patch lane of the canonical map:
`0x3AE00`..`0x3C000` (`0x1200` bytes). The build pins the generated payload
base to `MEIMEIDXEV3_SMU_PATCH_BASE` with a `STATIC_ASSERT`, bounds the image
length by `MEIMEIDXEV3_SMU_PATCH_MAX`, and keeps the image 4-byte aligned. The
cache-maintenance registers come from the same map. See
[SMU_MEMORY_MAP.md](https://github.com/RescueMei/BC250-DXEv3-Shared/blob/main/docs/SMU_MEMORY_MAP.md).

## Verification

Every injected dword, every hooked byte, the tick-slot entry, and the metrics
relocation literal are read back and compared. The payload region is verified
all-zero before injection.

## Build

```bash
git submodule update --init --recursive
./build_ffs.sh        # prebuild payload (Xtensa) + DXE .efi/.ffs
```

Output:

- `Build/Output/MeiMeiDXEv3_SMU_Patch.efi`
- `Build/Output/MeiMeiDXEv3_SMU_Patch.ffs`

## Validation

2026-11-25, nine-driver stack, configured ceiling 2100 MHz: `DEPLOY_OK` with
`patch tick 0xC780 = 0x0003AE28`, actual clock 2100 MHz sustained, 82 C peak,
0 ring timeouts, 0 C2P timeouts, and `pp_dpm_sclk` / `gpu_metrics.current_gfxclk`
reporting the real clock. See [COMPLIANCE.md](COMPLIANCE.md).
