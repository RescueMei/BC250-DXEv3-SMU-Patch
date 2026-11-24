#include "sram.h"
#include "smu_patch_map.h"

void smu_patch_main(void)
{
    unsigned count = REG(unsigned char, SMU_CFG_BASE + SMU_CFG_CORE_COUNT);
    float    sf    = REG(float, SMU_SCALE_FREQ);
    float    st    = REG(float, SMU_SCALE_TEMP);
    unsigned extra;
    unsigned i;

    REG(unsigned short, SMU_METRICS_RELOC + SMU_SPARE_OFF) = SMU_SPARE_MAGIC;

    if (count <= SMU_STOCK_CORES)
        return;

    extra = count - SMU_STOCK_CORES;
    if (extra > SMU_MAX_EXTRA_CORES)
        extra = SMU_MAX_EXTRA_CORES;

    for (i = 0; i < extra; i++) {
        unsigned idx  = SMU_STOCK_CORES + i;
        unsigned slot = REG(unsigned char, SMU_CORE_MAP + idx);
        unsigned src  = SMU_METRICS_SRC + slot * SMU_METRICS_STRIDE;
        unsigned f    = (unsigned)(REG(float, src + SMU_SRC_FREQ_OFF)  * sf);
        unsigned p    = (unsigned)(REG(float, src + SMU_SRC_POWER_OFF) * sf);
        unsigned t    = (unsigned)(REG(float, src + SMU_SRC_TEMP_OFF)  * st);
        REG(unsigned short, SMU_METRICS_RELOC + SMU_RELOC_EXTRA_OFF + i * 2u)      = (unsigned short)f;
        REG(unsigned short, SMU_METRICS_RELOC + SMU_RELOC_EXTRA_OFF + 4u + i * 2u) = (unsigned short)p;
        REG(unsigned short, SMU_METRICS_RELOC + SMU_RELOC_EXTRA_OFF + 8u + i * 2u) = (unsigned short)t;
    }
}
