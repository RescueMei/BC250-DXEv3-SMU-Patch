# Unified SMU SRAM allocation (BC-250)

Shared by the three DXEv3 SMU drivers. The SMU-local SRAM free region is
`0x3AE00 .. 0x40000`. Lane order is **Patch → CU-Unlock → Governor**; every lane
is disjoint (with headroom), and nothing is placed below `0x3AE00` or in
`0xCC48..` (live SMU firmware data).

```
[1] SMU-Patch (metrics 8-core)              FIRST
    0x3AE00  payload image      (<= 0x400 -> 0x3B200)   link base 0x3AE00
    0x3B200  scratch: a0-a7 + f0-f15 + stack (0x260 -> 0x3B460)
    0x3B480  FIXED tail block   (0x40 -> 0x3B4C0)
    0x3B500  .. headroom

[2] CU-Unlock (40 CU)                       SECOND
    0x3C000  payload image      (624 B -> 0x3C270)
    0x3C400  save / stack / state (0x180 -> 0x3C580)    state = base + 0x140

[3] SMU-Governor (GFX clock)                THIRD
    0x3D000  payload image      (2992 B -> 0x3DBB0)
    0x3DC00  state              (0xC8 -> 0x3DCC8)       min/max @ +0x14/+0x18

    0x3E000 .. 0x40000  reserved (future telemetry)
```

## Fixed telemetry tail (SMU-Patch lane, `0x3B480`)
`SmuMetricsTable_t` is output unchanged; the extra/expanded telemetry lives in a
FIXED-SIZE block at a FIXED address (constant layout independent of core count).
`Current.Spare == 0x67` is the "patched" discriminator.

| off | field |
|---|---|
| 0x00 | u32 magic (`0x38434F52` "ROC8") |
| 0x04 | u16 version |
| 0x06 | u16 core_count |
| 0x08 | u16 flags (bit0 = cores 6/7 valid) |
| 0x0A | u16 reserved |
| 0x0C | u16 CoreFrequency2[2] |
| 0x10 | u32 CorePower2[2] |
| 0x18 | u16 CoreTemperature2[2] |
| 0x1C | u16 C0Residency2[2] |
| 0x20..0x40 | reserved for future telemetry |

Core 6/7 slots are zeroed when absent. The tail is NOT part of the metrics
transfer (`smu_table->metrics_table` is `sizeof(SmuMetrics_t)` = 0xF4); a
consumer reads it from `0x3B480`.

## Tick slots (SMU tick array @0xC700 + n*4)
| slot | owner |
|---|---|
| 0x1E (0xC778) | CU-Unlock service |
| 0x1F (0xC77C) | Governor |
