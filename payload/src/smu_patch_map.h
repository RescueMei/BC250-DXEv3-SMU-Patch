/*
 * Decoded SMU (Robin 3 / BIOS 3.00) addresses the metrics patch uses.
 * All are SMU-local SRAM offsets (== firmware image offsets).
 */
#ifndef BC250_SMU_PATCH_MAP_H
#define BC250_SMU_PATCH_MAP_H

/* SmuMetrics_t.Current lives at Average(0xCB54) + sizeof(SmuMetrics_t)(0x74). */
#define SMU_METRICS_BASE      0x0000CBC8u

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

/* Current-table field offsets. */
#define SMU_SPARE_OFF         0x0072u
#define SMU_SPARE_MAGIC       0x67u
#define SMU_STOCK_CORES       6u
#define SMU_MAX_EXTRA_CORES   2u

/* Tail block (mirrors the stock layout at +0x74). */
#define SMU_TAIL_FREQ_OFF     0x0074u
#define SMU_TAIL_POWER_OFF    0x0080u
#define SMU_TAIL_TEMP_OFF     0x0098u
#define SMU_TAIL_RESID_OFF    0x00ACu

/* Per-core source field offsets. */
#define SMU_SRC_FREQ_OFF      0x0140u
#define SMU_SRC_POWER_OFF     0x0010u
#define SMU_SRC_TEMP_OFF      0x001Cu
#define SMU_SRC_RESID_OFF     0x014Cu

#endif /* BC250_SMU_PATCH_MAP_H */
