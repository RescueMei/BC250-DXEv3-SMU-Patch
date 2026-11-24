/*
 * Loop-bound clamps: min(count, 6).
 *
 * The stock metrics fills load the per-core loop bound with
 *   l8ui a13, <cfg>, 26      ; a13 = *(0x7AD8 + 0x1A) = CPU core count
 * then loop while the index < a13, filling the 6-wide per-core arrays.  For
 * >6 cores that overruns (it clobbers Current.GfxclkFrequency).  The DXE driver
 * replaces each 3-byte bound load with "j smu_clamp_*", which sets
 * a13 = min(count, 6) and resumes at the instruction after the original site.
 * This reproduces factory behavior for <=6 (count iterations) and clamps to 6
 * for >6 (no overrun).
 */
#include "smu_patch_map.h"

    .section .text.smu_clamp_current, "ax"
    .literal_position
    .global smu_clamp_current
    .type   smu_clamp_current, @function
    .align 4
smu_clamp_current:
    l32r    a13, .Lcount
    l8ui    a13, a13, 0
    blti    a13, 7, 1f
    movi    a13, 6
1:  j       0x00029BE8
    .size   smu_clamp_current, . - smu_clamp_current

    .section .text.smu_clamp_average, "ax"
    .literal_position
    .global smu_clamp_average
    .type   smu_clamp_average, @function
    .align 4
smu_clamp_average:
    l32r    a13, .Lcount
    l8ui    a13, a13, 0
    blti    a13, 7, 1f
    movi    a13, 6
1:  j       0x00029A12
    .size   smu_clamp_average, . - smu_clamp_average

    .section .literal, "a"
    .align 4
.Lcount: .word (SMU_CFG_BASE + SMU_CFG_CORE_COUNT)
