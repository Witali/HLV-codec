/*
 * Bit-exact memset wrapper for esp_new_jpeg's coefficient clear.
 *
 * jpeg_dec_huffman clears one aligned int16_t[64] block before decoding it.
 * The generic memset call therefore writes exactly 128 zero bytes 1,800
 * times per 320x240 frame. Keep every other memset on the libc path.
 *
 * ABI:
 *   a2 = destination and return value
 *   a3 = byte value
 *   a4 = byte count
 */

    .section .iram1.text.mjpeg_memset_optimized, "ax", @progbits
    .align 4
    .global __wrap_memset
    .type __wrap_memset, @function

__wrap_memset:
    entry   a1, 32
    bnez    a3, .L_memset_fallback
    movi    a7, 128
    bne     a4, a7, .L_memset_fallback
    extui   a7, a2, 0, 2
    bnez    a7, .L_memset_fallback
    movi    a7, 0

    .macro ZERO_WORD offset
    s32i    a7, a2, \offset
    .endm

    ZERO_WORD 0
    ZERO_WORD 4
    ZERO_WORD 8
    ZERO_WORD 12
    ZERO_WORD 16
    ZERO_WORD 20
    ZERO_WORD 24
    ZERO_WORD 28
    ZERO_WORD 32
    ZERO_WORD 36
    ZERO_WORD 40
    ZERO_WORD 44
    ZERO_WORD 48
    ZERO_WORD 52
    ZERO_WORD 56
    ZERO_WORD 60
    ZERO_WORD 64
    ZERO_WORD 68
    ZERO_WORD 72
    ZERO_WORD 76
    ZERO_WORD 80
    ZERO_WORD 84
    ZERO_WORD 88
    ZERO_WORD 92
    ZERO_WORD 96
    ZERO_WORD 100
    ZERO_WORD 104
    ZERO_WORD 108
    ZERO_WORD 112
    ZERO_WORD 116
    ZERO_WORD 120
    ZERO_WORD 124
    retw.n

.L_memset_fallback:
    mov     a10, a2
    mov     a11, a3
    mov     a12, a4
    call8   __real_memset
    mov     a2, a10
    retw.n

    .size __wrap_memset, . - __wrap_memset
