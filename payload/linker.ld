/*
 * Link the SMU metrics patch payload at the documented free SMU-SRAM region.
 * The DXE driver verifies the region is all-zero, injects the raw image, and
 * installs the manual hooks.  Literals precede the code (l32r pool).
 *
 * Base 0x3D000 sits above the Governor payload (0x3AE00..0x3B9B0, state
 * 0x3C000) and the CU-Unlock payload (0x3C200..0x3C470, block ..0x3C670), so
 * all three drivers coexist.  Scratch is at 0x3D200 (smu_patch_layout.h).
 */
ENTRY(smu_patch_entry)

SECTIONS
{
    . = 0x3D000;

    .text : {
        *(.literal)
        *(.literal.*)
        KEEP(*(.text.smu_patch_entry))
        *(.text)
        *(.text.*)
    }
    .rodata : ALIGN(4) { *(.rodata) *(.rodata.*) }
    .data   : ALIGN(4) { *(.data)   *(.data.*) }
    .bss    : ALIGN(4) { *(.bss) *(.bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.xtensa.info) *(.xt.prop*) }
}
