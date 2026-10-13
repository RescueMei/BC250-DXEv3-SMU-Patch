# Example AMDGPU (kernel) patch — 8-core metrics display

The DXE SMU patch in this repo makes the firmware *put* cores 6/7 into the
metrics table. Nothing userspace-visible changes until the **kernel driver**
that reads those metrics is also updated. This folder contains that kernel-side
patch, provided as an example of what a mainline/DKMS drop would look like.

## Why a driver change is required

`drivers/gpu/drm/amd/pm/swsmu/smu11/cyan_skillfish_ppt.c` fills
`gpu_metrics_v2_2` with a hardcoded 6-core loop:

```c
for (i = 0; i < 6; i++)
        gpu_metrics->current_coreclk[i] = metrics.Current.CoreFrequency[i];
```

The `gpu_metrics_v2_2` arrays (`current_coreclk[8]`, `average_core_power[8]`,
`temperature_core[8]`) are already 8-wide — slots 6/7 are simply never filled.
The SMU alone cannot change this; the driver has to parse the extra data.

## How the patch works

The SMU patch leaves the transferred `SmuMetrics_t` at its stock **0xF4** size
(the transfer cannot be extended) and delivers cores 6/7 inside the 12 trailing
bytes the driver normally ignores:

```
SmuMetrics_t layout (0xF4)
  Current   @ 0x00  (0x74)   ... Current.Spare @ 0x72  <- magic 0x67 when active
  Average   @ 0x74  (0x74)
  SampleStartTime/StopTime/Accnt @ 0xE8 (0x0C)  <- driver-unused: cores 6/7 ride here
```

At `+0xE8` the SMU writes:

```c
struct cyan_skillfish_metrics_extra {   /* 12 bytes */
        u16 CoreFrequency2[2];     /* cores 6,7 MHz   @ +0xE8 */
        u16 CorePower2[2];         /* cores 6,7 power @ +0xEC */
        u16 CoreTemperature2[2];   /* cores 6,7 degC  @ +0xF0 */
};
```

The driver is modified in three files:

1. **`cyan_skillfish_ppt.c`**
   - Adds `CYAN_SKILLFISH_METRICS_MAGIC 0x67` and the 12-byte extra struct.
   - After the stock 6-core loop, if `metrics.Current.Spare == 0x67`, reads the
     struct at `(const u8 *)&metrics + 0xE8` and fills `current_coreclk[6+i]`,
     `average_core_power[6+i]`, `temperature_core[6+i]`.
   - Registers an extra SMU table (`SMU_METRICS_EXT`). This is not on the data
     path, but is **load-bearing** in practice — removing it broke the metrics
     export (`smu_err=6`), so keep it until the SMU table registration is
     understood better.
2. **`smu11_driver_if_cyan_skillfish.h`** — `#define TABLE_SMU_METRICS_EXT 3`.
3. **`amdgpu_smu.h`** — `SMU_TABLE_SMU_METRICS_EXT` enum entry.

The `Spare == 0x67` gate makes the patch a **no-op on stock firmware**: a stock
SMU leaves `Spare = 0`, so slots 6/7 stay `65535` and the driver behaves exactly
as before. The change is therefore safe to ship unconditionally.

## Applying it

Against a pristine kernel source tree:

```sh
git apply cyan_skillfish_8core.patch
```

or patch the three files by hand using the hunks as a reference. Rebuild only
the `amdgpu` module afterwards (and regenerate the initramfs, e.g. `dracut -f`,
so the new module is the one loaded at boot).

## Compatibility matrix

| SMU patch | driver patch | cores 6/7 visible | safe |
|-----------|--------------|-------------------|------|
| no        | no           | no                | yes  |
| yes       | no (stock)   | no (`65535`)      | yes  |
| no        | yes          | no (gate false)   | yes  |
| yes       | yes          | **yes**           | yes  |

## Files

- `cyan_skillfish_8core.patch` — diff vs. a pristine `linux-6.19.10` tree
  (`cyan_skillfish_ppt.c`, `smu11_driver_if_cyan_skillfish.h`, `amdgpu_smu.h`).
