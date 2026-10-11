/*
 * Scratch block for the metrics-patch entry glue: register save area + private
 * stack used while calling into C.
 *
 * SMU-SRAM allocation (documented free region 0x3AE00..0x40000):
 *   0x3AE00  Governor payload image        (see BC250-DXEv3-SMU-Governor)
 *   0x3C000  Governor state
 *   0x3C200  CU-Unlock payload image       (see BC250-DXEv3-CU-Unlock)
 *   0x3C4C0  CU-Unlock save/stack/state
 *   0x3D000  THIS payload image  (payload base 0x3D000, linked via linker.ld)
 *   0x3D200  THIS scratch block  (save/stack; kept above the payload image)
 * The scratch is deliberately NOT the payload base + a fixed small offset: the
 * current image already exceeds 0x100 bytes, so it must sit above the image.
 */
#ifndef BC250_SMU_PATCH_LAYOUT_H
#define BC250_SMU_PATCH_LAYOUT_H

#define SMU_PATCH_SCRATCH_ADDR   0x0003D400u
#define SMU_PATCH_SAVE_OFF       0x0000u      /* a0..a7 */
#define SMU_PATCH_FSAVE_OFF      0x0020u      /* f0..f15 (the fill lives in f0-f11) */
#define SMU_PATCH_STACK_OFF      0x0060u
#define SMU_PATCH_STACK_SIZE     0x0200u
#define SMU_PATCH_SAVE_ADDR      (SMU_PATCH_SCRATCH_ADDR + SMU_PATCH_SAVE_OFF)
#define SMU_PATCH_FSAVE_ADDR     (SMU_PATCH_SCRATCH_ADDR + SMU_PATCH_FSAVE_OFF)
#define SMU_PATCH_STACK_TOP      (SMU_PATCH_SCRATCH_ADDR + SMU_PATCH_STACK_OFF + SMU_PATCH_STACK_SIZE)

/* Firmware continuation after the hooked branch (0x29A1D + 3). */
#define SMU_PATCH_RESUME_ADDR    0x00029A20u
/* Restored value of a7 (the instruction the hook displaced: l32r a7,0xCA54). */
#define SMU_PATCH_A7_RESUME      0x0000CA54u

#endif /* BC250_SMU_PATCH_LAYOUT_H */
