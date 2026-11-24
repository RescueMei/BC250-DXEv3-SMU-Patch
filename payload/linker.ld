/*
 * Link the SMU metrics patch payload at the documented free SMU-SRAM region.
 * The DXE driver verifies the region is all-zero, injects the raw image, and
 * installs the manual hooks.  Literals precede the code (l32r pool).
 *
 * Base 0x3AE00 is this payload's lane in the canonical shared map
 * (0x3AE00..0x3C000, shared MeiMeiDXEv3SmuMap.h).  Above it live the CU-Unlock
 * payload (0x3C000), CU state (0x3C400), governor state (0x3C600) and governor
 * payload (0x3D000); 0x3E000 is SMU firmware.  Scratch is at 0x3B200
 * (smu_patch_layout.h), inside this lane.
 */
ENTRY(smu_patch_entry)

SECTIONS
{
    . = 0x3AE00;

    .text : {
        *(.literal)
        *(.literal.*)
        KEEP(*(.text.smu_patch_entry))
        KEEP(*(.text.smu_spare_entry))
        KEEP(*(.text.smu_extra_entry))
        KEEP(*(.text.smu_clamp_current))
        KEEP(*(.text.smu_clamp_average))
        *(.text)
        *(.text.*)
    }
    .rodata : ALIGN(4) { *(.rodata) *(.rodata.*) }
    .data   : ALIGN(4) { *(.data)   *(.data.*) }
    .bss    : ALIGN(4) { *(.bss) *(.bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.xtensa.info) *(.xt.prop*) }
}
