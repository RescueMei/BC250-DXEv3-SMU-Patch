/*
 * Decoded SMU (Robin 3 / BIOS 3.00) addresses the metrics patch uses.
 * All are SMU-local SRAM offsets (== firmware image offsets).
 */
#ifndef BC250_SMU_PATCH_MAP_H
#define BC250_SMU_PATCH_MAP_H

/*
 * SmuMetrics_t.Current is only 0x74 bytes (0xCBC8..0xCC3C), but the SMU's
 * internal metrics block is 0xF4 bytes (0xCB54..0xCC48) and is IMMEDIATELY
 * followed by live firmware data:
 *   0xCC48  device/topology descriptor (symbol s_AMD_BC_250): name "AMD BC-250"
 *           at +0x00, flags at +0x18/+0x1C/+0x2C/+0x30/+0x34/+0x38/+0x40
 *   0xCC90  the descriptor's core->slot byte map (+0x48), also read by the
 *           stock metrics fill as PTR_DAT_00017f38
 *   0xCCA8  a function-pointer table
 * There is therefore NO spare room to extend Current in place: writing a tail
 * at Current+0x80.. clobbers the descriptor.  The extra cores are published to
 * a dedicated free-SRAM block (SMU_EXTRA_BASE) instead; Current.Spare still
 * carries SMU_SPARE_MAGIC as the "patched" marker.
 */
#define SMU_METRICS_BASE      0x0000CBC8u   /* SmuMetrics_t.Current (= 0xCB54+0x74) */

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

/* Extra-core block in the free SMU-SRAM region (after the payload @0x3D000 and
 * its scratch @0x3D200..0x3D340).  Not part of SmuMetrics_t; a consumer reads
 * it at this fixed address, gated by SMU_SPARE_MAGIC in Current.Spare. */
#define SMU_EXTRA_BASE        0x0003D400u
#define SMU_EXTRA_MAGIC       0x38434F52u   /* "ROC8" = 8-core extended metrics */
#define SMU_EXTRA_FREQ_OFF    0x0004u       /* u16 x2, abs MHz */
#define SMU_EXTRA_POWER_OFF   0x0008u       /* u32 x2, mW */
#define SMU_EXTRA_TEMP_OFF    0x0010u       /* u16 x2, centi-C */
#define SMU_EXTRA_RESID_OFF   0x0014u       /* u16 x2 */

/* Per-core source field offsets. */
#define SMU_SRC_FREQ_OFF      0x0140u
#define SMU_SRC_POWER_OFF     0x0010u
#define SMU_SRC_TEMP_OFF      0x001Cu
#define SMU_SRC_RESID_OFF     0x014Cu

#endif /* BC250_SMU_PATCH_MAP_H */
