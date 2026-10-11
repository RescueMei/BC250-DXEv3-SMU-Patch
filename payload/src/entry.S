/*
 * Entry glue for the SMU metrics patch.
 *
 * The DXE driver patches the branch at SMU 0x29A1D ("l32r a7, 0xCA54") with a
 * jump to smu_patch_entry.  The fill's live a0-a7 are saved to the scratch
 * block, a private stack is installed, the C smu_patch_main() runs, a0-a6 are
 * restored, a7 is reloaded with the value the displaced instruction would have
 * set, and control resumes at 0x29A20.
 *
 * Manual (non-compilable) hooks, applied by the DXE driver:
 *   0x29A10  A0 06   ->  movi a13, 6      (cap the stock per-core loop at 6)
 *   0x29A1D  j 0x3D000   (jump to smu_patch_entry; 3-byte encoding derived
 *                         from the payload link base)
 * The second edit's encoding depends on the payload link base and is verified
 * by payload/build.sh (see SmuPatchMeta.h).
 */
#include "smu_patch_layout.h"

    .section .literal, "a"
    .align 4
.Lsave:  .word SMU_PATCH_SAVE_ADDR
.Lstack: .word SMU_PATCH_STACK_TOP
.La7:    .word SMU_PATCH_A7_RESUME

    .section .text.smu_patch_entry, "ax"
    .literal_position
    .global smu_patch_entry
    .type   smu_patch_entry, @function
    .align 4
smu_patch_entry:
    l32r    a8, .Lsave
    s32i    a0, a8, 0
    s32i    a1, a8, 4
    s32i    a2, a8, 8
    s32i    a3, a8, 12
    s32i    a4, a8, 16
    s32i    a5, a8, 20
    s32i    a6, a8, 24
    s32i    a7, a8, 28

    l32r    a1, .Lstack
    call8   smu_patch_main

    l32r    a8, .Lsave
    l32i    a0, a8, 0
    l32i    a1, a8, 4
    l32i    a2, a8, 8
    l32i    a3, a8, 12
    l32i    a4, a8, 16
    l32i    a5, a8, 20
    l32i    a6, a8, 24
    l32r    a7, .La7
    j       SMU_PATCH_RESUME_ADDR
    .size   smu_patch_entry, . - smu_patch_entry
