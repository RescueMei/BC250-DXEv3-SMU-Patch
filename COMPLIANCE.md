# SMU Patch Compliance

Driver: `BC250-DXEv3-SMU-Patch`
Shared package pin: `1974a09`.

Checklist and evidence:

- [x] Shared package version/pin recorded (`1974a09`).
- [x] Configuration structs/constants come from the shared package; no local duplicates.
- [x] Public protocol copies match the provider ABI; drift check passed.
- [x] Driver-owned variable names/layouts documented.
- [x] SMU SRAM allocations match `docs/SMU_MEMORY_MAP.md`; no overlap.
- [x] Depex is correct; public protocol is installed unconditionally.
- [x] Driver info record installed unconditionally.
- [x] Feature-disabled path performs no feature writes.
- [x] Missing/malformed config is rejected safely; boot continues.
- [x] Factory learning still occurs when the feature is disabled (Core/CU).
- [x] No cross-driver private headers or internal functions.
- [x] No forced-VID/force-state from SMU tick context.
- [x] SMU writes are read-back verified; reserved fields preserved.
- [x] Payload is 4-byte aligned, fits its lane, and was injection-tested.
- [x] Individual driver build passed.
- [x] Nine-driver integrated build passed.
- [x] BC250 deployment reached `DEPLOY_OK` with expected state.
- [x] Light and heavy workload validation completed, including actual clock and thermal checks.
- [x] No generated payload headers, build output, secrets, or temporary files staged.

## Configuration and fail-closed behavior

`SMU_PATCH_CONFIG` from `MeiMeiDXEv3Config.h`; missing, oversized, unreadable, or wrong-attribute configuration applies no patch and the boot continues.

## Protocol and Depex

Produces no protocol; it consumes the SMU-unlock marker through its Depex and re-probes privileged access.

Depex: `gEfiVariableArchProtocolGuid AND gMeiMeiDXEv3SmuUnlockProtocolGuid`.

## SMU map

Payload, scratch, and cache-maintenance addresses come from the shared SMU map. `STATIC_ASSERT`s pin the generated payload base to `MEIMEIDXEV3_SMU_PATCH_BASE`, the image length is bounded by `MEIMEIDXEV3_SMU_PATCH_MAX`, and `0x3E000` firmware is never used as a lane.

## Write verification

Every injected payload dword, every hooked byte, the tick-slot entry, and the metrics-relocation literal are read back and compared.

## Payload

4-byte aligned raw image at `0x3AE00`, inside the `0x1200`-byte shared lane; injection and readback exercised on hardware.



## Validation record

- Individual driver build: passed (`./build_ffs.sh`, RELEASE_CLANGDWARF).
- Nine-driver integrated build: passed (`BC250-DXEv3-BIOSMOD/build_all_ffs.sh`).
- BC250 deployment: `DEPLOY_OK ceiling=2100 cores=8 nopatch=False nogov=False`.
- Workload: 60 s Unigine Valley + 60 s Superposition, 8 cores, patch enabled.
- Actual clock: 2100 MHz sustained; rail voltage: <= 1006 mV; GPU power: 202 W peak.
- Peak temperature: 82 C (85 C governor target, 87 C hard cap).
- Ring timeouts: 0; C2P timeouts: 0; board reachable after the workload.

Measurements from the final integrated run; the configured ceiling and the
actual clock are recorded separately.
