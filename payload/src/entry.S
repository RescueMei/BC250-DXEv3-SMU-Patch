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
    /* the hooked fill runs with live floats in f0-f11; save all of them */
    ssi     f0,  a8, 32
    ssi     f1,  a8, 36
    ssi     f2,  a8, 40
    ssi     f3,  a8, 44
    ssi     f4,  a8, 48
    ssi     f5,  a8, 52
    ssi     f6,  a8, 56
    ssi     f7,  a8, 60
    ssi     f8,  a8, 64
    ssi     f9,  a8, 68
    ssi     f10, a8, 72
    ssi     f11, a8, 76
    ssi     f12, a8, 80
    ssi     f13, a8, 84
    ssi     f14, a8, 88
    ssi     f15, a8, 92

    l32r    a1, .Lstack
    call8   smu_patch_main

    l32r    a8, .Lsave
    lsi     f0,  a8, 32
    lsi     f1,  a8, 36
    lsi     f2,  a8, 40
    lsi     f3,  a8, 44
    lsi     f4,  a8, 48
    lsi     f5,  a8, 52
    lsi     f6,  a8, 56
    lsi     f7,  a8, 60
    lsi     f8,  a8, 64
    lsi     f9,  a8, 68
    lsi     f10, a8, 72
    lsi     f11, a8, 76
    lsi     f12, a8, 80
    lsi     f13, a8, 84
    lsi     f14, a8, 88
    lsi     f15, a8, 92
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

/* SMU table id-2 copier: program the DMA to copy the fixed tail into the
 * driver-table DRAM buffer (desc at a2).  Invoked from the Q0 msg-6 handler. */
    .section .text.smu_extra_entry, "ax"
    .literal_position
    .global smu_extra_entry
    .type   smu_extra_entry, @function
    .align  4
smu_extra_entry:
    entry   a1, 32
    l32r    a10, .Lextra_desc      /* driver-table descriptor (dst set by msg4/5) */
    l32r    a11, .Lextra_src       /* source = fixed tail */
    movi    a12, 0x40
    call8   0x3a850
    retw.n
    .size   smu_extra_entry, . - smu_extra_entry
    .section .literal, "a"
    .align  4
.Lextra_src:  .word 0x0003B480
.Lextra_desc: .word 0x0000CA38

/* Late hook (installed at the Current fill's last store, 0x29CCD): stamp
 * Current.Spare with the magic AFTER the Current fill has run, so the driver's
 * patched-SMU gate sees it.  Resumes at 0x29CD0. */
    .section .text.smu_spare_entry, "ax"
    .literal_position
    .global smu_spare_entry
    .type   smu_spare_entry, @function
    .align  4
smu_spare_entry:
    addi    a1, a1, -32
    s32i    a8,  a1, 0
    s32i    a9,  a1, 4
    s32i    a10, a1, 8
    s32i    a11, a1, 12
    s32i    a12, a1, 16
    movi    a8, 0x67
    l32r    a9, .Lreloc_spare
    s16i    a8, a9, 0
    l32r    a9, .Ltail_addr
    l32r    a10, .Lreloc_extra
    l32i    a8, a9, 0x0C
    s32i    a8, a10, 0
    l32i    a8, a9, 0x10
    s32i    a8, a10, 4
    l32i    a8, a9, 0x14
    s32i    a8, a10, 8
    l32i    a12, a1, 16
    l32i    a11, a1, 12
    l32i    a10, a1, 8
    l32i    a9,  a1, 4
    l32i    a8,  a1, 0
    addi    a1, a1, 32
    s8i     a13, a1, 12
    j       0x29CBB
    .size   smu_spare_entry, . - smu_spare_entry
    .section .literal, "a"
    .align  4
.Lspare_addr:  .word 0x0000CBC6
.Ltail_addr:   .word 0x0003B480
.Lcontig_addr: .word 0x0000CC3C
.Lreloc_spare: .word 0x0003B572
.Lreloc_extra: .word 0x0003B5E8
