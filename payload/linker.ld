/*
 * Link the SMU metrics patch payload at the documented free SMU-SRAM region.
 * The DXE driver verifies the region is all-zero, injects the raw image, and
 * installs the manual hooks.  Literals precede the code (l32r pool).
 */
ENTRY(smu_patch_entry)

SECTIONS
{
    . = 0x3B000;

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
