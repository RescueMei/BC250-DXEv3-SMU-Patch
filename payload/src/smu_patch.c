/*
 * BC-250 SMU metrics 8-core patch - native Xtensa payload.
 *
 * Factory behavior is "fill exactly `count`, rest = 0".  The stock per-core
 * arrays are 6 wide, so:
 *   - for count <= 6 the stock fills run naturally (count iterations);
 *   - for count > 6 the manual clamp hooks turn each fill's loop bound into
 *     min(count, 6), so the stock arrays are still filled with exactly 6 rows
 *     and never overrun (the overrun is what clobbered Current.GfxclkFrequency).
 *
 * This routine then writes a FIXED-SIZE tail immediately after the whole
 * SmuMetrics_t (see smu_patch_map.h).  The tail size is constant regardless of
 * core count; it is fully zeroed first, then:
 *   - Current.Spare is stamped with the patch magic (stock-vs-patched marker),
 *   - the tail header (magic/version/core_count/flags) is written,
 *   - when count > 6 the extra cores 6/7 are emitted (flags bit0 set),
 *   - otherwise the extra-core slots stay zeroed.
 *
 * Integer/float arithmetic mirrors FUN_00029aec.
 */
#include "sram.h"
#include "smu_patch_map.h"

extern void smu_extra_entry(void);   /* id-2 copier stub (entry.S) */

void smu_patch_main(void)
{
    unsigned count = REG(unsigned char, SMU_CFG_BASE + SMU_CFG_CORE_COUNT);
    float    sf    = REG(float, SMU_SCALE_FREQ);
    float    st    = REG(float, SMU_SCALE_TEMP);
    unsigned extra;
    unsigned i;

    /* Stock-vs-patched marker in the stock struct (unchanged size). */
    REG(unsigned short, SMU_METRICS_BASE + SMU_SPARE_OFF) = SMU_SPARE_MAGIC;

#if 0 /* table reg unsupported */
    /* Register SMU table id 2 as a second transferable table carrying the
     * fixed tail (the driver reads it only when Spare == magic). */
    REG(unsigned,      SMU_TBL_HANDLER(SMU_TBL_EXT_ID)) = (unsigned)(unsigned long)smu_extra_entry;
    REG(unsigned,      SMU_TBL_FLAG(SMU_TBL_EXT_ID))    = 0xFFFFFFFFu;
    REG(unsigned char, SMU_TBL_DESC(SMU_TBL_EXT_ID) + 11u) = 3u;   /* mark valid */
#endif

#ifndef SMU_PATCH_NO_TAIL
    /* Zero the whole fixed tail so reserved space / unused slots are 0. */
    for (i = 0; i < SMU_TAIL_SIZE / 4u; i++) {
        REG(unsigned, SMU_TAIL_BASE + i * 4u) = 0;
    }

    /* Tail header. */
    REG(unsigned,       SMU_TAIL_BASE + SMU_TAIL_MAGIC_OFF)   = SMU_TAIL_MAGIC;
    REG(unsigned short, SMU_TAIL_BASE + SMU_TAIL_VERSION_OFF) = SMU_TAIL_VERSION;
    REG(unsigned short, SMU_TAIL_BASE + SMU_TAIL_COUNT_OFF)   = (unsigned short)count;

    if (count <= SMU_STOCK_CORES) {
        return;                 /* flags 0, extra-core slots zeroed */
    }

    extra = count - SMU_STOCK_CORES;
    if (extra > SMU_MAX_EXTRA_CORES) {
        extra = SMU_MAX_EXTRA_CORES;
    }
    REG(unsigned short, SMU_TAIL_BASE + SMU_TAIL_FLAGS_OFF) = SMU_TAIL_FLAG_CORES67;

#ifndef SMU_PATCH_NO_CORES
    for (i = 0; i < extra; i++) {
        unsigned idx  = SMU_STOCK_CORES + i;
        unsigned slot = REG(unsigned char, SMU_CORE_MAP + idx);
        unsigned src  = SMU_METRICS_SRC + slot * SMU_METRICS_STRIDE;
#ifndef SMU_PATCH_NOSRC
        unsigned f    = (unsigned)(REG(float, src + SMU_SRC_FREQ_OFF)  * sf);
        unsigned p    = (unsigned)(REG(float, src + SMU_SRC_POWER_OFF) * sf);
        unsigned t    = (unsigned)(REG(float, src + SMU_SRC_TEMP_OFF)  * st);
        unsigned r    = (unsigned)(REG(float, src + SMU_SRC_RESID_OFF) * st);
        REG(unsigned short, SMU_TAIL_BASE + SMU_TAIL_FREQ_OFF  + i * 2u) = (unsigned short)f;
        REG(unsigned short, SMU_TAIL_BASE + SMU_TAIL_POWER_OFF + i * 2u) = (unsigned short)p;
        REG(unsigned short, SMU_TAIL_BASE + SMU_TAIL_TEMP_OFF  + i * 2u) = (unsigned short)t;
        REG(unsigned short, SMU_TAIL_BASE + SMU_TAIL_RESID_OFF + i * 2u) = (unsigned short)r;
#else
        (void)src;
        REG(unsigned short, SMU_TAIL_BASE + SMU_TAIL_FREQ_OFF + i * 2u) = (unsigned short)slot;
#endif
    }
#else
    (void)sf; (void)st;
#endif
#else
    (void)count; (void)extra; (void)i;
#endif
}

/* Late hook (runs in the Current fill just before its own transfer):
 * stamp Current.Spare, and emit the extra cores into the transferred metrics'
 * trailing 12 unused bytes (SampleStartTime/SampleStopTime/Accnt). */

