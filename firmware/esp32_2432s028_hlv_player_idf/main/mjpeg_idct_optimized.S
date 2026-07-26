/*
 * Bit-exact ESP32 replacement for ESP_NEW_JPEG's idct_block_8_8.
 *
 * The original Xtensa kernel already skips AC-empty columns, but it always
 * executes the complete row transform. This wrapper recognizes a completely
 * DC-only block before that transform, calculates the identical rounded and
 * clipped sample, and fills the 8x8 destination with 32-bit stores. All other
 * blocks use an ordinary windowed call to the linker-wrapped original
 * implementation.
 *
 * ABI:
 *   a2 = int16_t coefficients[64]
 *   a3 = block width (8; retained for the original ABI)
 *   a4 = int16_t quantization table[64]
 *   a5 = uint16_t destination[]
 *   a6 = destination stride in uint16_t elements
 */

    .section .iram1.literal.idct_block_8_8_optimized, "a", @progbits
    .align 4
.L_idct_lips:
    .word lips

    .section .iram1.text.idct_block_8_8_optimized, "ax", @progbits
    .align 4
    .global __wrap_idct_block_8_8
    .type __wrap_idct_block_8_8, @function

__wrap_idct_block_8_8:
    entry   a1, 32

    /*
     * Check common low-frequency locations first so ordinary AC-containing
     * blocks reach the original kernel after only one or two loads.
     * Every remaining pair is still checked before taking the DC-only path.
     */
    l16si   a7, a2, 2
    bnez    a7, .L_idct_fallback

    .macro CHECK_AC_PAIR offset
    l32i    a7, a2, \offset
    bnez    a7, .L_idct_fallback
    .endm

    CHECK_AC_PAIR 16
    CHECK_AC_PAIR 32
    CHECK_AC_PAIR 4
    CHECK_AC_PAIR 20
    CHECK_AC_PAIR 48
    CHECK_AC_PAIR 64
    CHECK_AC_PAIR 36
    CHECK_AC_PAIR 8
    CHECK_AC_PAIR 12
    CHECK_AC_PAIR 24
    CHECK_AC_PAIR 28
    CHECK_AC_PAIR 40
    CHECK_AC_PAIR 44
    CHECK_AC_PAIR 52
    CHECK_AC_PAIR 56
    CHECK_AC_PAIR 60
    CHECK_AC_PAIR 68
    CHECK_AC_PAIR 72
    CHECK_AC_PAIR 76
    CHECK_AC_PAIR 80
    CHECK_AC_PAIR 84
    CHECK_AC_PAIR 88
    CHECK_AC_PAIR 92
    CHECK_AC_PAIR 96
    CHECK_AC_PAIR 100
    CHECK_AC_PAIR 104
    CHECK_AC_PAIR 108
    CHECK_AC_PAIR 112
    CHECK_AC_PAIR 116
    CHECK_AC_PAIR 120
    CHECK_AC_PAIR 124

    /*
     * This is the exact DC-only result of the original two-pass kernel:
     * clip[((coefficient[0] * quant[0] + 0x1000) >> 5) & 0x3ff].
     */
    l16si   a7, a2, 0
    l16si   a8, a4, 0
    mull    a7, a7, a8
    addmi   a7, a7, 0x1000
    extui   a7, a7, 5, 10
    l32r    a8, .L_idct_lips
    add     a8, a8, a7
    l8ui    a7, a8, 0

    /* Duplicate the 8-bit sample into two uint16_t output elements. */
    slli    a8, a7, 16
    or      a7, a7, a8
    movi    a8, 8

.L_idct_fill_row:
    s32i    a7, a5, 0
    s32i    a7, a5, 4
    s32i    a7, a5, 8
    s32i    a7, a5, 12
    addx2   a5, a6, a5
    addi    a8, a8, -1
    bnez    a8, .L_idct_fill_row
    retw.n

.L_idct_fallback:
    mov     a10, a2
    mov     a11, a3
    mov     a12, a4
    mov     a13, a5
    mov     a14, a6
    call8   __real_idct_block_8_8
    retw.n

    .size __wrap_idct_block_8_8, . - __wrap_idct_block_8_8
