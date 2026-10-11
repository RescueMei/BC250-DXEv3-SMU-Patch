    .section .text, "ax"
    .align 4
.Lscf:  .word 0x00016ed4
.Lsct:  .word 0x00016ed8
.Lmap:  .word 0x0000cc90
.Lsrc:  .word 0x0000fac4
.Lca54: .word 0x0000ca54
    .literal_position
    .align 4
    .global patch_metrics
patch_metrics:
    movi    a8, 0x67
    s16i    a8, a4, 0xe6
    movi    a3, 6
.Lloop:
    l32r    a5, .Lmap
    add.n   a5, a3, a5
    l8ui    a5, a5, 0
    l32r    a6, .Lsrc
    movi    a7, 0x178
    mul16s  a7, a5, a7
    add.n   a6, a6, a7
    movi    a2, 0x68
    add.n   a2, a2, a4
    addx2   a2, a3, a2
    movi    a9, 0x5c
    add.n   a9, a9, a4
    addx4   a9, a3, a9
    l32r    a14, .Lscf
    lsi     f0, a14, 0
    lsi     f1, a6, 0x140
    mul.s   f1, f1, f0
    utrunc.s a10, f1, 0
    s16i    a10, a2, 0x74
    lsi     f5, a6, 0x10
    mul.s   f5, f5, f0
    utrunc.s a10, f5, 0
    s32i    a10, a9, 0x80
    l32r    a14, .Lsct
    lsi     f2, a14, 0
    lsi     f3, a6, 0x1c
    mul.s   f3, f3, f2
    utrunc.s a10, f3, 0
    s16i    a10, a2, 0x98
    lsi     f4, a6, 0x14c
    mul.s   f4, f4, f2
    utrunc.s a10, f4, 0
    s16i    a10, a2, 0xac
    addi.n  a3, a3, 1
    movi    a8, 8
    blt     a3, a8, .Lloop
    l32r    a7, .Lca54
    j       0x29a20
