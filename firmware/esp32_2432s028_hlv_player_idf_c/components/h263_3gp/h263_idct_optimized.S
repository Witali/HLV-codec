/*
 * DC-only H.263 intra 8x8 IDCT output kernel for the ESP32.
 *
 * This exactly combines idctcol1() and idctrow1_intra(): the 16-bit column
 * result wraps as before, each consumed coefficient is cleared, and the
 * clipped byte is written to all 64 output samples.
 *
 * ABI:
 *   a2 = int16_t coefficients[64]
 *   a3 = uint8_t destination[]
 *   a4 = destination stride in bytes
 */

    .section .iram1.text.h263_idct_dc_intra, "ax", @progbits
    .align 4
    .global pv_h263_idct_dc_intra
    .type pv_h263_idct_dc_intra, @function

pv_h263_idct_dc_intra:
    entry   a1, 16

    /* Reproduce the int16_t assignment in idctcol1(), including wrap. */
    l16si   a5, a2, 0
    slli    a5, a5, 3
    slli    a5, a5, 16
    srai    a5, a5, 16
    addi    a5, a5, 32
    srai    a5, a5, 6

    movi    a6, 0
    blt     a5, a6, .L_h263_dc_clipped
    movi    a6, 255
    blt     a6, a5, .L_h263_dc_clipped
    mov     a6, a5

.L_h263_dc_clipped:
    /*
     * For a negative value a6 is already zero; for a value above 255 it is
     * already 255. Duplicate the clipped sample into a 32-bit output word.
     */
    slli    a5, a6, 8
    or      a6, a6, a5
    slli    a5, a6, 16
    or      a6, a6, a5
    movi    a7, 8
    movi    a8, 0

.L_h263_dc_fill_row:
    s16i    a8, a2, 0
    s32i    a6, a3, 0
    s32i    a6, a3, 4
    addi    a2, a2, 16
    add     a3, a3, a4
    addi    a7, a7, -1
    bnez    a7, .L_h263_dc_fill_row
    retw.n

    .size pv_h263_idct_dc_intra, . - pv_h263_idct_dc_intra
