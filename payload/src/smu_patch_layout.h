/*
 * Scratch block for the metrics-patch entry glue: register save area + private
 * stack used while calling into C.
 *
 * SMU-SRAM allocation (canonical map in the shared MeiMeiDXEv3SmuMap.h):
 *   0x3AE00  THIS payload image        (lane 0x3AE00..0x3C000)
 *   0x3C000  CU-Unlock payload image
 *   0x3C400  CU-Unlock save/stack/state
 *   0x3C600  SMU Governor state
 *   0x3D000  SMU Governor payload image
 *   0x3E000  SMU firmware; do not use
 * The scratch block below stays inside this payload's own lane.
 */
#ifndef BC250_SMU_PATCH_LAYOUT_H
#define BC250_SMU_PATCH_LAYOUT_H

#define SMU_PATCH_SCRATCH_ADDR   0x0003B200u
#define SMU_PATCH_SAVE_OFF       0x0000u      /* a0..a15 */
#define SMU_PATCH_FSAVE_OFF      0x0040u      /* f0..f15 (the fill lives in f0-f11) */
#define SMU_PATCH_STACK_OFF      0x0080u
#define SMU_PATCH_STACK_SIZE     0x01e0u
#define SMU_PATCH_SAVE_ADDR      (SMU_PATCH_SCRATCH_ADDR + SMU_PATCH_SAVE_OFF)
#define SMU_PATCH_FSAVE_ADDR     (SMU_PATCH_SCRATCH_ADDR + SMU_PATCH_FSAVE_OFF)
#define SMU_PATCH_STACK_TOP      (SMU_PATCH_SCRATCH_ADDR + SMU_PATCH_STACK_OFF + SMU_PATCH_STACK_SIZE)

/* Firmware continuation after the hooked branch (0x29A1D + 3). */
#define SMU_PATCH_RESUME_ADDR    0x00029A20u
/* Restored value of a7 (the instruction the hook displaced: l32r a7,0xCA54). */
#define SMU_PATCH_A7_RESUME      0x0000CA54u

#endif /* BC250_SMU_PATCH_LAYOUT_H */
