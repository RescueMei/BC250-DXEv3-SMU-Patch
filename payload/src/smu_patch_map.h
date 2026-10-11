/*
 * Decoded SMU (Robin 3 / BIOS 3.00) addresses the metrics patch uses.
 *
 * SmuMetrics_t layout (driver smu11_driver_if_cyan_skillfish.h):
 *   SmuMetricsTable_t (0x74 bytes): Current @ 0xCB54, Average @ 0xCBC8
 *     0x00 CoreFrequency[6] (u16)   0x30 L3Frequency[2]    0x44 GfxclkFrequency
 *     0x0C CorePower[6]    (u32)    0x34 L3Temperature[2]  0x46 GfxTemperature
 *     0x24 CoreTemperature[6](u16)  0x38 C0Residency[6]    ...
 *     0x72 Spare
 *
 * Factory behavior is "fill exactly `count`, rest = 0".  The stock arrays are
 * 6 wide, so for >6 cores the patch clamps the stock fills at 6 (min(count,6))
 * and writes the extra cores into a FIXED-SIZE tail immediately after the whole
 * SmuMetrics_t.  The tail size is constant regardless of core count; when a
 * core slot has no data it is zeroed.
 *
 * Tail (SMU_TAIL_SIZE bytes, at sizeof(SmuMetrics_t)):
 *   0x00 u32 magic (= SMU_TAIL_MAGIC)
 *   0x04 u16 version
 *   0x06 u16 core_count        (actual CPU core count)
 *   0x08 u16 flags             (bit0 = cores 6/7 valid)
 *   0x0A u16 reserved0
 *   0x0C u16 CoreFrequency2[2]
 *   0x10 u32 CorePower2[2]
 *   0x18 u16 CoreTemperature2[2]
 *   0x1C u16 C0Residency2[2]
 *   0x20 .. SMU_TAIL_SIZE      reserved for future telemetry
 */
#ifndef BC250_SMU_PATCH_MAP_H
#define BC250_SMU_PATCH_MAP_H

#define SMU_METRICS_BASE      0x0000CB54u   /* SmuMetricsTable_t.Current */
#define SMU_SPARE_OFF         0x0072u
#define SMU_SPARE_MAGIC       0x67u
#define SMU_STOCK_CORES       6u
#define SMU_MAX_EXTRA_CORES   2u

/* Fixed-size tail, constant location independent of core count.  It lives in
 * free SMU SRAM (NOT contiguous with SmuMetrics_t: 0xCC48.. is live SMU data
 * that cannot be repurposed).  A consumer reads it at this fixed address,
 * gated by Current.Spare == SMU_SPARE_MAGIC. */
#define SMU_TAIL_BASE         0x0003B480u
#define SMU_TAIL_SIZE         0x0040u

#define SMU_TAIL_MAGIC        0x38434F52u   /* "ROC8" */
#define SMU_TAIL_VERSION      1u
#define SMU_TAIL_FLAG_CORES67 0x0001u

#define SMU_TAIL_MAGIC_OFF    0x0000u       /* u32 */
#define SMU_TAIL_VERSION_OFF  0x0004u       /* u16 */
#define SMU_TAIL_COUNT_OFF    0x0006u       /* u16 */
#define SMU_TAIL_FLAGS_OFF    0x0008u       /* u16 */
#define SMU_TAIL_FREQ_OFF     0x000Cu       /* u16 x2, MHz */
#define SMU_TAIL_POWER_OFF    0x0010u       /* u32 x2, mW */
#define SMU_TAIL_TEMP_OFF     0x0018u       /* u16 x2, centi-C */
#define SMU_TAIL_RESID_OFF    0x001Cu       /* u16 x2 */

/* Per-core source records (stride 0x178) and the core -> slot byte map. */
#define SMU_METRICS_SRC       0x0000FAC4u
#define SMU_METRICS_STRIDE    0x000178u
#define SMU_CORE_MAP          0x0000CC90u

/* Config struct; byte +0x1a = CPU core count (set from the present-CCX mask). */
#define SMU_CFG_BASE          0x00007AD8u
#define SMU_CFG_CORE_COUNT    0x001Au

/* Float scale factors applied by the stock fill (read from SRAM). */
#define SMU_SCALE_FREQ        0x00016ED4u
#define SMU_SCALE_TEMP        0x00016ED8u

/* Per-core source field offsets. */
#define SMU_SRC_FREQ_OFF      0x0140u
#define SMU_SRC_POWER_OFF     0x0010u
#define SMU_SRC_TEMP_OFF      0x001Cu
#define SMU_SRC_RESID_OFF     0x014Cu

#endif /* BC250_SMU_PATCH_MAP_H */
