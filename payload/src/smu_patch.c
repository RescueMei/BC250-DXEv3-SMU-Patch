/*
 * BC-250 SMU metrics 8-core patch - native Xtensa payload.
 *
 * The DXE driver hooks the firmware GPU-metrics fill right after its per-core
 * loop and jumps here.  Both stock fill loops (Current FUN_00029aec and
 * Average FUN_00029848) are capped at 6 cores by manual hooks in the DXE
 * driver, so the 6-wide stock arrays are filled correctly with no overrun.
 *
 * This routine then:
 *   1. stamps Spare (Current + 0x72) with the magic 0x67, and
 *   2. emits the extra CPU cores (indices 6..count-1, i.e. the cores the
 *      8-core unlock enables) into the expanded block immediately after
 *      Spare, mirroring the stock per-core layout (+0x74/+0x80/+0x98/+0xAC),
 *      so the stock SmuMetricsTable_t is output unchanged and the extra cores
 *      follow it.
 *
 * Integer/float arithmetic mirrors FUN_00029aec: each source field is a float
 * scaled by a per-domain factor read from SRAM, then truncated to an integer.
 */
#include "sram.h"
#include "smu_patch_map.h"

void smu_patch_main(void)
{
    unsigned count = REG(unsigned char, SMU_CFG_BASE + SMU_CFG_CORE_COUNT);
    float    sf    = REG(float, SMU_SCALE_FREQ);
    float    st    = REG(float, SMU_SCALE_TEMP);
    unsigned extra;
    unsigned i;

    /* Magic marker: proves the patch ran and gates the expanded block. */
    REG(unsigned short, SMU_METRICS_BASE + SMU_SPARE_OFF) = SMU_SPARE_MAGIC;

    if (count <= SMU_STOCK_CORES) {
        return;
    }
    extra = count - SMU_STOCK_CORES;
    if (extra > SMU_MAX_EXTRA_CORES) {
        extra = SMU_MAX_EXTRA_CORES;
    }

    for (i = 0; i < extra; i++) {
        unsigned idx  = SMU_STOCK_CORES + i;
        unsigned slot = REG(unsigned char, SMU_CORE_MAP + idx);
        unsigned src  = SMU_METRICS_SRC + slot * SMU_METRICS_STRIDE;
        unsigned f    = (unsigned)(REG(float, src + SMU_SRC_FREQ_OFF)  * sf);
        unsigned p    = (unsigned)(REG(float, src + SMU_SRC_POWER_OFF) * sf);
        unsigned t    = (unsigned)(REG(float, src + SMU_SRC_TEMP_OFF)  * st);
        unsigned r    = (unsigned)(REG(float, src + SMU_SRC_RESID_OFF) * st);
        REG(unsigned short, SMU_METRICS_BASE + SMU_TAIL_FREQ_OFF  + i * 2u) = (unsigned short)f;
        REG(unsigned,       SMU_METRICS_BASE + SMU_TAIL_POWER_OFF + i * 4u) = p;
        REG(unsigned short, SMU_METRICS_BASE + SMU_TAIL_TEMP_OFF  + i * 2u) = (unsigned short)t;
        REG(unsigned short, SMU_METRICS_BASE + SMU_TAIL_RESID_OFF + i * 2u) = (unsigned short)r;
    }
}
