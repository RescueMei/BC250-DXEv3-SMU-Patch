/*
 * Decoded SMU (Robin 3 / BIOS 3.00) addresses the metrics patch uses.
 *
 * SmuMetrics_t layout (driver smu11_driver_if_cyan_skillfish.h):
 *   SmuMetricsTable_t (0x74 bytes): Current @ 0xCB54, Average @ 0xCBC8.
 *     0x00 CoreFrequency[6] (u16)   0x30 L3Frequency[2]    0x44 GfxclkFrequency
 *     0x0C CorePower[6]    (u32)    0x34 L3Temperature[2]  0x46 GfxTemperature
 *     0x24 CoreTemperature[6](u16)  0x38 C0Residency[6]    ...
 *     0x72 Spare
 *
 * The stock per-core arrays are 6-wide.  For 8 cores we output the stock
 * struct unchanged for cores 0..5 (both fill loops capped at 6), put the magic
 * in Current.Spare, and append cores 6/7 into the expanded block immediately
 * after Spare (Current+0x74/+0x80/+0x98/+0xAC, mirroring the stock offsets).
 */
#ifndef BC250_SMU_PATCH_MAP_H
#define BC250_SMU_PATCH_MAP_H

#define SMU_METRICS_BASE      0x0000CB54u   /* SmuMetricsTable_t.Current */

/* Per-core source records (stride 0x178) and the core -> slot byte map. */
#define SMU_METRICS_SRC       0x0000FAC4u
#define SMU_METRICS_STRIDE    0x000178u
#define SMU_CORE_MAP          0x0000CC90u

/* Config struct; byte +0x1a = CPU core count (set from the present-CCX mask). */
#define SMU_CFG_BASE          0x00007AD8u
#define SMU_CFG_CORE_COUNT    0x001Au

/* Float scale factors applied by the stock fill (read from SRAM). */
#define SMU_SCALE_FREQ        0x00016ED4u   /* freq/power scale */
#define SMU_SCALE_TEMP        0x00016ED8u   /* temp/residency scale */

#define SMU_SPARE_OFF         0x0072u
#define SMU_SPARE_MAGIC       0x67u
#define SMU_STOCK_CORES       6u
#define SMU_MAX_EXTRA_CORES   2u

/* Expanded block right after Current.Spare (stock per-core offsets + 0x74). */
#define SMU_TAIL_FREQ_OFF     0x0074u       /* u16 x2, MHz */
#define SMU_TAIL_POWER_OFF    0x0080u       /* u32 x2, mW */
#define SMU_TAIL_TEMP_OFF     0x0098u       /* u16 x2, centi-C */
#define SMU_TAIL_RESID_OFF    0x00ACu       /* u16 x2 */

/* Per-core source field offsets. */
#define SMU_SRC_FREQ_OFF      0x0140u
#define SMU_SRC_POWER_OFF     0x0010u
#define SMU_SRC_TEMP_OFF      0x001Cu
#define SMU_SRC_RESID_OFF     0x014Cu

#endif /* BC250_SMU_PATCH_MAP_H */
