/*
 * Scratch block for the metrics-patch entry glue: register save area + private
 * stack used while calling into C.  Placed just above the payload image
 * (payload base 0x3B000, scratch at 0x3B100), inside the documented free
 * region (0x3AE00+).
 */
#ifndef BC250_SMU_PATCH_LAYOUT_H
#define BC250_SMU_PATCH_LAYOUT_H

#define SMU_PATCH_SCRATCH_ADDR   0x0003B100u
#define SMU_PATCH_SAVE_OFF       0x0000u      /* a0..a7 */
#define SMU_PATCH_STACK_OFF      0x0040u
#define SMU_PATCH_STACK_SIZE     0x0100u
#define SMU_PATCH_SAVE_ADDR      (SMU_PATCH_SCRATCH_ADDR + SMU_PATCH_SAVE_OFF)
#define SMU_PATCH_STACK_TOP      (SMU_PATCH_SCRATCH_ADDR + SMU_PATCH_STACK_OFF + SMU_PATCH_STACK_SIZE)

/* Firmware continuation after the hooked branch (0x29A1D + 3). */
#define SMU_PATCH_RESUME_ADDR    0x00029A20u
/* Restored value of a7 (the instruction the hook displaced: l32r a7,0xCA54). */
#define SMU_PATCH_A7_RESUME      0x0000CA54u

#endif /* BC250_SMU_PATCH_LAYOUT_H */
