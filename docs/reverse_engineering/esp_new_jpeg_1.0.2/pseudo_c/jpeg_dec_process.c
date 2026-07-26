/* ESP_NEW_JPEG 1.0.2 ESP32 decoder Ghidra pseudo-C.
 * Derived from the Espressif binary; see LICENSE.ESPRESSIF in the analysis directory.
 * Program: jpeg_dec_process.c.obj
 * Language: Xtensa:LE:32:default
 * Types and names are reconstructed and must not be treated as the original source.
 */

/* ==================================================================
 * jpeg_dec_proc_gray_0
 * Purpose: Decodes JPEG MCUs for grayscale Y without rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000105b4
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_gray_0(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  uint8_t *puVar7;
  int iVar8;
  int iVar9;
  uint uVar10;
  uint uVar11;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar11 = (uint)jd->height;
  if (uVar11 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar7 = io->outbuf + jd->out_start_pos;
    uVar10 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar9 = 0;
    do {
      iVar8 = 0;
      if (uVar10 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar11 = _DAT_fffd062c;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            uVar2 = uVar2 ^ uVar4;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            jVar5 = (uint)uVar2 & 7;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar11) || ((uVar2 & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
          }
          jVar6 = (*_DAT_fffd0650)(jd,1,0,jd->workbuf_y);
          jVar5 = jVar5 | jVar6;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->color_trans)(jd->y,(int16_t *)0x0,(int16_t *)0x0,jd->w_h,
                             puVar7 + (uint)jd->pixel * iVar8);
          uVar10 = (uint)jd->width;
          iVar8 = iVar8 + jd->w_h[2];
        } while (iVar8 < (int)uVar10);
        uVar11 = (uint)jd->height;
      }
      iVar9 = iVar9 + jd->w_h[3];
      puVar7 = puVar7 + jd->out_h;
    } while (iVar9 < (int)uVar11);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_gray_180
 * Purpose: Decodes JPEG MCUs for grayscale Y with 180-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000106e4
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_gray_180(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  int iVar7;
  uint8_t *puVar8;
  int iVar9;
  uint uVar10;
  uint uVar11;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar11 = (uint)jd->height;
  if (uVar11 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar8 = io->outbuf + jd->out_start_pos;
    uVar10 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar9 = 0;
    do {
      iVar7 = 0;
      if (uVar10 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar11 = _DAT_fffd075c;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            uVar2 = uVar2 ^ uVar4;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            jVar5 = (uint)uVar2 & 7;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar11) || ((uVar2 & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
          }
          jVar6 = (*_DAT_fffd0780)(jd,1,0,jd->workbuf_y);
          jVar5 = jVar5 | jVar6;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->color_trans)(jd->y,(int16_t *)0x0,(int16_t *)0x0,jd->w_h,
                             puVar8 + -(int)(short)((ushort)jd->pixel * (short)iVar7));
          uVar10 = (uint)jd->width;
          iVar7 = iVar7 + jd->w_h[2];
        } while (iVar7 < (int)uVar10);
        uVar11 = (uint)jd->height;
      }
      iVar9 = iVar9 + jd->w_h[3];
      puVar8 = puVar8 + jd->out_h;
    } while (iVar9 < (int)uVar11);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_gray_0_clipper
 * Purpose: Decodes JPEG MCUs for grayscale Y without rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00010818
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_gray_0_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  uint8_t *puVar7;
  int iVar8;
  uint uVar9;
  uint uVar10;
  int iVar11;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  if ((jd->resize).height == 0) {
    jVar6 = JPEG_ERR_OK;
    uVar10 = 0;
  }
  else {
    iVar11 = 0;
    puVar7 = io->outbuf + jd->out_start_pos;
    uVar9 = (uint)jd->width;
    jVar6 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar8 = 0;
      if (uVar9 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar9 = _DAT_fffd0890;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            uVar2 = uVar2 ^ uVar4;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            jVar6 = (uint)uVar2 & 7;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar9) || ((uVar2 & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
          }
          jVar5 = (*_DAT_fffd08b4)(jd,1,0,jd->workbuf_y);
          jVar6 = jVar6 | jVar5;
          if (iVar8 < (int)(uint)(jd->resize).width) {
            (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
            (*jd->color_trans)(jd->y,(int16_t *)0x0,(int16_t *)0x0,jd->w_h,
                               puVar7 + (uint)jd->pixel * iVar8);
          }
          uVar9 = (uint)jd->width;
          iVar8 = iVar8 + jd->w_h[2];
        } while (iVar8 < (int)uVar9);
      }
      uVar10 = (uint)(jd->resize).height;
      iVar11 = iVar11 + jd->w_h[3];
      puVar7 = puVar7 + jd->out_h;
    } while (iVar11 < (int)uVar10);
  }
  io->out_size = (jd->resize).width * uVar10 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar6;
}

/* ==================================================================
 * jpeg_dec_proc_gray_90
 * Purpose: Decodes JPEG MCUs for grayscale Y with 90-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00010948
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_gray_90(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  int iVar7;
  uint8_t *puVar8;
  int iVar9;
  uint uVar10;
  uint uVar11;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar11 = (uint)jd->height;
  if (uVar11 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar8 = io->outbuf + jd->out_start_pos;
    uVar10 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar9 = 0;
    do {
      iVar7 = 0;
      if (uVar10 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar11 = _DAT_fffd09c0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            uVar2 = uVar2 ^ uVar4;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            jVar5 = (uint)uVar2 & 7;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar11) || ((uVar2 & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
          }
          jVar6 = (*_DAT_fffd09e4)(jd,1,0,jd->workbuf_y);
          jVar5 = jVar5 | jVar6;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->color_trans)(jd->y,(int16_t *)0x0,(int16_t *)0x0,jd->w_h,
                             puVar8 + jd->w_h[0] * iVar7 * (uint)jd->pixel);
          uVar10 = (uint)jd->width;
          iVar7 = iVar7 + jd->w_h[3];
        } while (iVar7 < (int)uVar10);
        uVar11 = (uint)jd->height;
      }
      iVar9 = iVar9 + jd->w_h[2];
      puVar8 = puVar8 + jd->out_h;
    } while (iVar9 < (int)uVar11);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_gray_180_clipper
 * Purpose: Decodes JPEG MCUs for grayscale Y with 180-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00010a80
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_gray_180_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  uint8_t *puVar7;
  int iVar8;
  uint uVar9;
  uint uVar10;
  int iVar11;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  if ((jd->resize).height == 0) {
    jVar6 = JPEG_ERR_OK;
    uVar10 = 0;
  }
  else {
    iVar11 = 0;
    puVar7 = io->outbuf + jd->out_start_pos;
    uVar9 = (uint)jd->width;
    jVar6 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar8 = 0;
      if (uVar9 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar9 = _DAT_fffd0af8;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            uVar2 = uVar2 ^ uVar4;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            jVar6 = (uint)uVar2 & 7;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar9) || ((uVar2 & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
          }
          jVar5 = (*_DAT_fffd0b1c)(jd,1,0,jd->workbuf_y);
          jVar6 = jVar6 | jVar5;
          if (iVar8 < (int)(uint)(jd->resize).width) {
            (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
            (*jd->color_trans)(jd->y,(int16_t *)0x0,(int16_t *)0x0,jd->w_h,
                               puVar7 + -(int)(short)((ushort)jd->pixel * (short)iVar8));
          }
          uVar9 = (uint)jd->width;
          iVar8 = iVar8 + jd->w_h[2];
        } while (iVar8 < (int)uVar9);
      }
      uVar10 = (uint)(jd->resize).height;
      iVar11 = iVar11 + jd->w_h[3];
      puVar7 = puVar7 + jd->out_h;
    } while (iVar11 < (int)uVar10);
  }
  io->out_size = (jd->resize).width * uVar10 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar6;
}

/* ==================================================================
 * jpeg_dec_proc_gray_270
 * Purpose: Decodes JPEG MCUs for grayscale Y with 270-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00010bb4
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_gray_270(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  int iVar7;
  uint8_t *puVar8;
  int iVar9;
  uint uVar10;
  uint uVar11;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar11 = (uint)jd->height;
  if (uVar11 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar8 = io->outbuf + jd->out_start_pos;
    uVar10 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar9 = 0;
    do {
      iVar7 = 0;
      if (uVar10 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar11 = _DAT_fffd0c2c;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            uVar2 = uVar2 ^ uVar4;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            jVar5 = (uint)uVar2 & 7;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar11) || ((uVar2 & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
          }
          jVar6 = (*_DAT_fffd0c50)(jd,1,0,jd->workbuf_y);
          jVar5 = jVar5 | jVar6;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->color_trans)(jd->y,(int16_t *)0x0,(int16_t *)0x0,jd->w_h,
                             puVar8 + -(jd->w_h[0] * iVar7 * (uint)jd->pixel));
          uVar10 = (uint)jd->width;
          iVar7 = iVar7 + jd->w_h[3];
        } while (iVar7 < (int)uVar10);
        uVar11 = (uint)jd->height;
      }
      iVar9 = iVar9 + jd->w_h[2];
      puVar8 = puVar8 + jd->out_h;
    } while (iVar9 < (int)uVar11);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_gray_90_clipper
 * Purpose: Decodes JPEG MCUs for grayscale Y with 90-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00010cec
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_gray_90_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  uint8_t *puVar7;
  int iVar8;
  uint uVar9;
  uint uVar10;
  int iVar11;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  if ((jd->resize).height == 0) {
    jVar6 = JPEG_ERR_OK;
    uVar10 = 0;
  }
  else {
    iVar11 = 0;
    puVar7 = io->outbuf + jd->out_start_pos;
    uVar9 = (uint)jd->width;
    jVar6 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar8 = 0;
      if (uVar9 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar9 = _DAT_fffd0d64;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            uVar2 = uVar2 ^ uVar4;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            jVar6 = (uint)uVar2 & 7;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar9) || ((uVar2 & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
          }
          jVar5 = (*_DAT_fffd0d88)(jd,1,0,jd->workbuf_y);
          jVar6 = jVar6 | jVar5;
          if (iVar8 < (int)(uint)(jd->resize).width) {
            (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
            (*jd->color_trans)(jd->y,(int16_t *)0x0,(int16_t *)0x0,jd->w_h,
                               puVar7 + jd->w_h[0] * iVar8 * (uint)jd->pixel);
          }
          uVar9 = (uint)jd->width;
          iVar8 = iVar8 + jd->w_h[3];
        } while (iVar8 < (int)uVar9);
      }
      uVar10 = (uint)(jd->resize).height;
      iVar11 = iVar11 + jd->w_h[2];
      puVar7 = puVar7 + jd->out_h;
    } while (iVar11 < (int)uVar10);
  }
  io->out_size = (jd->resize).width * uVar10 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar6;
}

/* ==================================================================
 * jpeg_dec_proc_gray_270_clipper
 * Purpose: Decodes JPEG MCUs for grayscale Y with 270-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00010e24
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_gray_270_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  uint8_t *puVar7;
  int iVar8;
  uint uVar9;
  uint uVar10;
  int iVar11;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  if ((jd->resize).height == 0) {
    jVar6 = JPEG_ERR_OK;
    uVar10 = 0;
  }
  else {
    iVar11 = 0;
    puVar7 = io->outbuf + jd->out_start_pos;
    uVar9 = (uint)jd->width;
    jVar6 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar8 = 0;
      if (uVar9 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar9 = _DAT_fffd0e9c;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            uVar2 = uVar2 ^ uVar4;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            jVar6 = (uint)uVar2 & 7;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar9) || ((uVar2 & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
          }
          jVar5 = (*_DAT_fffd0ec0)(jd,1,0,jd->workbuf_y);
          jVar6 = jVar6 | jVar5;
          if (iVar8 < (int)(uint)(jd->resize).width) {
            (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
            (*jd->color_trans)(jd->y,(int16_t *)0x0,(int16_t *)0x0,jd->w_h,
                               puVar7 + -(jd->w_h[0] * iVar8 * (uint)jd->pixel));
          }
          uVar9 = (uint)jd->width;
          iVar8 = iVar8 + jd->w_h[3];
        } while (iVar8 < (int)uVar9);
      }
      uVar10 = (uint)(jd->resize).height;
      iVar11 = iVar11 + jd->w_h[2];
      puVar7 = puVar7 + jd->out_h;
    } while (iVar11 < (int)uVar10);
  }
  io->out_size = (jd->resize).width * uVar10 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar6;
}

/* ==================================================================
 * jpeg_dec_proc_yuv444_0
 * Purpose: Decodes JPEG MCUs for YUV444 without rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00010f5c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv444_0(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  uint uVar11;
  uint uVar12;
  int iVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar12 = (uint)jd->height;
  if (uVar12 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar9 = io->outbuf;
    uVar11 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar13 = 0;
    do {
      iVar10 = 0;
      if (uVar11 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd0fd0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar5 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd0ff4)(jd,1,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd1008)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd101c)(jd,1,2,jd->workbuf_v);
          jVar5 = jVar5 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar9 + (uint)jd->pixel * iVar10);
          uVar11 = (uint)jd->width;
          iVar10 = iVar10 + jd->w_h[2];
        } while (iVar10 < (int)uVar11);
        uVar12 = (uint)jd->height;
      }
      iVar13 = iVar13 + jd->w_h[3];
      puVar9 = puVar9 + jd->out_h;
    } while (iVar13 < (int)uVar12);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_yuv444_0_clipper
 * Purpose: Decodes JPEG MCUs for YUV444 without rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000110e4
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv444_0_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  uint uVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  if ((jd->resize).height == 0) {
    jVar8 = JPEG_ERR_OK;
    uVar13 = 0;
  }
  else {
    puVar9 = io->outbuf;
    iVar11 = 0;
    uVar12 = (uint)jd->width;
    jVar8 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar10 = 0;
      if (uVar12 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd1154;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd117c)(jd,1,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd1190)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd11a4)(jd,1,2,jd->workbuf_v);
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          if (iVar10 < (int)(uint)(jd->resize).width) {
            (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar9 + (uint)jd->pixel * iVar10);
          }
          uVar12 = (uint)jd->width;
          iVar10 = iVar10 + jd->w_h[2];
        } while (iVar10 < (int)uVar12);
      }
      uVar13 = (uint)(jd->resize).height;
      iVar11 = iVar11 + jd->w_h[3];
      puVar9 = puVar9 + jd->out_h;
    } while (iVar11 < (int)uVar13);
  }
  io->out_size = (jd->resize).width * uVar13 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv444_180
 * Purpose: Decodes JPEG MCUs for YUV444 with 180-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00011268
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv444_180(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  uint uVar11;
  uint uVar12;
  int iVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar12 = (uint)jd->height;
  if (uVar12 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar9 = io->outbuf + jd->out_start_pos;
    uVar11 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar13 = 0;
    do {
      iVar10 = 0;
      if (uVar11 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd12e0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar5 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd1304)(jd,1,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd1318)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd132c)(jd,1,2,jd->workbuf_v);
          jVar5 = jVar5 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar9 + -(int)(short)((ushort)jd->pixel * (short)iVar10));
          uVar11 = (uint)jd->width;
          iVar10 = iVar10 + jd->w_h[2];
        } while (iVar10 < (int)uVar11);
        uVar12 = (uint)jd->height;
      }
      iVar13 = iVar13 + jd->w_h[3];
      puVar9 = puVar9 + jd->out_h;
    } while (iVar13 < (int)uVar12);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_yuv444_90
 * Purpose: Decodes JPEG MCUs for YUV444 with 90-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000113f8
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv444_90(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  uint uVar11;
  uint uVar12;
  int iVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar12 = (uint)jd->height;
  if (uVar12 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar9 = io->outbuf + jd->out_start_pos;
    uVar11 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar13 = 0;
    do {
      iVar10 = 0;
      if (uVar11 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd1470;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar5 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd1494)(jd,1,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd14a8)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd14bc)(jd,1,2,jd->workbuf_v);
          jVar5 = jVar5 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar9 + jd->w_h[0] * iVar10 * (uint)jd->pixel);
          uVar11 = (uint)jd->width;
          iVar10 = iVar10 + jd->w_h[3];
        } while (iVar10 < (int)uVar11);
        uVar12 = (uint)jd->height;
      }
      iVar13 = iVar13 + jd->w_h[2];
      puVar9 = puVar9 + jd->out_h;
    } while (iVar13 < (int)uVar12);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_yuv444_180_clipper
 * Purpose: Decodes JPEG MCUs for YUV444 with 180-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00011588
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv444_180_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  uint uVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  if ((jd->resize).height == 0) {
    jVar8 = JPEG_ERR_OK;
    uVar13 = 0;
  }
  else {
    iVar11 = 0;
    puVar9 = io->outbuf + jd->out_start_pos;
    uVar12 = (uint)jd->width;
    jVar8 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar10 = 0;
      if (uVar12 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd1600;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd1628)(jd,1,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd163c)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd1650)(jd,1,2,jd->workbuf_v);
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          if (iVar10 < (int)(uint)(jd->resize).width) {
            (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                               puVar9 + -(int)(short)((ushort)jd->pixel * (short)iVar10));
          }
          uVar12 = (uint)jd->width;
          iVar10 = iVar10 + jd->w_h[2];
        } while (iVar10 < (int)uVar12);
      }
      uVar13 = (uint)(jd->resize).height;
      iVar11 = iVar11 + jd->w_h[3];
      puVar9 = puVar9 + jd->out_h;
    } while (iVar11 < (int)uVar13);
  }
  io->out_size = (jd->resize).width * uVar13 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv444_270
 * Purpose: Decodes JPEG MCUs for YUV444 with 270-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00011718
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv444_270(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  uint uVar11;
  uint uVar12;
  int iVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar12 = (uint)jd->height;
  if (uVar12 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar9 = io->outbuf + jd->out_start_pos;
    uVar11 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar13 = 0;
    do {
      iVar10 = 0;
      if (uVar11 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd1790;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar5 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd17b4)(jd,1,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd17c8)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd17dc)(jd,1,2,jd->workbuf_v);
          jVar5 = jVar5 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar9 + -(jd->w_h[0] * iVar10 * (uint)jd->pixel));
          uVar11 = (uint)jd->width;
          iVar10 = iVar10 + jd->w_h[3];
        } while (iVar10 < (int)uVar11);
        uVar12 = (uint)jd->height;
      }
      iVar13 = iVar13 + jd->w_h[2];
      puVar9 = puVar9 + jd->out_h;
    } while (iVar13 < (int)uVar12);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_yuv444_90_clipper
 * Purpose: Decodes JPEG MCUs for YUV444 with 90-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000118ac
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv444_90_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  uint uVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  if ((jd->resize).height == 0) {
    jVar8 = JPEG_ERR_OK;
    uVar13 = 0;
  }
  else {
    iVar11 = 0;
    puVar9 = io->outbuf + jd->out_start_pos;
    uVar12 = (uint)jd->width;
    jVar8 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar10 = 0;
      if (uVar12 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd1924;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd194c)(jd,1,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd1960)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd1974)(jd,1,2,jd->workbuf_v);
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          if (iVar10 < (int)(uint)(jd->resize).width) {
            (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                               puVar9 + jd->w_h[0] * iVar10 * (uint)jd->pixel);
          }
          uVar12 = (uint)jd->width;
          iVar10 = iVar10 + jd->w_h[3];
        } while (iVar10 < (int)uVar12);
      }
      uVar13 = (uint)(jd->resize).height;
      iVar11 = iVar11 + jd->w_h[2];
      puVar9 = puVar9 + jd->out_h;
    } while (iVar11 < (int)uVar13);
  }
  io->out_size = (jd->resize).width * uVar13 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv444_270_clipper
 * Purpose: Decodes JPEG MCUs for YUV444 with 270-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00011a40
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv444_270_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  uint uVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  if ((jd->resize).height == 0) {
    jVar8 = JPEG_ERR_OK;
    uVar13 = 0;
  }
  else {
    iVar11 = 0;
    puVar9 = io->outbuf + jd->out_start_pos;
    uVar12 = (uint)jd->width;
    jVar8 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar10 = 0;
      if (uVar12 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd1ab8;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd1ae0)(jd,1,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd1af4)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd1b08)(jd,1,2,jd->workbuf_v);
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          if (iVar10 < (int)(uint)(jd->resize).width) {
            (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                               puVar9 + -(jd->w_h[0] * iVar10 * (uint)jd->pixel));
          }
          uVar12 = (uint)jd->width;
          iVar10 = iVar10 + jd->w_h[3];
        } while (iVar10 < (int)uVar12);
      }
      uVar13 = (uint)(jd->resize).height;
      iVar11 = iVar11 + jd->w_h[2];
      puVar9 = puVar9 + jd->out_h;
    } while (iVar11 < (int)uVar13);
  }
  io->out_size = (jd->resize).width * uVar13 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_0
 * Purpose: Decodes JPEG MCUs for YUV422 without rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00011bd4
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_0(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  int iVar8;
  jpeg_error_t jVar9;
  uint8_t *puVar10;
  uint uVar11;
  uint uVar12;
  int iVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar12 = (uint)jd->height;
  if (uVar12 == 0) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    puVar10 = io->outbuf;
    uVar11 = (uint)jd->width;
    jVar9 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar13 = 0;
    do {
      iVar8 = 0;
      if (uVar11 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd1c48;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd1c6c)(jd,2,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd1c80)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd1c94)(jd,1,2,jd->workbuf_v);
          jVar9 = jVar9 | jVar5 | jVar6 | jVar7;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar10 + (uint)jd->pixel * iVar8);
          uVar11 = (uint)jd->width;
          iVar8 = iVar8 + jd->w_h[2];
        } while (iVar8 < (int)uVar11);
        uVar12 = (uint)jd->height;
      }
      iVar13 = iVar13 + jd->w_h[3];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar13 < (int)uVar12);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_180
 * Purpose: Decodes JPEG MCUs for YUV422 with 180-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00011d7c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_180(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  int iVar8;
  jpeg_error_t jVar9;
  uint8_t *puVar10;
  uint uVar11;
  uint uVar12;
  int iVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar12 = (uint)jd->height;
  if (uVar12 == 0) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    puVar10 = io->outbuf + jd->out_start_pos;
    uVar11 = (uint)jd->width;
    jVar9 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar13 = 0;
    do {
      iVar8 = 0;
      if (uVar11 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd1df4;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd1e18)(jd,2,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd1e2c)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd1e40)(jd,1,2,jd->workbuf_v);
          jVar9 = jVar9 | jVar5 | jVar6 | jVar7;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar10 + -(int)(short)((ushort)jd->pixel * (short)iVar8));
          uVar11 = (uint)jd->width;
          iVar8 = iVar8 + jd->w_h[2];
        } while (iVar8 < (int)uVar11);
        uVar12 = (uint)jd->height;
      }
      iVar13 = iVar13 + jd->w_h[3];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar13 < (int)uVar12);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_90
 * Purpose: Decodes JPEG MCUs for YUV422 with 90-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00011f28
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_90(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  int iVar8;
  jpeg_error_t jVar9;
  uint8_t *puVar10;
  int iVar11;
  uint uVar12;
  uint uVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar13 = (uint)jd->height;
  if (uVar13 == 0) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    puVar10 = io->outbuf + jd->out_start_pos;
    uVar12 = (uint)jd->width;
    jVar9 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar11 = 0;
    do {
      iVar8 = 0;
      if (uVar12 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar13 = _DAT_fffd1f98;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar13) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd1fc8)(jd,2,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd1fdc)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd1ff0)(jd,1,2,jd->workbuf_v);
          jVar9 = jVar9 | jVar5 | jVar6 | jVar7;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar10 + jd->w_h[0] * iVar8 * (uint)jd->pixel);
          uVar12 = (uint)jd->width;
          iVar8 = iVar8 + jd->w_h[3];
        } while (iVar8 < (int)uVar12);
        uVar13 = (uint)jd->height;
      }
      iVar11 = iVar11 + jd->w_h[2];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar11 < (int)uVar13);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_270
 * Purpose: Decodes JPEG MCUs for YUV422 with 270-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000120dc
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_270(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  uint uVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar13 = (uint)jd->height;
  if (uVar13 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    iVar11 = 0;
    uVar12 = (uint)jd->width;
    puVar9 = io->outbuf + jd->out_start_pos;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar10 = 0;
      if (uVar12 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar13 = _DAT_fffd214c;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar13) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar5 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd217c)(jd,2,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd2190)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd21a4)(jd,1,2,jd->workbuf_v);
          jVar5 = jVar5 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar9 + -(jd->w_h[0] * iVar10 * (uint)jd->pixel));
          uVar12 = (uint)jd->width;
          iVar10 = iVar10 + jd->w_h[3];
        } while (iVar10 < (int)uVar12);
        uVar13 = (uint)jd->height;
      }
      iVar11 = iVar11 + jd->w_h[2];
      puVar9 = puVar9 + jd->out_h;
    } while (iVar11 < (int)uVar13);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_0
 * Purpose: Decodes JPEG MCUs for YUV420 without rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00012290
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_0(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  int iVar9;
  uint8_t *puVar10;
  uint uVar11;
  int iVar12;
  uint uVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar13 = (uint)jd->height;
  if (uVar13 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar10 = io->outbuf;
    uVar11 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar12 = 0;
    do {
      iVar9 = 0;
      if (uVar11 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar13 = _DAT_fffd22fc;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar13) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar5 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd2328)(jd,4,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd233c)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd2350)(jd,1,2,jd->workbuf_v);
          jVar5 = jVar5 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar10 + (uint)jd->pixel * iVar9);
          uVar11 = (uint)jd->width;
          iVar9 = iVar9 + jd->w_h[2];
        } while (iVar9 < (int)uVar11);
        uVar13 = (uint)jd->height;
      }
      iVar12 = iVar12 + jd->w_h[3];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar12 < (int)uVar13);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_180
 * Purpose: Decodes JPEG MCUs for YUV420 with 180-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00012478
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_180(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  int iVar9;
  uint8_t *puVar10;
  uint uVar11;
  int iVar12;
  uint uVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar13 = (uint)jd->height;
  if (uVar13 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar10 = io->outbuf + jd->out_start_pos;
    uVar11 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar12 = 0;
    do {
      iVar9 = 0;
      if (uVar11 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar13 = _DAT_fffd24e8;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar13) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar5 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd2518)(jd,4,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd252c)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd2540)(jd,1,2,jd->workbuf_v);
          jVar5 = jVar5 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar10 + -(int)(short)((ushort)jd->pixel * (short)iVar9));
          uVar11 = (uint)jd->width;
          iVar9 = iVar9 + jd->w_h[2];
        } while (iVar9 < (int)uVar11);
        uVar13 = (uint)jd->height;
      }
      iVar12 = iVar12 + jd->w_h[3];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar12 < (int)uVar13);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_90
 * Purpose: Decodes JPEG MCUs for YUV420 with 90-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 0001266c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_90(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  int iVar9;
  uint8_t *puVar10;
  uint uVar11;
  int iVar12;
  uint uVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar13 = (uint)jd->height;
  if (uVar13 == 0) {
    jVar5 = JPEG_ERR_OK;
  }
  else {
    puVar10 = io->outbuf + jd->out_start_pos;
    uVar11 = (uint)jd->width;
    jVar5 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar12 = 0;
    do {
      iVar9 = 0;
      if (uVar11 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar13 = _DAT_fffd26dc;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar13) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar5 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd270c)(jd,4,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd2720)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd2734)(jd,1,2,jd->workbuf_v);
          jVar5 = jVar5 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar10 + jd->w_h[0] * iVar9 * (uint)jd->pixel);
          uVar11 = (uint)jd->width;
          iVar9 = iVar9 + jd->w_h[3];
        } while (iVar9 < (int)uVar11);
        uVar13 = (uint)jd->height;
      }
      iVar12 = iVar12 + jd->w_h[2];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar12 < (int)uVar13);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_270
 * Purpose: Decodes JPEG MCUs for YUV420 with 270-degree rotation using the ordinary image-output path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00012860
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_270(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  int iVar8;
  jpeg_error_t jVar9;
  uint8_t *puVar10;
  uint uVar11;
  uint uVar12;
  int iVar13;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar12 = (uint)jd->height;
  if (uVar12 == 0) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    puVar10 = io->outbuf + jd->out_start_pos;
    uVar11 = (uint)jd->width;
    jVar9 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    iVar13 = 0;
    do {
      iVar8 = 0;
      if (uVar11 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd28d0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) || (((uVar2 ^ uVar4) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd2900)(jd,4,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd2914)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd2928)(jd,1,2,jd->workbuf_v);
          jVar9 = jVar9 | jVar5 | jVar6 | jVar7;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar10 + -(jd->w_h[0] * iVar8 * (uint)jd->pixel));
          uVar11 = (uint)jd->width;
          iVar8 = iVar8 + jd->w_h[3];
        } while (iVar8 < (int)uVar11);
        uVar12 = (uint)jd->height;
      }
      iVar13 = iVar13 + jd->w_h[2];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar13 < (int)uVar12);
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_0_clipper
 * Purpose: Decodes JPEG MCUs for YUV422 without rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00012a54
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_0_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  short sVar1;
  uint16_t uVar2;
  ushort uVar3;
  ushort uVar4;
  ushort uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  uint8_t *puVar10;
  int iVar11;
  idct_func p_Var12;
  uint uVar13;
  int iVar14;
  int16_t *piVar15;
  int iVar16;
  uint uVar17;
  uint uVar18;
  int16_t *piVar19;
  int16_t *piVar20;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int width_loop@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar18 = (uint)(jd->resize).width;
  jVar9 = JPEG_ERR_OK;
  uVar17 = 0;
  if ((jd->resize).height != 0) {
    sVar1 = jd->w_h[2];
    iVar14 = 0;
    puVar10 = io->outbuf;
    uVar13 = (uint)jd->width;
    jVar9 = JPEG_ERR_OK;
    do {
      iVar11 = 0;
      if (uVar13 != 0) {
        do {
                    /* Unresolved local var: int w@[???] */
          if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
            uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
            jd->rsc = uVar3 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar17 = _DAT_fffd2ab8;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar4 = *(ushort *)jd->dptr;
            uVar5 = uVar4 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar17) || (((uVar3 ^ uVar5) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd2ae8)(jd,2,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd2afc)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd2b10)(jd,1,2,jd->workbuf_v);
          uVar17 = (uint)(jd->resize).width;
          jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
          if (iVar11 < (int)uVar17) {
            p_Var12 = jd->idct_y;
            piVar15 = jd->workbuf_y;
            piVar19 = jd->qttbl[jd->qtid[0]];
            piVar20 = jd->y;
            if (iVar11 + 8 < (int)uVar17) {
              (*p_Var12)(piVar15,8,piVar19,piVar20,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
              (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
              (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
              (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar10 + (uint)jd->pixel * iVar11);
              goto LAB_00012c1c;
            }
            jd->w_h[2] = 8;
            jd->w_h[3] = 8;
            (*p_Var12)(piVar15,8,piVar19,piVar20,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                               puVar10 + (uint)jd->pixel * ((int)uVar18 / (int)sVar1) * 0x10);
            jd->w_h[2] = 0x10;
            jd->w_h[3] = 8;
            iVar16 = 0x10;
          }
          else {
LAB_00012c1c:
            iVar16 = (int)jd->w_h[2];
          }
          uVar13 = (uint)jd->width;
          iVar11 = iVar11 + iVar16;
        } while (iVar11 < (int)uVar13);
      }
      uVar17 = (uint)(jd->resize).height;
      iVar14 = iVar14 + jd->w_h[3];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar14 < (int)uVar17);
    uVar18 = (uint)(jd->resize).width;
  }
  io->out_size = uVar17 * uVar18 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_180_clipper
 * Purpose: Decodes JPEG MCUs for YUV422 with 180-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00012c80
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_180_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  short sVar1;
  uint16_t uVar2;
  ushort uVar3;
  ushort uVar4;
  ushort uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  uint8_t *puVar10;
  int iVar11;
  uint uVar12;
  uint uVar13;
  idct_func p_Var14;
  int iVar15;
  int16_t *piVar16;
  int iVar17;
  uint uVar18;
  int16_t *piVar19;
  int16_t *piVar20;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int width_loop@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar12 = (uint)(jd->resize).width;
  if ((jd->resize).height == 0) {
    jVar9 = JPEG_ERR_OK;
    uVar18 = 0;
  }
  else {
    sVar1 = jd->w_h[2];
                    /* Unresolved local var: int w@[???] */
    iVar15 = 0;
    puVar10 = io->outbuf + jd->out_start_pos;
    uVar13 = (uint)jd->width;
    jVar9 = JPEG_ERR_OK;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar11 = 0;
      if (uVar13 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
            uVar3 = jd->rsc;
            jd->rsc = uVar3 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar13 = _DAT_fffd2d04;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar4 = *(ushort *)jd->dptr;
            uVar5 = uVar4 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar13) || (((uVar3 ^ uVar5) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd2d34)(jd,2,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd2d48)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd2d5c)(jd,1,2,jd->workbuf_v);
          uVar13 = (uint)(jd->resize).width;
          jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
          if (iVar11 < (int)uVar13) {
            p_Var14 = jd->idct_y;
            piVar16 = jd->workbuf_y;
            piVar19 = jd->qttbl[jd->qtid[0]];
            piVar20 = jd->y;
            if (iVar11 + 8 < (int)uVar13) {
              (*p_Var14)(piVar16,8,piVar19,piVar20 + 8,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
              (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
              (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
              (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                                 puVar10 + -(int)(short)((ushort)jd->pixel * (short)iVar11));
              goto LAB_00012e6a;
            }
            jd->w_h[2] = 8;
            jd->w_h[3] = 8;
            (*p_Var14)(piVar16,8,piVar19,piVar20,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans_90)
                      (jd->y,jd->u,jd->v,jd->w_h,
                       puVar10 + -(int)(short)((ushort)jd->pixel *
                                              ((short)((int)uVar12 / (int)sVar1) * 0x10 + -8)));
            jd->w_h[2] = 0x10;
            jd->w_h[3] = 8;
            iVar17 = 0x10;
          }
          else {
LAB_00012e6a:
            iVar17 = (int)jd->w_h[2];
          }
          uVar13 = (uint)jd->width;
          iVar11 = iVar11 + iVar17;
        } while (iVar11 < (int)uVar13);
      }
      uVar18 = (uint)(jd->resize).height;
      iVar15 = iVar15 + jd->w_h[3];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar15 < (int)uVar18);
    uVar12 = (uint)(jd->resize).width;
  }
  io->out_size = uVar18 * uVar12 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_90_clipper
 * Purpose: Decodes JPEG MCUs for YUV422 with 90-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00012ebc
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_90_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  short sVar1;
  uint16_t uVar2;
  ushort uVar3;
  ushort uVar4;
  ushort uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  uint8_t *puVar10;
  int iVar11;
  uint uVar12;
  idct_func p_Var13;
  int iVar14;
  int iVar15;
  int16_t *piVar16;
  uint uVar17;
  int16_t *piVar18;
  uint uVar19;
  int16_t *piVar20;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int width_loop@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar12 = (uint)(jd->resize).width;
  jVar9 = JPEG_ERR_OK;
  uVar19 = 0;
  if ((jd->resize).height != 0) {
    sVar1 = jd->w_h[3];
    iVar15 = 0;
    uVar17 = (uint)jd->width;
    puVar10 = io->outbuf + jd->out_start_pos;
    jVar9 = JPEG_ERR_OK;
    do {
      iVar11 = 0;
      if (uVar17 != 0) {
        do {
                    /* Unresolved local var: int w@[???] */
          if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
            uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
            jd->rsc = uVar3 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar19 = _DAT_fffd2f20;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar4 = *(ushort *)jd->dptr;
            uVar5 = uVar4 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar19) || (((uVar3 ^ uVar5) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd2f50)(jd,2,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd2f64)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd2f78)(jd,1,2,jd->workbuf_v);
          uVar19 = (uint)(jd->resize).width;
          jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
          if (iVar11 < (int)uVar19) {
            p_Var13 = jd->idct_y;
            piVar16 = jd->workbuf_y;
            piVar18 = jd->qttbl[jd->qtid[0]];
            piVar20 = jd->y;
            if (iVar11 + 8 < (int)uVar19) {
              (*p_Var13)(piVar16,8,piVar18,piVar20,8);
              (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
              (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
              (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
              (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                                 puVar10 + jd->w_h[0] * iVar11 * (uint)jd->pixel);
              goto LAB_0001308c;
            }
            jd->w_h[2] = 8;
            jd->w_h[3] = 8;
            (*p_Var13)(piVar16,8,piVar18,piVar20,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                               puVar10 + (int)jd->w_h[0] * ((int)uVar12 / (int)sVar1) *
                                         (uint)jd->pixel * 0x10);
            jd->w_h[3] = 0x10;
            jd->w_h[2] = 8;
            iVar14 = 0x10;
          }
          else {
LAB_0001308c:
            iVar14 = (int)jd->w_h[3];
          }
          uVar17 = (uint)jd->width;
          iVar11 = iVar11 + iVar14;
        } while (iVar11 < (int)uVar17);
      }
      uVar19 = (uint)(jd->resize).height;
      iVar15 = iVar15 + jd->w_h[2];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar15 < (int)uVar19);
    uVar12 = (uint)(jd->resize).width;
  }
  io->out_size = uVar19 * uVar12 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_270_clipper
 * Purpose: Decodes JPEG MCUs for YUV422 with 270-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000130f4
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_270_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  short sVar1;
  uint16_t uVar2;
  ushort uVar3;
  ushort uVar4;
  ushort uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  uint8_t *puVar10;
  int iVar11;
  uint uVar12;
  uint uVar13;
  idct_func p_Var14;
  int iVar15;
  int16_t *piVar16;
  int iVar17;
  uint uVar18;
  int16_t *piVar19;
  int16_t *piVar20;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int width_loop@[???] */
                    /* Unresolved local var: int h@[???] */
  uVar12 = (uint)(jd->resize).width;
  if ((jd->resize).height == 0) {
    jVar9 = JPEG_ERR_OK;
    uVar18 = 0;
  }
  else {
    sVar1 = jd->w_h[3];
                    /* Unresolved local var: int w@[???] */
    iVar15 = 0;
    uVar13 = (uint)jd->width;
    puVar10 = io->outbuf + jd->out_start_pos;
    jVar9 = JPEG_ERR_OK;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar11 = 0;
      if (uVar13 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
            uVar3 = jd->rsc;
            jd->rsc = uVar3 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar13 = _DAT_fffd3174;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar4 = *(ushort *)jd->dptr;
            uVar5 = uVar4 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if ((((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar13) || (((uVar3 ^ uVar5) & 7) != 0)) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd31a4)(jd,2,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd31b8)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd31cc)(jd,1,2,jd->workbuf_v);
          uVar13 = (uint)(jd->resize).width;
          jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
          if (iVar11 < (int)uVar13) {
            p_Var14 = jd->idct_y;
            piVar16 = jd->workbuf_y;
            piVar19 = jd->qttbl[jd->qtid[0]];
            piVar20 = jd->y;
            if (iVar11 + 8 < (int)uVar13) {
              (*p_Var14)(piVar16,8,piVar19,piVar20 + 0x40,8);
              (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,8);
              (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
              (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
              (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                                 puVar10 + -(jd->w_h[0] * iVar11 * (uint)jd->pixel));
              goto LAB_000132e6;
            }
            jd->w_h[2] = 8;
            jd->w_h[3] = 8;
            (*p_Var14)(piVar16,8,piVar19,piVar20,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u + 0x20,jd->v + 0x20,jd->w_h,
                               puVar10 + -((int)jd->w_h[0] *
                                           (((int)uVar12 / (int)sVar1) * 0x10 + -8) *
                                          (uint)jd->pixel));
            jd->w_h[2] = 8;
            jd->w_h[3] = 0x10;
            iVar17 = 0x10;
          }
          else {
LAB_000132e6:
            iVar17 = (int)jd->w_h[3];
          }
          uVar13 = (uint)jd->width;
          iVar11 = iVar11 + iVar17;
        } while (iVar11 < (int)uVar13);
      }
      uVar18 = (uint)(jd->resize).height;
      iVar15 = iVar15 + jd->w_h[2];
      puVar10 = puVar10 + jd->out_h;
    } while (iVar15 < (int)uVar18);
    uVar12 = (uint)(jd->resize).width;
  }
  io->out_size = uVar18 * uVar12 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_gray_0_block
 * Purpose: Decodes JPEG MCUs for grayscale Y without rotation using the block-output path used by the streaming API; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00013338
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_gray_0_block(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  short sVar3;
  ushort uVar4;
  uint uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  int iVar8;
  uint8_t *puVar9;
  uint32_t uVar10;
  ushort uVar11;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int w@[???] */
  if (jd->width == 0) {
    jVar7 = JPEG_ERR_OK;
  }
  else {
    iVar8 = 0;
    uVar10 = jd->out_start_pos;
    puVar9 = io->outbuf;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    jVar7 = JPEG_ERR_OK;
    do {
      if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
        uVar2 = jd->rsc;
        jd->rsc = uVar2 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        uVar5 = _DAT_fffd33a0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar4 = *(ushort *)jd->dptr;
        uVar11 = uVar4 >> 8;
        uVar2 = uVar2 ^ uVar11;
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        jd->dctr = jd->dctr - 2;
        jVar7 = (uint)uVar2 & 7;
        if ((((uVar4 & 0xff) << 8 | uVar11 & 0xd8) != uVar5) || ((uVar2 & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
      }
      jVar6 = (*_DAT_fffd33c4)(jd,1,0,jd->workbuf_y);
      jVar7 = jVar7 | jVar6;
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->color_trans)(jd->y,(int16_t *)0x0,(int16_t *)0x0,jd->w_h,
                         puVar9 + (uint)jd->pixel * iVar8 + uVar10);
      iVar8 = iVar8 + jd->w_h[2];
    } while (iVar8 < (int)(uint)jd->width);
  }
  sVar3 = jd->w_h[3];
  uVar2 = (jd->resize).width;
  uVar11 = jd->block_output_line + sVar3;
  jd->block_output_line = uVar11;
  uVar4 = (jd->resize).height;
  io->out_size = (uint)uVar2 * (int)sVar3 * (uint)jd->pixel;
  if (uVar4 <= uVar11) {
    jd->start_sos = true;
    jd->block_output_line = 0;
  }
  return jVar7;
}

/* ==================================================================
 * jpeg_dec_proc_yuv444_0_block
 * Purpose: Decodes JPEG MCUs for YUV444 without rotation using the block-output path used by the streaming API; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 0001345c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv444_0_block(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  short sVar3;
  ushort uVar4;
  uint uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  int iVar10;
  uint8_t *puVar11;
  ushort uVar12;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???] */
                    /* Unresolved local var: int w@[???] */
  if (jd->width == 0) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    iVar10 = 0;
    puVar11 = io->outbuf;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    jVar9 = JPEG_ERR_OK;
    do {
      if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
        uVar2 = jd->rsc;
        jd->rsc = uVar2 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        uVar5 = _DAT_fffd34c4;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar4 = *(ushort *)jd->dptr;
        uVar12 = uVar4 >> 8;
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        jd->dctr = jd->dctr - 2;
        if ((((uVar4 & 0xff) << 8 | uVar12 & 0xd8) != uVar5) || (((uVar2 ^ uVar12) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar9 = JPEG_ERR_OK;
      }
      jVar6 = (*_DAT_fffd34e8)(jd,1,0,jd->workbuf_y);
      jVar7 = (*_DAT_fffd34fc)(jd,1,1,jd->workbuf_u);
      jVar8 = (*_DAT_fffd3510)(jd,1,2,jd->workbuf_v);
      jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar11 + (uint)jd->pixel * iVar10);
      iVar10 = iVar10 + jd->w_h[2];
    } while (iVar10 < (int)(uint)jd->width);
  }
  sVar3 = jd->w_h[3];
  uVar2 = (jd->resize).width;
  uVar12 = jd->block_output_line + sVar3;
  jd->block_output_line = uVar12;
  uVar4 = (jd->resize).height;
  io->out_size = (uint)uVar2 * (int)sVar3 * (uint)jd->pixel;
  if (uVar4 <= uVar12) {
    jd->start_sos = true;
    jd->block_output_line = 0;
  }
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_0_unalign
 * Purpose: Decodes JPEG MCUs for YUV422 without rotation using the edge path for dimensions not aligned to an MCU; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000135d4
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_0_unalign(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  uint uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  int iVar10;
  int iVar11;
  uint8_t *puVar12;
  int iVar13;
  int iVar14;
  uint uVar15;
  uint uVar16;
  int16_t *piVar17;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???] */
  iVar11 = (int)(uint)jd->height / (int)jd->w_h[3];
                    /* Unresolved local var: int h@[???] */
  if (iVar11 < 1) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    uVar16 = (uint)jd->width;
    iVar14 = (int)jd->w_h[2];
    jVar9 = JPEG_ERR_OK;
    iVar13 = (int)uVar16 / iVar14;
    puVar12 = io->outbuf;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    while( true ) {
      iVar10 = 0;
      if (iVar14 <= (int)uVar16) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            uVar16 = _DAT_fffd3654;
            jd->dctr = jd->dctr - 2;
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar16) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd367c)(jd,2,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd3690)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd36a4)(jd,1,2,jd->workbuf_v);
          jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar12 + (uint)jd->pixel * iVar10);
          iVar10 = iVar10 + jd->w_h[2];
        } while (iVar10 <= (int)((uint)jd->width - (int)jd->w_h[2]));
      }
      if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
        uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar2 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar3 = *(ushort *)jd->dptr;
        uVar15 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar5 = _DAT_fffd3790;
        uVar16 = _DAT_fffd3788;
        jd->dctr = jd->dctr - 2;
        if (((uVar15 & uVar16) != uVar5) || (((uVar2 ^ uVar15) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar9 = JPEG_ERR_OK;
      }
      piVar17 = jd->workbuf_y;
      jd->w_h[2] = 8;
      jd->w_h[3] = 8;
      jVar6 = (*_DAT_fffd37c4)(jd,2,0,piVar17);
      jVar7 = (*_DAT_fffd37d8)(jd,1,1,jd->workbuf_u);
      jVar8 = (*_DAT_fffd37f0)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar12 + (uint)jd->pixel * iVar13 * 0x10);
      jd->w_h[2] = 0x10;
      jd->w_h[3] = 8;
      iVar11 = iVar11 + -1;
      jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
      if (iVar11 == 0) break;
      uVar16 = (uint)jd->width;
      puVar12 = puVar12 + jd->out_h;
      iVar14 = 0x10;
    }
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_180_unalign
 * Purpose: Decodes JPEG MCUs for YUV422 with 180-degree rotation using the edge path for dimensions not aligned to an MCU; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000138ac
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_180_unalign(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  uint uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  int iVar10;
  int iVar11;
  uint8_t *puVar12;
  int iVar13;
  uint uVar14;
  uint uVar15;
  int iVar16;
  int16_t *piVar17;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???] */
  iVar11 = (int)(uint)jd->height / (int)jd->w_h[3];
                    /* Unresolved local var: int h@[???] */
  if (iVar11 < 1) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    uVar15 = (uint)jd->width;
    iVar13 = (int)jd->w_h[2];
    iVar16 = (int)uVar15 / iVar13;
    jVar9 = JPEG_ERR_OK;
    puVar12 = io->outbuf + jd->out_start_pos;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    while( true ) {
      iVar10 = 0;
      if (iVar13 <= (int)uVar15) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dctr = jd->dctr - 2;
            uVar15 = _DAT_fffd3934;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar15) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd3964)(jd,2,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd3978)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd398c)(jd,1,2,jd->workbuf_v);
          jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar12 + -(int)(short)((ushort)jd->pixel * (short)iVar10));
          iVar10 = iVar10 + jd->w_h[2];
        } while (iVar10 <= (int)((uint)jd->width - (int)jd->w_h[2]));
      }
      if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
        uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar2 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar3 = *(ushort *)jd->dptr;
        uVar14 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar5 = _DAT_fffd3a74;
        uVar15 = _DAT_fffd3a70;
        jd->dctr = jd->dctr - 2;
        if (((uVar14 & uVar15) != uVar5) || (((uVar2 ^ uVar14) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar9 = JPEG_ERR_OK;
      }
      piVar17 = jd->workbuf_y;
      jd->w_h[2] = 8;
      jd->w_h[3] = 8;
      jVar6 = (*_DAT_fffd3aa8)(jd,2,0,piVar17);
      jVar7 = (*_DAT_fffd3abc)(jd,1,1,jd->workbuf_u);
      jVar8 = (*_DAT_fffd3ad4)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      (*jd->color_trans_90)
                (jd->y,jd->u,jd->v,jd->w_h,
                 puVar12 + -(int)(short)((ushort)jd->pixel * ((short)iVar16 * 0x10 + -8)));
      jd->w_h[2] = 0x10;
      jd->w_h[3] = 8;
      iVar11 = iVar11 + -1;
      jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
      if (iVar11 == 0) break;
      uVar15 = (uint)jd->width;
      puVar12 = puVar12 + jd->out_h;
      iVar13 = 0x10;
    }
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_90_unalign
 * Purpose: Decodes JPEG MCUs for YUV422 with 90-degree rotation using the edge path for dimensions not aligned to an MCU; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00013b90
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_90_unalign(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  uint uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  int iVar10;
  int iVar11;
  uint8_t *puVar12;
  int iVar13;
  int iVar14;
  uint uVar15;
  uint uVar16;
  int16_t *piVar17;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???] */
  iVar11 = (int)(uint)jd->height / (int)jd->w_h[2];
                    /* Unresolved local var: int h@[???] */
  if (iVar11 < 1) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    uVar16 = (uint)jd->width;
    iVar14 = (int)jd->w_h[3];
    jVar9 = JPEG_ERR_OK;
    iVar13 = (int)uVar16 / iVar14;
    puVar12 = io->outbuf + jd->out_start_pos;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    while( true ) {
      iVar10 = 0;
      if (iVar14 <= (int)uVar16) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dctr = jd->dctr - 2;
            uVar16 = _DAT_fffd3c0c;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar16) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd3c3c)(jd,2,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd3c50)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd3c64)(jd,1,2,jd->workbuf_v);
          jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar12 + jd->w_h[0] * iVar10 * (uint)jd->pixel);
          iVar10 = iVar10 + jd->w_h[3];
        } while (iVar10 <= (int)((uint)jd->width - (int)jd->w_h[3]));
      }
      if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
        uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar2 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar3 = *(ushort *)jd->dptr;
        uVar15 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar5 = _DAT_fffd3d50;
        uVar16 = _DAT_fffd3d48;
        jd->dctr = jd->dctr - 2;
        if (((uVar15 & uVar16) != uVar5) || (((uVar2 ^ uVar15) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar9 = JPEG_ERR_OK;
      }
      piVar17 = jd->workbuf_y;
      jd->w_h[2] = 8;
      jd->w_h[3] = 8;
      jVar6 = (*_DAT_fffd3d84)(jd,2,0,piVar17);
      jVar7 = (*_DAT_fffd3d98)(jd,1,1,jd->workbuf_u);
      jVar8 = (*_DAT_fffd3db0)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                         puVar12 + jd->w_h[0] * iVar13 * (uint)jd->pixel * 0x10);
      iVar11 = iVar11 + -1;
      jd->w_h[2] = 8;
      jd->w_h[3] = 0x10;
      jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
      if (iVar11 == 0) break;
      uVar16 = (uint)jd->width;
      puVar12 = puVar12 + jd->out_h;
      iVar14 = 0x10;
    }
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_270_unalign
 * Purpose: Decodes JPEG MCUs for YUV422 with 270-degree rotation using the edge path for dimensions not aligned to an MCU; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00013e70
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_270_unalign(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  uint uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  int iVar10;
  int iVar11;
  uint8_t *puVar12;
  int iVar13;
  uint uVar14;
  uint uVar15;
  int iVar16;
  int16_t *piVar17;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???] */
  iVar11 = (int)(uint)jd->height / (int)jd->w_h[2];
                    /* Unresolved local var: int h@[???] */
  if (iVar11 < 1) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    uVar15 = (uint)jd->width;
    iVar13 = (int)jd->w_h[3];
    iVar16 = (int)uVar15 / iVar13;
    jVar9 = JPEG_ERR_OK;
    puVar12 = io->outbuf + jd->out_start_pos;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    while( true ) {
      iVar10 = 0;
      if (iVar13 <= (int)uVar15) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dctr = jd->dctr - 2;
            uVar15 = _DAT_fffd3ef4;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar15) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd3f24)(jd,2,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd3f38)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd3f4c)(jd,1,2,jd->workbuf_v);
          jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar12 + -(jd->w_h[0] * iVar10 * (uint)jd->pixel));
          iVar10 = iVar10 + jd->w_h[3];
        } while (iVar10 <= (int)((uint)jd->width - (int)jd->w_h[3]));
      }
      if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
        uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar2 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar3 = *(ushort *)jd->dptr;
        uVar14 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar5 = _DAT_fffd403c;
        uVar15 = _DAT_fffd4038;
        jd->dctr = jd->dctr - 2;
        if (((uVar14 & uVar15) != uVar5) || (((uVar2 ^ uVar14) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar9 = JPEG_ERR_OK;
      }
      piVar17 = jd->workbuf_y;
      jd->w_h[2] = 8;
      jd->w_h[3] = 8;
      jVar6 = (*_DAT_fffd4070)(jd,2,0,piVar17);
      jVar7 = (*_DAT_fffd4084)(jd,1,1,jd->workbuf_u);
      jVar8 = (*_DAT_fffd409c)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      (*jd->color_trans)(jd->y,jd->u + 0x20,jd->v + 0x20,jd->w_h,
                         puVar12 + -((int)jd->w_h[0] * (iVar16 * 0x10 + -8) * (uint)jd->pixel));
      iVar11 = iVar11 + -1;
      jd->w_h[2] = 8;
      jd->w_h[3] = 0x10;
      jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
      if (iVar11 == 0) break;
      uVar15 = (uint)jd->width;
      puVar12 = puVar12 + jd->out_h;
      iVar13 = 0x10;
    }
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_0_clipper
 * Purpose: Decodes JPEG MCUs for YUV420 without rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00014160
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_0_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  uint uVar13;
  idct_func p_Var14;
  int iVar15;
  int iVar16;
  int16_t *piVar17;
  int iVar18;
  int16_t *piVar19;
  int16_t *piVar20;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int left_y@[???] */
  uVar13 = (uint)(jd->resize).height;
  iVar11 = (int)jd->w_h[3];
  iVar15 = (int)(uint)(jd->resize).width / (int)jd->w_h[2];
  puVar9 = io->outbuf;
                    /* Unresolved local var: int h@[???] */
  jVar8 = JPEG_ERR_OK;
  if (0 < (int)uVar13 / iVar11) {
    iVar18 = 0;
    uVar12 = (uint)jd->width;
    jVar8 = JPEG_ERR_OK;
    do {
      iVar10 = 0;
      if (uVar12 != 0) {
        do {
                    /* Unresolved local var: int w@[???] */
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd41d8;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd4208)(jd,4,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd421c)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd4230)(jd,1,2,jd->workbuf_v);
          uVar12 = (uint)(jd->resize).width;
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          if (iVar10 < (int)uVar12) {
            p_Var14 = jd->idct_y;
            piVar17 = jd->workbuf_y;
            piVar19 = jd->qttbl[jd->qtid[0]];
            piVar20 = jd->y;
            if (iVar10 + 8 < (int)uVar12) {
              (*p_Var14)(piVar17,8,piVar19,piVar20,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
              (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
              (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
              (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar9 + (uint)jd->pixel * iVar10);
              goto LAB_0001439e;
            }
            jd->w_h[3] = 0x10;
            jd->w_h[2] = 8;
            (*p_Var14)(piVar17,8,piVar19,piVar20,8);
            (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar9 + (uint)jd->pixel * iVar15 * 0x10);
            iVar16 = 0x10;
            jd->w_h[2] = 0x10;
            jd->w_h[3] = 0x10;
          }
          else {
LAB_0001439e:
            iVar16 = (int)jd->w_h[2];
          }
          uVar12 = (uint)jd->width;
          iVar10 = iVar10 + iVar16;
        } while (iVar10 < (int)uVar12);
      }
      iVar18 = iVar18 + 1;
      puVar9 = puVar9 + jd->out_h;
    } while ((int)uVar13 / iVar11 != iVar18);
    uVar13 = (uint)(jd->resize).height;
    iVar11 = (int)jd->w_h[3];
  }
  if ((int)uVar13 % iVar11 != 0) {
    jd->w_h[2] = 0x10;
                    /* Unresolved local var: int w@[???] */
    uVar1 = jd->width;
    jd->w_h[3] = 8;
    iVar11 = 0;
    if (uVar1 != 0) {
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
      do {
        if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
          uVar2 = jd->rsc;
          jd->rsc = uVar2 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          uVar13 = _DAT_fffd4440;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar4 = uVar3 >> 8;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          jd->dctr = jd->dctr - 2;
          if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar13) || (((uVar2 ^ uVar4) & 7) != 0)) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar8 = JPEG_ERR_OK;
        }
        jVar5 = (*_DAT_fffd4470)(jd,4,0,jd->workbuf_y);
        jVar6 = (*_DAT_fffd4484)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd4498)(jd,1,2,jd->workbuf_v);
        uVar13 = (uint)(jd->resize).width;
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        if (iVar11 < (int)uVar13) {
          p_Var14 = jd->idct_y;
          piVar17 = jd->workbuf_y;
          piVar19 = jd->qttbl[jd->qtid[0]];
          piVar20 = jd->y;
          if (iVar11 + 8 < (int)uVar13) {
            (*p_Var14)(piVar17,8,piVar19,piVar20,0x10);
            (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar9 + (uint)jd->pixel * iVar11);
            goto LAB_000145a2;
          }
          jd->w_h[2] = 8;
          jd->w_h[3] = 8;
          (*p_Var14)(piVar17,8,piVar19,piVar20,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar9 + (uint)jd->pixel * iVar15 * 0x10);
          jd->w_h[2] = 0x10;
          jd->w_h[3] = 8;
          iVar18 = 0x10;
        }
        else {
LAB_000145a2:
          iVar18 = (int)jd->w_h[2];
        }
        iVar11 = iVar11 + iVar18;
      } while (iVar11 < (int)(uint)jd->width);
      uVar13 = (uint)(jd->resize).height;
    }
  }
  io->out_size = (jd->resize).width * uVar13 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_90_clipper
 * Purpose: Decodes JPEG MCUs for YUV420 with 90-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000145cc
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_90_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  int iVar11;
  int iVar12;
  uint uVar13;
  idct_func p_Var14;
  uint uVar15;
  int iVar16;
  int16_t *piVar17;
  int iVar18;
  int16_t *piVar19;
  int16_t *piVar20;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int left_y@[???] */
  uVar15 = (uint)(jd->resize).height;
  iVar12 = (int)jd->w_h[2];
  iVar11 = (int)(uint)(jd->resize).width / (int)jd->w_h[3];
  puVar9 = io->outbuf + jd->out_start_pos;
                    /* Unresolved local var: int h@[???] */
  jVar8 = JPEG_ERR_OK;
  if (0 < (int)uVar15 / iVar12) {
    uVar13 = (uint)jd->width;
    jVar8 = JPEG_ERR_OK;
    iVar18 = 0;
    do {
      iVar10 = 0;
      if (uVar13 != 0) {
        do {
                    /* Unresolved local var: int w@[???] */
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar13 = _DAT_fffd4648;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar13) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd4678)(jd,4,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd468c)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd46a0)(jd,1,2,jd->workbuf_v);
          uVar13 = (uint)(jd->resize).width;
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          if (iVar10 < (int)uVar13) {
            piVar20 = jd->y;
            p_Var14 = jd->idct_y;
            piVar17 = jd->workbuf_y;
            piVar19 = jd->qttbl[jd->qtid[0]];
            if (iVar10 + 8 < (int)uVar13) {
              (*p_Var14)(piVar17,8,piVar19,piVar20 + 8,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
              (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
              (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
              (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                                 puVar9 + jd->w_h[0] * iVar10 * (uint)jd->pixel);
              goto LAB_00014814;
            }
            jd->w_h[2] = 0x10;
            jd->w_h[3] = 8;
            (*p_Var14)(piVar17,8,piVar19,piVar20 + 8,0x10);
            (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                               puVar9 + jd->w_h[0] * iVar11 * (uint)jd->pixel * 0x10);
            iVar16 = 0x10;
            jd->w_h[2] = 0x10;
            jd->w_h[3] = 0x10;
          }
          else {
LAB_00014814:
            iVar16 = (int)jd->w_h[3];
          }
          uVar13 = (uint)jd->width;
          iVar10 = iVar10 + iVar16;
        } while (iVar10 < (int)uVar13);
      }
      iVar18 = iVar18 + 1;
      puVar9 = puVar9 + jd->out_h;
    } while ((int)uVar15 / iVar12 != iVar18);
    uVar15 = (uint)(jd->resize).height;
    iVar12 = (int)jd->w_h[2];
  }
  if ((int)uVar15 % iVar12 != 0) {
    jd->w_h[2] = 8;
                    /* Unresolved local var: int w@[???] */
    uVar1 = jd->width;
    jd->w_h[3] = 0x10;
    iVar12 = 0;
    if (uVar1 != 0) {
      do {
        if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
          uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar2 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          uVar15 = _DAT_fffd48bc;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar4 = uVar3 >> 8;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          jd->dctr = jd->dctr - 2;
          if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar15) ||
             (uVar2 = uVar2 ^ uVar4, jVar8 = (uint)uVar2 & 7, (uVar2 & 7) != 0)) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
        }
        jVar5 = (*_DAT_fffd48ec)(jd,4,0,jd->workbuf_y);
        jVar6 = (*_DAT_fffd4900)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd4914)(jd,1,2,jd->workbuf_v);
        uVar15 = (uint)(jd->resize).width;
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        if (iVar12 < (int)uVar15) {
          p_Var14 = jd->idct_y;
          piVar17 = jd->workbuf_y;
          piVar19 = jd->qttbl[jd->qtid[0]];
          piVar20 = jd->y;
          if (iVar12 + 8 < (int)uVar15) {
            (*p_Var14)(piVar17,8,piVar19,piVar20,8);
            (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans_90)
                      (jd->y,jd->u,jd->v,jd->w_h,
                       puVar9 + (jd->w_h[0] * iVar12 + 8) * (uint)jd->pixel);
            goto LAB_00014a28;
          }
          jd->w_h[2] = 8;
          jd->w_h[3] = 8;
          (*p_Var14)(piVar17,8,piVar19,piVar20,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans_90)
                    (jd->y,jd->u,jd->v,jd->w_h,
                     puVar9 + (jd->w_h[0] * iVar11 * 0x10 + 8) * (uint)jd->pixel);
          jd->w_h[2] = 8;
          jd->w_h[3] = 0x10;
          iVar18 = 0x10;
        }
        else {
LAB_00014a28:
          iVar18 = (int)jd->w_h[3];
        }
        iVar12 = iVar12 + iVar18;
      } while (iVar12 < (int)(uint)jd->width);
      uVar15 = (uint)(jd->resize).height;
    }
  }
  io->out_size = (jd->resize).width * uVar15 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_180_clipper
 * Purpose: Decodes JPEG MCUs for YUV420 with 180-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00014a54
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_180_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  short sVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  uint8_t *puVar10;
  int iVar11;
  uint uVar12;
  int iVar13;
  idct_func p_Var14;
  int iVar15;
  int16_t *piVar16;
  int iVar17;
  uint uVar18;
  uint uVar19;
  int16_t *piVar20;
  int16_t *piVar21;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int left_y@[???] */
  uVar19 = (uint)(jd->resize).height;
  iVar13 = (int)jd->w_h[3];
  uVar12 = (uint)(jd->resize).width;
  puVar10 = io->outbuf + jd->out_start_pos;
                    /* Unresolved local var: int h@[???] */
  sVar5 = (short)((int)uVar12 / (int)jd->w_h[2]);
  if ((int)uVar19 / iVar13 < 1) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
                    /* Unresolved local var: int w@[???] */
    iVar17 = 0;
    uVar12 = (uint)jd->width;
    jVar9 = JPEG_ERR_OK;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar11 = 0;
      if (uVar12 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd4ae8;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd4b18)(jd,4,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd4b2c)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd4b40)(jd,1,2,jd->workbuf_v);
          uVar12 = (uint)(jd->resize).width;
          jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
          if (iVar11 < (int)uVar12) {
            p_Var14 = jd->idct_y;
            piVar16 = jd->workbuf_y;
            piVar20 = jd->qttbl[jd->qtid[0]];
            piVar21 = jd->y;
            if (iVar11 + 8 < (int)uVar12) {
              (*p_Var14)(piVar16,8,piVar20,piVar21 + 0x88,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
              (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
              (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
              (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                                 puVar10 + -(int)(short)((ushort)jd->pixel * (short)iVar11));
              goto LAB_00014caf;
            }
            jd->w_h[2] = 8;
            jd->w_h[3] = 0x10;
            (*p_Var14)(piVar16,8,piVar20,piVar21 + 0x40,8);
            (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans_90)
                      (jd->y,jd->u,jd->v,jd->w_h,
                       puVar10 + -(int)(short)((ushort)jd->pixel * (sVar5 * 0x10 + -8)));
            iVar15 = 0x10;
            jd->w_h[2] = 0x10;
            jd->w_h[3] = 0x10;
          }
          else {
LAB_00014caf:
            iVar15 = (int)jd->w_h[2];
          }
          uVar12 = (uint)jd->width;
          iVar11 = iVar11 + iVar15;
        } while (iVar11 < (int)uVar12);
      }
      iVar17 = iVar17 + 1;
      puVar10 = puVar10 + jd->out_h;
    } while ((int)uVar19 / iVar13 != iVar17);
    uVar19 = (uint)(jd->resize).height;
    iVar13 = (int)jd->w_h[3];
    uVar12 = (uint)(jd->resize).width;
  }
  uVar18 = (uint)jd->pixel;
  if ((int)uVar19 % iVar13 != 0) {
    jd->w_h[2] = 0x10;
                    /* Unresolved local var: int w@[???] */
    uVar1 = jd->width;
    jd->w_h[3] = 8;
    if (uVar1 != 0) {
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
      iVar13 = 0;
      do {
        if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
          uVar2 = jd->rsc;
          jd->rsc = uVar2 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          uVar19 = _DAT_fffd4d60;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar4 = uVar3 >> 8;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          jd->dctr = jd->dctr - 2;
          if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar19) || (((uVar2 ^ uVar4) & 7) != 0)) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar9 = JPEG_ERR_OK;
        }
        jVar6 = (*_DAT_fffd4d90)(jd,4,0,jd->workbuf_y);
        jVar7 = (*_DAT_fffd4da4)(jd,1,1,jd->workbuf_u);
        jVar8 = (*_DAT_fffd4db8)(jd,1,2,jd->workbuf_v);
        uVar19 = (uint)(jd->resize).width;
        jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
        if (iVar13 < (int)uVar19) {
          p_Var14 = jd->idct_y;
          piVar16 = jd->workbuf_y;
          piVar20 = jd->qttbl[jd->qtid[0]];
          piVar21 = jd->y;
          if (iVar13 + 8 < (int)uVar19) {
            (*p_Var14)(piVar16,8,piVar20,piVar21 + 8,0x10);
            (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u + 0x20,jd->v + 0x20,jd->w_h,
                               puVar10 + (uVar12 * uVar18 * 8 -
                                         (int)(short)((ushort)jd->pixel * (short)iVar13)));
            goto LAB_00014ed1;
          }
          jd->w_h[2] = 8;
          jd->w_h[3] = 8;
          (*p_Var14)(piVar16,8,piVar20,piVar21,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans_90)
                    (jd->y,jd->u + 0x20,jd->v + 0x20,jd->w_h,
                     puVar10 + (uVar12 * uVar18 * 8 -
                               (int)(short)((ushort)jd->pixel * (sVar5 * 0x10 + -8))));
          jd->w_h[2] = 0x10;
          jd->w_h[3] = 8;
          iVar17 = 0x10;
        }
        else {
LAB_00014ed1:
          iVar17 = (int)jd->w_h[2];
        }
        iVar13 = iVar13 + iVar17;
      } while (iVar13 < (int)(uint)jd->width);
      uVar12 = (uint)(jd->resize).width;
      uVar19 = (uint)(jd->resize).height;
      uVar18 = (uint)jd->pixel;
    }
  }
  io->out_size = uVar12 * uVar19 * uVar18;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_270_clipper
 * Purpose: Decodes JPEG MCUs for YUV420 with 270-degree rotation using the clipped-region path; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00014f00
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_270_clipper(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  uint uVar13;
  idct_func p_Var14;
  int iVar15;
  int iVar16;
  int16_t *piVar17;
  int iVar18;
  int16_t *piVar19;
  int16_t *piVar20;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int left_y@[???] */
  uVar13 = (uint)(jd->resize).height;
  iVar11 = (int)jd->w_h[2];
  puVar9 = io->outbuf + jd->out_start_pos;
  iVar15 = (int)(uint)(jd->resize).width / (int)jd->w_h[3];
                    /* Unresolved local var: int h@[???] */
  if ((int)uVar13 / iVar11 < 1) {
    jVar8 = JPEG_ERR_OK;
  }
  else {
                    /* Unresolved local var: int w@[???] */
    iVar18 = 0;
    uVar12 = (uint)jd->width;
    jVar8 = JPEG_ERR_OK;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      iVar10 = 0;
      if (uVar12 != 0) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar12 = _DAT_fffd4f90;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar12) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd4fc0)(jd,4,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd4fd4)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd4fe8)(jd,1,2,jd->workbuf_v);
          uVar12 = (uint)(jd->resize).width;
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          if (iVar10 < (int)uVar12) {
            p_Var14 = jd->idct_y;
            piVar17 = jd->workbuf_y;
            piVar19 = jd->qttbl[jd->qtid[0]];
            piVar20 = jd->y;
            if (iVar10 + 8 < (int)uVar12) {
              (*p_Var14)(piVar17,8,piVar19,piVar20 + 0x80,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
              (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
              (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
              (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                                 puVar9 + -(jd->w_h[0] * iVar10 * (uint)jd->pixel));
              goto LAB_00015161;
            }
            jd->w_h[2] = 0x10;
            jd->w_h[3] = 8;
            (*p_Var14)(piVar17,8,piVar19,piVar20,0x10);
            (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u + 0x20,jd->v + 0x20,jd->w_h,
                               puVar9 + -((int)jd->w_h[0] * (iVar15 * 0x10 + -8) * (uint)jd->pixel))
            ;
            iVar16 = 0x10;
            jd->w_h[2] = 0x10;
            jd->w_h[3] = 0x10;
          }
          else {
LAB_00015161:
            iVar16 = (int)jd->w_h[3];
          }
          uVar12 = (uint)jd->width;
          iVar10 = iVar10 + iVar16;
        } while (iVar10 < (int)uVar12);
      }
      iVar18 = iVar18 + 1;
      puVar9 = puVar9 + jd->out_h;
    } while ((int)uVar13 / iVar11 != iVar18);
    uVar13 = (uint)(jd->resize).height;
    iVar11 = (int)jd->w_h[2];
  }
  if ((int)uVar13 % iVar11 != 0) {
                    /* Unresolved local var: int w@[???] */
    jd->w_h[2] = 8;
    uVar1 = jd->width;
    jd->w_h[3] = 0x10;
    iVar11 = 0;
    if (uVar1 != 0) {
      do {
        if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
          uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar2 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          uVar13 = _DAT_fffd5204;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar4 = uVar3 >> 8;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          jd->dctr = jd->dctr - 2;
          if ((((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar13) ||
             (uVar2 = uVar2 ^ uVar4, jVar8 = (uint)uVar2 & 7, (uVar2 & 7) != 0)) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
        }
        jVar5 = (*_DAT_fffd5234)(jd,4,0,jd->workbuf_y);
        jVar6 = (*_DAT_fffd5248)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd525c)(jd,1,2,jd->workbuf_v);
        uVar13 = (uint)(jd->resize).width;
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        if (iVar11 < (int)uVar13) {
          p_Var14 = jd->idct_y;
          piVar17 = jd->workbuf_y;
          piVar19 = jd->qttbl[jd->qtid[0]];
          piVar20 = jd->y;
          if (iVar11 + 8 < (int)uVar13) {
            (*p_Var14)(piVar17,8,piVar19,piVar20 + 0x40,8);
            (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,8);
            (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
            (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
            (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                               puVar9 + -(jd->w_h[0] * iVar11 * (uint)jd->pixel));
            goto LAB_00015371;
          }
          jd->w_h[2] = 8;
          jd->w_h[3] = 8;
          (*p_Var14)(piVar17,8,piVar19,piVar20,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u + 0x20,jd->v + 0x20,jd->w_h,
                             puVar9 + -((int)jd->w_h[0] * (iVar15 * 0x10 + -8) * (uint)jd->pixel));
          jd->w_h[2] = 8;
          jd->w_h[3] = 0x10;
          iVar18 = 0x10;
        }
        else {
LAB_00015371:
          iVar18 = (int)jd->w_h[3];
        }
        iVar11 = iVar11 + iVar18;
      } while (iVar11 < (int)(uint)jd->width);
      uVar13 = (uint)(jd->resize).height;
    }
  }
  io->out_size = (jd->resize).width * uVar13 * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_process_scale
 * Purpose: General scaled decode path handling reduced IDCT dimensions, clipping, rotation and output-format dispatch.
 * Entry: 0001539c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

int jpeg_dec_process_scale(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  short *psVar1;
  short sVar2;
  uint16_t uVar3;
  ushort uVar4;
  ushort uVar5;
  ushort uVar6;
  int16_t **ppiVar7;
  uint uVar8;
  uint uVar9;
  uint uVar10;
  int16_t **ppiVar11;
  int iVar12;
  int16_t **ppiVar13;
  uint uVar14;
  short *psVar15;
  short *psVar16;
  uint uVar17;
  int16_t *piVar18;
  int iVar19;
  short *psVar20;
  int iVar21;
  int16_t *piVar22;
  short *psVar23;
  int iVar24;
  int16_t *piVar25;
  short sVar26;
  int16_t *piVar27;
  int iVar28;
  uint local_70;
  int16_t **ppiStack_6c;
  int iStack_68;
  int iStack_64;
  uint uStack_60;
  int iStack_58;
  uint uStack_54;
  uint uStack_48;
  uint8_t *puStack_44;
  int iStack_40;

                    /* Unresolved local var: uint16_t x@[???]
                       Unresolved local var: uint16_t y@[???]
                       Unresolved local var: int ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int16_t height_loop@[???]
                       Unresolved local var: int16_t width_loop@[???]
                       Unresolved local var: int16_t y_index@[???]
                       Unresolved local var: int16_t x_index@[???]
                       Unresolved local var: int16_t r_index@[???]
                       Unresolved local var: int16_t * scale_ybuf@[???]
                       Unresolved local var: int y_index1@[???]
                       Unresolved local var: int x_index1@[???] */
  psVar20 = jd->scale_ybuf;
  puStack_44 = io->outbuf + jd->out_start_pos;
  local_70 = (uint)jd->process_id;
  uVar17 = (int)(jd->msy * 8 + (uint)jd->height + -1) / ((int)jd->msy << 3);
  uVar14 = (int)(jd->msx * 8 + (uint)jd->width + -1) / ((int)jd->msx << 3);
  if (jd->rotate == JPEG_ROTATE_180D) {
    if (local_70 == 2) {
      uVar17 = uVar17 & 0xffff;
      if ((*psVar20 != -1) && (uVar17 != 0)) {
                    /* Unresolved local var: int16_t * scale_xbuf@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        iStack_68 = 0;
        iVar12 = 8;
        iVar19 = 0;
        local_70 = 0;
        do {
          if ((uVar14 & 0xffff) != 0) {
            piVar18 = jd->scale_xbuf;
            iStack_58 = 0;
            uStack_48 = uVar14 & 0xffff;
            do {
              if ((jd->nrst != 0) && (uVar3 = jd->rst, jd->rst = uVar3 + 1, jd->nrst == uVar3)) {
                uVar4 = jd->rsc;
                jd->rsc = uVar4 + 1;
                jd->bits_left = 0;
                jd->get_buffer = 0;
                if ((uint)jd->dctr < 2) {
                  return -3;
                }
                uVar5 = *(ushort *)jd->dptr;
                uVar6 = uVar5 >> 8;
                jd->dptr = (uint8_t *)((int)jd->dptr + 2);
                uVar8 = _DAT_fffd5964;
                jd->dctr = jd->dctr - 2;
                if (((uVar5 & 0xff) << 8 | uVar6 & 0xd8) != uVar8) {
                  return -5;
                }
                uVar4 = uVar4 ^ uVar6;
                local_70 = uVar4 & 7;
                if ((uVar4 & 7) != 0) {
                  return -5;
                }
                jd->dcv[0] = 0;
                jd->dcv[1] = 0;
                jd->dcv[2] = 0;
                jd->rst = 1;
              }
              uVar8 = (*_DAT_fffd5994)(jd,2,0,jd->workbuf_y);
              uVar9 = (*_DAT_fffd59ac)(jd,1,1,jd->workbuf_u);
              uVar10 = (*_DAT_fffd59bc)(jd,1,2,jd->workbuf_v);
              local_70 = local_70 | uVar8 | uVar9 | uVar10;
              if (piVar18[iStack_58] != -1) {
                (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
                (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
                (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
                (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
                ppiVar11 = jd->y_data + iVar19;
                ppiVar7 = jd->v_data + iVar19;
                ppiStack_6c = jd->u_data + iVar19;
                iVar24 = (int)psVar20[iVar19];
                psVar23 = psVar20 + iVar19;
                iStack_68 = iVar19;
                do {
                  piVar27 = *ppiVar11;
                  piVar25 = *ppiStack_6c;
                  piVar22 = *ppiVar7;
                  psVar15 = piVar18 + iStack_58;
                  iVar28 = iStack_58;
                  while( true ) {
                    piVar27[((jd->resize).width - 1) - iVar28] = jd->y_t[iVar24][*psVar15];
                    piVar25[((jd->resize).width - 1) - iVar28] =
                         jd->u_t[*psVar23][(int)*psVar15 >> 1];
                    piVar22[((jd->resize).width - 1) - iVar28] =
                         jd->v_t[*psVar23][(int)*psVar15 >> 1];
                    sVar26 = *psVar15;
                    psVar1 = psVar15 + 1;
                    iVar28 = iVar28 + 1;
                    psVar15 = psVar15 + 1;
                    if (*psVar1 <= sVar26) break;
                    iVar24 = (int)*psVar23;
                  }
                  iStack_68 = iStack_68 + 1;
                  iVar24 = (int)psVar23[1];
                  ppiStack_6c = ppiStack_6c + 1;
                  sVar26 = *psVar23;
                  ppiVar11 = ppiVar11 + 1;
                  ppiVar7 = ppiVar7 + 1;
                  psVar23 = psVar23 + 1;
                } while (sVar26 < iVar24);
                iStack_58 = (int)(short)iVar28;
              }
              uStack_48 = uStack_48 - 1 & 0xffff;
            } while (uStack_48 != 0);
          }
          iVar24 = _DAT_fffd5b44;
          iVar19 = (int)(short)iStack_68;
          if (iVar12 <= iVar19) {
            do {
              iVar28 = iVar12 + iVar24;
              (*jd->color_trans_scale)
                        (jd->y_data[iVar28],jd->u_data[iVar28],jd->v_data[iVar28],jd->w_h,puStack_44
                        );
              iVar12 = (int)(short)((short)iVar12 + 8);
              puStack_44 = puStack_44 + jd->out_h;
            } while (iVar12 <= iVar19);
          }
          uVar17 = uVar17 - 1 & 0xffff;
        } while ((psVar20[iVar19] != -1) && (uVar17 != 0));
        goto LAB_000169a8;
      }
    }
    else if (local_70 < 3) {
      if (local_70 == 0) {
        uVar17 = uVar17 & 0xffff;
        if ((*psVar20 != -1) && (uVar17 != 0)) {
                    /* Unresolved local var: int16_t * scale_xbuf@[???] */
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          iVar19 = 8;
          iVar12 = 0;
          iStack_68 = 0;
          do {
            if ((uVar14 & 0xffff) != 0) {
              piVar18 = jd->scale_xbuf;
              iVar24 = 0;
              uVar8 = uVar14 & 0xffff;
              do {
                if ((jd->nrst != 0) && (uVar3 = jd->rst, jd->rst = uVar3 + 1, jd->nrst == uVar3)) {
                  uVar4 = jd->rsc;
                  jd->rsc = uVar4 + 1;
                  jd->bits_left = 0;
                  jd->get_buffer = 0;
                  if ((uint)jd->dctr < 2) {
                    return -3;
                  }
                  uVar5 = *(ushort *)jd->dptr;
                  uVar6 = uVar5 >> 8;
                  jd->dptr = (uint8_t *)((int)jd->dptr + 2);
                  uVar9 = _DAT_fffd549c;
                  jd->dctr = jd->dctr - 2;
                  if (((uVar5 & 0xff) << 8 | uVar6 & 0xd8) != uVar9) {
                    return -5;
                  }
                  uVar4 = uVar4 ^ uVar6;
                  local_70 = uVar4 & 7;
                  if ((uVar4 & 7) != 0) {
                    return -5;
                  }
                  jd->dcv[0] = 0;
                  jd->dcv[1] = 0;
                  jd->dcv[2] = 0;
                  jd->rst = 1;
                }
                uVar9 = (*_DAT_fffd54d0)(jd,1,0,jd->workbuf_y);
                local_70 = local_70 | uVar9;
                if (piVar18[iVar24] != -1) {
                  (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
                  iVar28 = (int)psVar20[iStack_68];
                  ppiVar11 = jd->y_data + iStack_68;
                  iVar12 = iStack_68;
                  psVar23 = psVar20 + iStack_68;
                  do {
                    piVar22 = *ppiVar11;
                    psVar15 = piVar18 + iVar24;
                    iVar21 = iVar24;
                    while( true ) {
                      piVar22[((jd->resize).width - 1) - iVar21] = jd->y_t[iVar28][*psVar15];
                      sVar26 = *psVar15;
                      psVar1 = psVar15 + 1;
                      iVar21 = iVar21 + 1;
                      psVar15 = psVar15 + 1;
                      if (*psVar1 <= sVar26) break;
                      iVar28 = (int)*psVar23;
                    }
                    iVar28 = (int)psVar23[1];
                    sVar26 = *psVar23;
                    iVar12 = iVar12 + 1;
                    ppiVar11 = ppiVar11 + 1;
                    psVar23 = psVar23 + 1;
                  } while (sVar26 < iVar28);
                  iVar24 = (int)(short)iVar21;
                }
                uVar8 = uVar8 - 1 & 0xffff;
              } while (uVar8 != 0);
            }
            iVar24 = _DAT_fffd557c;
            iStack_68 = (int)(short)iVar12;
            if (iVar19 <= iStack_68) {
                    /* Unresolved local var: int16_t * tmpu@[???]
                       Unresolved local var: int16_t * tmpv@[???]
                       Unresolved local var: int i@[???] */
              do {
                iVar21 = iVar19 + iVar24;
                piVar25 = jd->u_data[iVar21];
                piVar27 = jd->v_data[iVar21];
                iVar28 = 0;
                piVar18 = piVar27;
                piVar22 = piVar25;
                if (0 < (int)jd->w_h[2] << 3) {
                  do {
                    *piVar22 = 0x80;
                    *piVar18 = 0x80;
                    iVar28 = iVar28 + 1;
                    piVar22 = piVar22 + 1;
                    piVar18 = piVar18 + 1;
                  } while (iVar28 < (int)jd->w_h[2] << 3);
                }
                (*jd->color_trans_scale)(jd->y_data[iVar21],piVar25,piVar27,jd->w_h,puStack_44);
                puStack_44 = puStack_44 + jd->out_h;
                iVar19 = (int)(short)((short)iVar19 + 8);
              } while (iVar19 <= iStack_68);
            }
            uVar17 = uVar17 - 1 & 0xffff;
          } while ((psVar20[iStack_68] != -1) && (uVar17 != 0));
          goto LAB_000169a8;
        }
      }
      else {
        uVar17 = uVar17 & 0xffff;
        if ((*psVar20 != -1) && (uVar17 != 0)) {
                    /* Unresolved local var: int16_t * scale_xbuf@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          iStack_68 = 0;
          iVar12 = 8;
          iVar19 = 0;
          local_70 = 0;
          do {
            if ((uVar14 & 0xffff) != 0) {
              piVar18 = jd->scale_xbuf;
              iStack_58 = 0;
              uStack_48 = uVar14 & 0xffff;
              do {
                if ((jd->nrst != 0) && (uVar3 = jd->rst, jd->rst = uVar3 + 1, jd->nrst == uVar3)) {
                  uVar4 = jd->rsc;
                  jd->rsc = uVar4 + 1;
                  jd->bits_left = 0;
                  jd->get_buffer = 0;
                  if ((uint)jd->dctr < 2) {
                    return -3;
                  }
                  uVar5 = *(ushort *)jd->dptr;
                  uVar6 = uVar5 >> 8;
                  jd->dptr = (uint8_t *)((int)jd->dptr + 2);
                  uVar8 = _DAT_fffd56b4;
                  jd->dctr = jd->dctr - 2;
                  if (((uVar5 & 0xff) << 8 | uVar6 & 0xd8) != uVar8) {
                    return -5;
                  }
                  uVar4 = uVar4 ^ uVar6;
                  local_70 = uVar4 & 7;
                  if ((uVar4 & 7) != 0) {
                    return -5;
                  }
                  jd->dcv[0] = 0;
                  jd->dcv[1] = 0;
                  jd->dcv[2] = 0;
                  jd->rst = 1;
                }
                uVar8 = (*_DAT_fffd56e4)(jd,1,0,jd->workbuf_y);
                uVar9 = (*_DAT_fffd56fc)(jd,1,1,jd->workbuf_u);
                uVar10 = (*_DAT_fffd570c)(jd,1,2,jd->workbuf_v);
                local_70 = local_70 | uVar8 | uVar9 | uVar10;
                if (piVar18[iStack_58] != -1) {
                  (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
                  (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
                  (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
                  ppiStack_6c = jd->y_data + iVar19;
                  iVar24 = (int)psVar20[iVar19];
                  ppiVar7 = jd->u_data + iVar19;
                  ppiVar11 = jd->v_data + iVar19;
                  psVar23 = psVar20 + iVar19;
                  iStack_68 = iVar19;
                  do {
                    piVar25 = *ppiVar7;
                    piVar27 = *ppiStack_6c;
                    piVar22 = *ppiVar11;
                    psVar15 = piVar18 + iStack_58;
                    iVar28 = iStack_58;
                    while( true ) {
                      piVar27[((jd->resize).width - 1) - iVar28] = jd->y_t[iVar24][*psVar15];
                      piVar25[((jd->resize).width - 1) - iVar28] = jd->u_t[*psVar23][*psVar15];
                      piVar22[((jd->resize).width - 1) - iVar28] = jd->v_t[*psVar23][*psVar15];
                      sVar26 = *psVar15;
                      psVar1 = psVar15 + 1;
                      iVar28 = iVar28 + 1;
                      psVar15 = psVar15 + 1;
                      if (*psVar1 <= sVar26) break;
                      iVar24 = (int)*psVar23;
                    }
                    iStack_68 = iStack_68 + 1;
                    iVar24 = (int)psVar23[1];
                    ppiStack_6c = ppiStack_6c + 1;
                    sVar26 = *psVar23;
                    ppiVar7 = ppiVar7 + 1;
                    ppiVar11 = ppiVar11 + 1;
                    psVar23 = psVar23 + 1;
                  } while (sVar26 < iVar24);
                  iStack_58 = (int)(short)iVar28;
                }
                uStack_48 = uStack_48 - 1 & 0xffff;
              } while (uStack_48 != 0);
            }
            iVar24 = _DAT_fffd5868;
            iVar19 = (int)(short)iStack_68;
            if (iVar12 <= iVar19) {
              do {
                iVar28 = iVar12 + iVar24;
                (*jd->color_trans_scale)
                          (jd->y_data[iVar28],jd->u_data[iVar28],jd->v_data[iVar28],jd->w_h,
                           puStack_44);
                iVar12 = (int)(short)((short)iVar12 + 8);
                puStack_44 = puStack_44 + jd->out_h;
              } while (iVar12 <= iVar19);
            }
            uVar17 = uVar17 - 1 & 0xffff;
          } while ((psVar20[iVar19] != -1) && (uVar17 != 0));
          goto LAB_000169a8;
        }
      }
    }
    else {
      if (local_70 != 3) {
        return -1;
      }
      uVar17 = uVar17 & 0xffff;
      if ((*psVar20 != -1) && (uVar17 != 0)) {
                    /* Unresolved local var: int16_t * scale_xbuf@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        iStack_68 = 0;
        iVar12 = 8;
        iVar19 = 0;
        local_70 = 0;
        do {
          if ((uVar14 & 0xffff) != 0) {
            piVar18 = jd->scale_xbuf;
            iStack_58 = 0;
            uStack_48 = uVar14 & 0xffff;
            do {
              if ((jd->nrst != 0) && (uVar3 = jd->rst, jd->rst = uVar3 + 1, jd->nrst == uVar3)) {
                uVar4 = jd->rsc;
                jd->rsc = uVar4 + 1;
                jd->bits_left = 0;
                jd->get_buffer = 0;
                if ((uint)jd->dctr < 2) {
                  return -3;
                }
                uVar5 = *(ushort *)jd->dptr;
                uVar6 = uVar5 >> 8;
                jd->dptr = (uint8_t *)((int)jd->dptr + 2);
                uVar8 = _DAT_fffd5c40;
                jd->dctr = jd->dctr - 2;
                if (((uVar5 & 0xff) << 8 | uVar6 & 0xd8) != uVar8) {
                  return -5;
                }
                uVar4 = uVar4 ^ uVar6;
                local_70 = uVar4 & 7;
                if ((uVar4 & 7) != 0) {
                  return -5;
                }
                jd->dcv[0] = 0;
                jd->dcv[1] = 0;
                jd->dcv[2] = 0;
                jd->rst = 1;
              }
              uVar8 = (*_DAT_fffd5c70)(jd,4,0,jd->workbuf_y);
              uVar9 = (*_DAT_fffd5c88)(jd,1,1,jd->workbuf_u);
              uVar10 = (*_DAT_fffd5c98)(jd,1,2,jd->workbuf_v);
              local_70 = local_70 | uVar8 | uVar9 | uVar10;
              if (piVar18[iStack_58] != -1) {
                (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
                (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
                (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
                (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
                (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
                (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
                ppiStack_6c = jd->v_data + iVar19;
                iVar24 = (int)psVar20[iVar19];
                ppiVar7 = jd->y_data + iVar19;
                ppiVar11 = jd->u_data + iVar19;
                psVar23 = psVar20 + iVar19;
                iStack_68 = iVar19;
                do {
                  piVar27 = *ppiVar7;
                  piVar22 = *ppiStack_6c;
                  piVar25 = *ppiVar11;
                  psVar15 = piVar18 + iStack_58;
                  iVar28 = iStack_58;
                  while( true ) {
                    piVar27[((jd->resize).width - 1) - iVar28] = jd->y_t[iVar24][*psVar15];
                    piVar25[((jd->resize).width - 1) - iVar28] =
                         jd->u_t[(int)*psVar23 >> 1][(int)*psVar15 >> 1];
                    piVar22[((jd->resize).width - 1) - iVar28] =
                         jd->v_t[(int)*psVar23 >> 1][(int)*psVar15 >> 1];
                    sVar26 = *psVar15;
                    psVar1 = psVar15 + 1;
                    iVar28 = iVar28 + 1;
                    psVar15 = psVar15 + 1;
                    if (*psVar1 <= sVar26) break;
                    iVar24 = (int)*psVar23;
                  }
                  iStack_68 = iStack_68 + 1;
                  iVar24 = (int)psVar23[1];
                  ppiStack_6c = ppiStack_6c + 1;
                  sVar26 = *psVar23;
                  ppiVar7 = ppiVar7 + 1;
                  ppiVar11 = ppiVar11 + 1;
                  psVar23 = psVar23 + 1;
                } while (sVar26 < iVar24);
                iStack_58 = (int)(short)iVar28;
              }
              uStack_48 = uStack_48 - 1 & 0xffff;
            } while (uStack_48 != 0);
          }
          iVar24 = _DAT_fffd5e64;
          iVar19 = (int)(short)iStack_68;
          if (iVar12 <= iVar19) {
            do {
              iVar28 = iVar12 + iVar24;
              (*jd->color_trans_scale)
                        (jd->y_data[iVar28],jd->u_data[iVar28],jd->v_data[iVar28],jd->w_h,puStack_44
                        );
              iVar12 = (int)(short)((short)iVar12 + 8);
              puStack_44 = puStack_44 + jd->out_h;
            } while (iVar12 <= iVar19);
          }
          uVar17 = uVar17 - 1 & 0xffff;
        } while ((psVar20[iVar19] != -1) && (uVar17 != 0));
        goto LAB_000169a8;
      }
    }
  }
  else if (local_70 == 2) {
    uVar17 = uVar17 & 0xffff;
    if ((*psVar20 != -1) && (uVar17 != 0)) {
                    /* Unresolved local var: int16_t * scale_xbuf@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
      ppiStack_6c = (int16_t **)0x0;
      iVar12 = 8;
      iStack_40 = 0;
      local_70 = 0;
      do {
        if ((uVar14 & 0xffff) != 0) {
          piVar18 = jd->scale_xbuf;
          uStack_60 = 0;
          uStack_54 = uVar14 & 0xffff;
          do {
            if ((jd->nrst != 0) && (uVar3 = jd->rst, jd->rst = uVar3 + 1, jd->nrst == uVar3)) {
              uVar4 = jd->rsc;
              jd->rsc = uVar4 + 1;
              jd->bits_left = 0;
              jd->get_buffer = 0;
              if ((uint)jd->dctr < 2) {
                return -3;
              }
              uVar5 = *(ushort *)jd->dptr;
              uVar6 = uVar5 >> 8;
              jd->dctr = jd->dctr - 2;
              uVar8 = _DAT_fffd645c;
              jd->dptr = (uint8_t *)((int)jd->dptr + 2);
              if (((uVar5 & 0xff) << 8 | uVar6 & 0xd8) != uVar8) {
                return -5;
              }
              uVar4 = uVar4 ^ uVar6;
              local_70 = uVar4 & 7;
              if ((uVar4 & 7) != 0) {
                return -5;
              }
              jd->dcv[0] = 0;
              jd->dcv[1] = 0;
              jd->dcv[2] = 0;
              jd->rst = 1;
            }
            uVar8 = (*_DAT_fffd648c)(jd,2,0,jd->workbuf_y);
            uVar9 = (*_DAT_fffd64a4)(jd,1,1,jd->workbuf_u);
            uVar10 = (*_DAT_fffd64b4)(jd,1,2,jd->workbuf_v);
            local_70 = local_70 | uVar8 | uVar9 | uVar10;
            if (piVar18[uStack_60] != -1) {
              (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
              (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
              (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
              ppiStack_6c = (int16_t **)iStack_40;
              iVar19 = (int)psVar20[iStack_40];
              ppiVar7 = jd->y_data + iStack_40;
              ppiVar13 = jd->u_data + iStack_40;
              ppiVar11 = jd->v_data + iStack_40;
              psVar23 = psVar20 + iStack_40;
              do {
                piVar27 = *ppiVar13 + uStack_60;
                piVar22 = *ppiVar7 + uStack_60;
                piVar25 = *ppiVar11 + uStack_60;
                psVar15 = piVar18 + uStack_60;
                uVar8 = uStack_60;
                while( true ) {
                  uVar8 = uVar8 + 1;
                  *piVar22 = jd->y_t[iVar19][*psVar15];
                  piVar22 = piVar22 + 1;
                  *piVar27 = jd->u_t[*psVar23][(int)*psVar15 >> 1];
                  piVar27 = piVar27 + 1;
                  *piVar25 = jd->v_t[*psVar23][(int)*psVar15 >> 1];
                  piVar25 = piVar25 + 1;
                  if (psVar15[1] <= *psVar15) break;
                  iVar19 = (int)*psVar23;
                  psVar15 = psVar15 + 1;
                }
                iVar19 = (int)psVar23[1];
                ppiStack_6c = (int16_t **)((int)ppiStack_6c + 1);
                sVar26 = *psVar23;
                ppiVar7 = ppiVar7 + 1;
                ppiVar13 = ppiVar13 + 1;
                ppiVar11 = ppiVar11 + 1;
                psVar23 = psVar23 + 1;
              } while (sVar26 < iVar19);
              uStack_60 = (uint)(short)uVar8;
            }
            uStack_54 = uStack_54 - 1 & 0xffff;
          } while (uStack_54 != 0);
        }
        iVar19 = _DAT_fffd6624;
        iStack_40 = (int)(short)ppiStack_6c;
        if (iVar12 <= iStack_40) {
          do {
            iVar24 = iVar12 + iVar19;
            (*jd->color_trans_scale)
                      (jd->y_data[iVar24],jd->u_data[iVar24],jd->v_data[iVar24],jd->w_h,puStack_44);
            iVar12 = (int)(short)((short)iVar12 + 8);
            puStack_44 = puStack_44 + jd->out_h;
          } while (iVar12 <= iStack_40);
        }
        uVar17 = uVar17 - 1 & 0xffff;
      } while ((psVar20[iStack_40] != -1) && (uVar17 != 0));
      goto LAB_000169a8;
    }
  }
  else if (local_70 < 3) {
    if (local_70 == 0) {
      uVar17 = uVar17 & 0xffff;
      if ((*psVar20 != -1) && (uVar17 != 0)) {
                    /* Unresolved local var: int16_t * scale_xbuf@[???] */
        iVar12 = 8;
        iStack_64 = 0;
        iStack_58 = 0;
        do {
          if ((uVar14 & 0xffff) != 0) {
            piVar18 = jd->scale_xbuf;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
            iVar19 = 0;
            uStack_60 = uVar14 & 0xffff;
            do {
              if ((jd->nrst != 0) && (uVar3 = jd->rst, jd->rst = uVar3 + 1, jd->nrst == uVar3)) {
                uVar4 = jd->rsc;
                jd->rsc = uVar4 + 1;
                jd->bits_left = 0;
                jd->get_buffer = 0;
                if ((uint)jd->dctr < 2) {
                  return -3;
                }
                uVar5 = *(ushort *)jd->dptr;
                jd->dctr = jd->dctr - 2;
                uVar8 = _DAT_fffd5f78;
                uVar9 = (uint)uVar5 << 8 | (uint)(uVar5 >> 8);
                jd->dptr = (uint8_t *)((int)jd->dptr + 2);
                if (((uVar9 & uVar8) != _DAT_fffd5f88) || (((uVar4 ^ uVar9) & 7) != 0)) {
                  return -5;
                }
                jd->dcv[0] = 0;
                jd->dcv[1] = 0;
                jd->dcv[2] = 0;
                jd->rst = 1;
                local_70 = 0;
              }
              uVar8 = (*_DAT_fffd5fb4)(jd,1,0,jd->workbuf_y);
              local_70 = local_70 | uVar8;
              if (piVar18[iVar19] != -1) {
                (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
                iVar24 = (int)psVar20[iStack_58];
                ppiVar11 = jd->y_data + iStack_58;
                iStack_64 = iStack_58;
                psVar23 = psVar20 + iStack_58;
                do {
                  piVar22 = *ppiVar11;
                  piVar22[iVar19] = jd->y_t[iVar24][piVar18[iVar19]];
                  sVar26 = (short)(iVar19 + 1);
                  if (piVar18[iVar19] < piVar18[iVar19 + 1]) {
                    piVar22 = piVar22 + iVar19;
                    psVar15 = piVar18 + iVar19;
                    iVar24 = iVar19 + 1;
                    do {
                      iVar24 = iVar24 + 1;
                      sVar26 = (short)iVar24;
                      psVar16 = psVar15 + 1;
                      piVar22[1] = jd->y_t[*psVar23][psVar15[1]];
                      psVar1 = psVar15 + 2;
                      piVar22 = piVar22 + 1;
                      psVar15 = psVar16;
                    } while (*psVar16 < *psVar1);
                  }
                  iVar24 = (int)psVar23[1];
                  sVar2 = *psVar23;
                  iStack_64 = iStack_64 + 1;
                  ppiVar11 = ppiVar11 + 1;
                  psVar23 = psVar23 + 1;
                } while (sVar2 < iVar24);
                iVar19 = (int)sVar26;
              }
              uStack_60 = uStack_60 - 1 & 0xffff;
            } while (uStack_60 != 0);
          }
          iVar19 = _DAT_fffd6090;
          iStack_58 = (int)(short)iStack_64;
          if (iVar12 <= iStack_58) {
                    /* Unresolved local var: int16_t * tmpu@[???]
                       Unresolved local var: int16_t * tmpv@[???]
                       Unresolved local var: int i@[???] */
            do {
              iVar28 = iVar12 + iVar19;
              piVar25 = jd->u_data[iVar28];
              piVar27 = jd->v_data[iVar28];
              iVar24 = 0;
              piVar18 = piVar27;
              piVar22 = piVar25;
              if (0 < (int)jd->w_h[2] << 3) {
                do {
                  *piVar22 = 0x80;
                  *piVar18 = 0x80;
                  iVar24 = iVar24 + 1;
                  piVar22 = piVar22 + 1;
                  piVar18 = piVar18 + 1;
                } while (iVar24 < (int)jd->w_h[2] << 3);
              }
              (*jd->color_trans_scale)(jd->y_data[iVar28],piVar25,piVar27,jd->w_h,puStack_44);
              puStack_44 = puStack_44 + jd->out_h;
              iVar12 = (int)(short)((short)iVar12 + 8);
            } while (iVar12 <= iStack_58);
          }
          uVar17 = uVar17 - 1 & 0xffff;
        } while ((psVar20[iStack_58] != -1) && (uVar17 != 0));
        goto LAB_000169a8;
      }
    }
    else {
      uVar17 = uVar17 & 0xffff;
      if ((*psVar20 != -1) && (uVar17 != 0)) {
        ppiStack_6c = (int16_t **)0x0;
        iVar12 = 8;
        iVar19 = 0;
        local_70 = 0;
                    /* Unresolved local var: int16_t * scale_xbuf@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        do {
          if ((uVar14 & 0xffff) != 0) {
            piVar18 = jd->scale_xbuf;
            uStack_60 = 0;
            uStack_54 = uVar14 & 0xffff;
            do {
              if ((jd->nrst != 0) && (uVar3 = jd->rst, jd->rst = uVar3 + 1, jd->nrst == uVar3)) {
                uVar4 = jd->rsc;
                jd->rsc = uVar4 + 1;
                jd->bits_left = 0;
                jd->get_buffer = 0;
                if ((uint)jd->dctr < 2) {
                  return -3;
                }
                uVar5 = *(ushort *)jd->dptr;
                uVar6 = uVar5 >> 8;
                jd->dctr = jd->dctr - 2;
                uVar8 = _DAT_fffd61c0;
                jd->dptr = (uint8_t *)((int)jd->dptr + 2);
                if (((uVar5 & 0xff) << 8 | uVar6 & 0xd8) != uVar8) {
                  return -5;
                }
                uVar4 = uVar4 ^ uVar6;
                local_70 = uVar4 & 7;
                if ((uVar4 & 7) != 0) {
                  return -5;
                }
                jd->dcv[0] = 0;
                jd->dcv[1] = 0;
                jd->dcv[2] = 0;
                jd->rst = 1;
              }
              uVar8 = (*_DAT_fffd61f0)(jd,1,0,jd->workbuf_y);
              uVar9 = (*_DAT_fffd6208)(jd,1,1,jd->workbuf_u);
              uVar10 = (*_DAT_fffd6218)(jd,1,2,jd->workbuf_v);
              local_70 = local_70 | uVar8 | uVar9 | uVar10;
              if (piVar18[uStack_60] != -1) {
                (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
                (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
                (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
                iVar24 = (int)psVar20[iVar19];
                ppiVar11 = jd->y_data + iVar19;
                ppiVar7 = jd->u_data + iVar19;
                ppiVar13 = jd->v_data + iVar19;
                psVar23 = psVar20 + iVar19;
                ppiStack_6c = (int16_t **)iVar19;
                do {
                  piVar27 = *ppiVar7 + uStack_60;
                  piVar22 = *ppiVar11 + uStack_60;
                  piVar25 = *ppiVar13 + uStack_60;
                  psVar15 = piVar18 + uStack_60;
                  uVar8 = uStack_60;
                  while( true ) {
                    uVar8 = uVar8 + 1;
                    *piVar22 = jd->y_t[iVar24][*psVar15];
                    piVar22 = piVar22 + 1;
                    *piVar27 = jd->u_t[*psVar23][*psVar15];
                    piVar27 = piVar27 + 1;
                    *piVar25 = jd->v_t[*psVar23][*psVar15];
                    piVar25 = piVar25 + 1;
                    if (psVar15[1] <= *psVar15) break;
                    iVar24 = (int)*psVar23;
                    psVar15 = psVar15 + 1;
                  }
                  iVar24 = (int)psVar23[1];
                  ppiStack_6c = (int16_t **)((int)ppiStack_6c + 1);
                  sVar26 = *psVar23;
                  ppiVar11 = ppiVar11 + 1;
                  ppiVar7 = ppiVar7 + 1;
                  ppiVar13 = ppiVar13 + 1;
                  psVar23 = psVar23 + 1;
                } while (sVar26 < iVar24);
                uStack_60 = (uint)(short)uVar8;
              }
              uStack_54 = uStack_54 - 1 & 0xffff;
            } while (uStack_54 != 0);
          }
          iVar24 = _DAT_fffd6358;
          iVar19 = (int)(short)ppiStack_6c;
          if (iVar12 <= iVar19) {
            do {
              iVar28 = iVar12 + iVar24;
              (*jd->color_trans_scale)
                        (jd->y_data[iVar28],jd->u_data[iVar28],jd->v_data[iVar28],jd->w_h,puStack_44
                        );
              iVar12 = (int)(short)((short)iVar12 + 8);
              puStack_44 = puStack_44 + jd->out_h;
            } while (iVar12 <= iVar19);
          }
          uVar17 = uVar17 - 1 & 0xffff;
        } while ((psVar20[iVar19] != -1) && (uVar17 != 0));
        goto LAB_000169a8;
      }
    }
  }
  else {
    if (local_70 != 3) {
      return -1;
    }
    uVar17 = uVar17 & 0xffff;
    if ((*psVar20 != -1) && (uVar17 != 0)) {
                    /* Unresolved local var: int16_t * scale_xbuf@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
      ppiStack_6c = (int16_t **)0x0;
      iVar12 = 8;
      iVar19 = 0;
      local_70 = 0;
      do {
        if ((uVar14 & 0xffff) != 0) {
          piVar18 = jd->scale_xbuf;
          uStack_60 = 0;
          uStack_54 = uVar14 & 0xffff;
          do {
            if ((jd->nrst != 0) && (uVar3 = jd->rst, jd->rst = uVar3 + 1, jd->nrst == uVar3)) {
              uVar4 = jd->rsc;
              jd->rsc = uVar4 + 1;
              jd->bits_left = 0;
              jd->get_buffer = 0;
              if ((uint)jd->dctr < 2) {
                return -3;
              }
              uVar5 = *(ushort *)jd->dptr;
              uVar6 = uVar5 >> 8;
              jd->dctr = jd->dctr - 2;
              uVar8 = _DAT_fffd6718;
              jd->dptr = (uint8_t *)((int)jd->dptr + 2);
              if (((uVar5 & 0xff) << 8 | uVar6 & 0xd8) != uVar8) {
                return -5;
              }
              uVar4 = uVar4 ^ uVar6;
              local_70 = uVar4 & 7;
              if ((uVar4 & 7) != 0) {
                return -5;
              }
              jd->dcv[0] = 0;
              jd->dcv[1] = 0;
              jd->dcv[2] = 0;
              jd->rst = 1;
            }
            uVar8 = (*_DAT_fffd6748)(jd,4,0,jd->workbuf_y);
            uVar9 = (*_DAT_fffd6760)(jd,1,1,jd->workbuf_u);
            uVar10 = (*_DAT_fffd6770)(jd,1,2,jd->workbuf_v);
            local_70 = local_70 | uVar8 | uVar9 | uVar10;
            if (piVar18[uStack_60] != -1) {
              (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
              (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
              (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
              (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
              iVar24 = (int)psVar20[iVar19];
              ppiVar11 = jd->u_data + iVar19;
              ppiVar7 = jd->y_data + iVar19;
              ppiVar13 = jd->v_data + iVar19;
              psVar23 = psVar20 + iVar19;
              ppiStack_6c = (int16_t **)iVar19;
              do {
                piVar22 = *ppiVar11 + uStack_60;
                piVar25 = *ppiVar7 + uStack_60;
                piVar27 = *ppiVar13 + uStack_60;
                psVar15 = piVar18 + uStack_60;
                uVar8 = uStack_60;
                while( true ) {
                  uVar8 = uVar8 + 1;
                  *piVar25 = jd->y_t[iVar24][*psVar15];
                  piVar25 = piVar25 + 1;
                  *piVar22 = jd->u_t[(int)*psVar23 >> 1][(int)*psVar15 >> 1];
                  piVar22 = piVar22 + 1;
                  *piVar27 = jd->v_t[(int)*psVar23 >> 1][(int)*psVar15 >> 1];
                  piVar27 = piVar27 + 1;
                  if (psVar15[1] <= *psVar15) break;
                  iVar24 = (int)*psVar23;
                  psVar15 = psVar15 + 1;
                }
                iVar24 = (int)psVar23[1];
                ppiStack_6c = (int16_t **)((int)ppiStack_6c + 1);
                sVar26 = *psVar23;
                ppiVar7 = ppiVar7 + 1;
                ppiVar11 = ppiVar11 + 1;
                ppiVar13 = ppiVar13 + 1;
                psVar23 = psVar23 + 1;
              } while (sVar26 < iVar24);
              uStack_60 = (uint)(short)uVar8;
            }
            uStack_54 = uStack_54 - 1 & 0xffff;
          } while (uStack_54 != 0);
        }
        iVar24 = _DAT_fffd6928;
        iVar19 = (int)(short)ppiStack_6c;
        if (iVar12 <= iVar19) {
          do {
            iVar28 = iVar12 + iVar24;
            (*jd->color_trans_scale)
                      (jd->y_data[iVar28],jd->u_data[iVar28],jd->v_data[iVar28],jd->w_h,puStack_44);
            iVar12 = (int)(short)((short)iVar12 + 8);
            puStack_44 = puStack_44 + jd->out_h;
          } while (iVar12 <= iVar19);
        }
        uVar17 = uVar17 - 1 & 0xffff;
      } while ((psVar20[iVar19] != -1) && (uVar17 != 0));
      goto LAB_000169a8;
    }
  }
  local_70 = 0;
LAB_000169a8:
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  return local_70;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_0_variety
 * Purpose: Decodes JPEG MCUs for YUV420 without rotation using the general path selecting scale, clip and output details; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000169d8
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_0_variety(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  byte bVar1;
  uint16_t uVar2;
  ushort uVar3;
  ushort uVar4;
  ushort uVar5;
  uint16_t *puVar6;
  jpeg_dec_io_t *pjVar7;
  jpeg_dec_io_t *pjVar8;
  jpeg_error_t jVar9;
  jpeg_error_t jVar10;
  jpeg_error_t jVar11;
  jpeg_error_t jVar12;
  uint uVar13;
  uint8_t *puVar14;
  uint16_t *puVar15;
  int iVar16;
  uint8_t *puVar17;
  uint uVar18;
  int iVar19;
  uint8_t *puVar20;
  uint uVar21;
  int iVar22;
  undefined4 local_50;
  undefined4 uStack_4c;
  jpeg_dec_io_t *pjStack_40;
  uint16_t *puStack_3c;
  uint8_t *puStack_38;
  uint16_t *puStack_34;
  jpeg_error_t jStack_30;
  int iStack_2c;
  jpeg_dec_io_t *pjStack_28;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: uint8_t * tmp_buf@[???]
                       Unresolved local var: uint8_t * tmp_out@[???]
                       Unresolved local var: int16_t[4] tmp_w_h@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int width_left@[???]
                       Unresolved local var: int left_y@[???] */
  puVar15 = jd->huffdata[1][1] + 0xfa;
  local_50 = *_DAT_fffd69dc;
  uVar21 = (uint)(jd->resize).width;
  iVar19 = (int)jd->w_h[2];
  uVar13 = (uint)jd->height;
  iVar22 = (int)jd->w_h[3];
  uStack_4c = _DAT_fffd69dc[1];
  iStack_2c = (int)uVar21 / iVar19;
  pjStack_40 = (jpeg_dec_io_t *)((int)uVar13 / iVar22);
  puStack_3c = (uint16_t *)((int)uVar21 % iVar19);
  pjVar8 = (jpeg_dec_io_t *)0x0;
  puVar17 = io->outbuf;
                    /* Unresolved local var: int h@[???] */
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
  jVar12 = JPEG_ERR_OK;
  pjVar7 = io;
  if (0 < (int)pjStack_40) {
    while( true ) {
      pjStack_28 = pjVar7;
      iVar22 = 0;
      if (iVar19 <= (int)uVar21) {
        do {
          if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
            uVar3 = jd->rsc;
            jd->rsc = uVar3 + 1;
            jd->bits_left = jVar12;
            jd->get_buffer = jVar12;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar4 = *(ushort *)jd->dptr;
            uVar5 = uVar4 >> 8;
            jd->dctr = jd->dctr - 2;
            uVar13 = _DAT_fffd6a68;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            if (((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar13) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar3 ^ uVar5) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            pjVar8 = (jpeg_dec_io_t *)jVar12;
          }
          jVar9 = (*_DAT_fffd6a98)(jd,4,jVar12,jd->workbuf_y);
          jVar10 = (*_DAT_fffd6aac)(jd,1,1,jd->workbuf_u);
          jVar11 = (*_DAT_fffd6ac0)(jd,1,2,jd->workbuf_v);
          pjVar8 = (jpeg_dec_io_t *)((uint)pjVar8 | jVar9 | jVar10 | jVar11);
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar17 + (uint)jd->pixel * iVar22);
          iVar22 = iVar22 + jd->w_h[2];
        } while (iVar22 <= (int)((uint)puVar15[6] - (int)jd->w_h[2]));
      }
      if (puStack_3c != (uint16_t *)0x0) {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar3 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar4 = *(ushort *)jd->dptr;
          uVar18 = (uint)uVar4 << 8 | (uint)(uVar4 >> 8);
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          uVar21 = _DAT_fffd6bec;
          uVar13 = _DAT_fffd6be8;
          jd->dctr = jd->dctr - 2;
          if ((uVar18 & uVar13) != uVar21) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar18 ^ uVar3) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          pjVar8 = (jpeg_dec_io_t *)0x0;
        }
        jVar9 = (*_DAT_fffd6c18)(jd,4,jVar12,jd->workbuf_y);
        jVar10 = (*_DAT_fffd6c28)(jd,1,1,jd->workbuf_u);
        jVar11 = (*_DAT_fffd6c40)(jd,1,2,jd->workbuf_v);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
        puVar6 = puStack_3c;
                    /* Unresolved local var: int i@[???] */
        pjVar8 = (jpeg_dec_io_t *)((uint)pjVar8 | jVar9 | jVar10 | jVar11);
        if (0 < jd->w_h[3]) {
          uVar13 = (uint)jd->pixel;
          puVar20 = jd->unalign_output;
          puVar14 = puVar17 + uVar13 * iStack_2c * 0x10;
          iVar22 = 0;
          puStack_38 = puVar17;
          puStack_34 = puVar15;
          jStack_30 = jVar12;
          do {
            (*_DAT_fffd6d3c)(puVar14,puVar20,(int)(short)uVar13 * (int)(short)puVar6);
            bVar1 = jd->pixel;
            uVar13 = (uint)bVar1;
            iVar22 = iVar22 + 1;
            puVar20 = puVar20 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
            puVar14 = puVar14 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
            jVar12 = jStack_30;
            puVar15 = puStack_34;
            puVar17 = puStack_38;
          } while (iVar22 < jd->w_h[3]);
        }
      }
      pjStack_40 = (jpeg_dec_io_t *)((int)pjStack_40 + -1);
                    /* Unresolved local var: int w@[???] */
      uVar21 = (uint)puVar15[6];
      puVar17 = puVar17 + jd->out_h;
      if (pjStack_40 == (jpeg_dec_io_t *)0x0) break;
      iVar19 = (int)jd->w_h[2];
      pjVar7 = pjStack_28;
    }
    uVar13 = (uint)jd->height;
    iVar22 = (int)jd->w_h[3];
    io = pjStack_28;
  }
  iVar22 = (int)uVar13 % iVar22;
  if (iVar22 == 0) {
    uVar13 = (uint)jd->pixel;
  }
  else {
    jStack_30 = iVar22;
    if ((int)jd->w_h[2] <= (int)uVar21) {
      iVar19 = 0;
      pjStack_40 = pjVar8;
      puStack_38 = puVar17;
      puStack_34 = puVar15;
      pjStack_28 = io;
      do {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar3 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          uVar13 = _DAT_fffd6e00;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar4 = *(ushort *)jd->dptr;
          uVar5 = uVar4 >> 8;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          jd->dctr = jd->dctr - 2;
          if (((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar13) {
            return JPEG_ERR_BAD_DATA;
          }
          uVar3 = uVar3 ^ uVar5;
          pjStack_40 = (jpeg_dec_io_t *)((uint)uVar3 & 7);
          if ((uVar3 & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
        }
        jVar12 = (*_DAT_fffd6e30)(jd,4,0,jd->workbuf_y);
        jVar12 = (uint)pjStack_40 | jVar12;
        jVar9 = (*_DAT_fffd6e48)(jd,1,1,jd->workbuf_u);
        jVar10 = (*_DAT_fffd6e5c)(jd,1,2,jd->workbuf_v);
        pjStack_40 = (jpeg_dec_io_t *)(jVar12 | jVar9 | jVar10);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
        uVar13 = (uint)jd->pixel;
        puVar17 = jd->unalign_output;
                    /* Unresolved local var: int i@[???] */
        iVar16 = 0;
        puVar14 = puStack_38 + uVar13 * iVar19;
        do {
          (*_DAT_fffd6f44)(puVar14,puVar17,(int)jd->w_h[2] * uVar13);
          bVar1 = jd->pixel;
          uVar13 = (uint)bVar1;
          iVar16 = iVar16 + 1;
          puVar17 = puVar17 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
          puVar14 = puVar14 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
        } while (iVar22 != iVar16);
        iVar19 = iVar19 + jd->w_h[2];
        pjVar8 = pjStack_40;
        puVar15 = puStack_34;
        puVar17 = puStack_38;
        io = pjStack_28;
      } while (iVar19 <= (int)((uint)puStack_34[6] - (int)jd->w_h[2]));
    }
    if (puStack_3c == (uint16_t *)0x0) {
      uVar21 = (uint)puVar15[6];
      uVar13 = (uint)jd->pixel;
    }
    else {
      if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
        uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar3 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar4 = *(ushort *)jd->dptr;
        uVar18 = (uint)uVar4 << 8 | (uint)(uVar4 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar21 = _DAT_fffd6fd4;
        uVar13 = _DAT_fffd6fcc;
        jd->dctr = jd->dctr - 2;
        if (((uVar18 & uVar13) != uVar21) || (((uVar3 ^ uVar18) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        pjVar8 = (jpeg_dec_io_t *)0x0;
      }
      pjStack_40 = io;
      jVar12 = (*_DAT_fffd7000)(jd,4,0,jd->workbuf_y);
      jVar9 = (*_DAT_fffd7014)(jd,1,1,jd->workbuf_u);
      jVar10 = (*_DAT_fffd702c)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
      pjVar8 = (jpeg_dec_io_t *)((uint)pjVar8 | jVar12 | jVar9 | jVar10);
      (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
      (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
      (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
      puVar6 = puStack_3c;
      uVar13 = (uint)jd->pixel;
      puVar14 = jd->unalign_output;
                    /* Unresolved local var: int i@[???] */
      puVar17 = puVar17 + uVar13 * iStack_2c * 0x10;
      iVar22 = 0;
      puStack_3c = puVar15;
      do {
        (*_DAT_fffd711c)(puVar17,puVar14,(int)(short)uVar13 * (int)(short)puVar6);
        bVar1 = jd->pixel;
        uVar13 = (uint)bVar1;
        iVar22 = iVar22 + 1;
        puVar14 = puVar14 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
        puVar17 = puVar17 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
      } while (jStack_30 != iVar22);
      uVar21 = (uint)puStack_3c[6];
      puVar15 = puStack_3c;
      io = pjStack_40;
    }
  }
  io->out_size = puVar15[7] * uVar21 * uVar13;
  *(undefined1 *)(puVar15 + 0x79) = 1;
  return (jpeg_error_t)pjVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_0_block
 * Purpose: Decodes JPEG MCUs for YUV422 without rotation using the block-output path used by the streaming API; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00017160
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_0_block(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  uint uVar4;
  uint uVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  int iVar10;
  uint8_t *puVar11;
  int iVar12;
  short sVar13;
  ushort uVar14;
  uint uVar15;
  int16_t *piVar16;
  uint uVar17;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int width_left@[???] */
  uVar17 = (uint)jd->width;
  iVar10 = (int)jd->w_h[2];
  puVar11 = io->outbuf;
                    /* Unresolved local var: int w@[???] */
  if ((int)uVar17 < iVar10) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    iVar12 = 0;
    jVar9 = JPEG_ERR_OK;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    do {
      if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
        uVar2 = jd->rsc;
        jd->rsc = uVar2 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        uVar4 = _DAT_fffd71d0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar3 = *(ushort *)jd->dptr;
        uVar14 = uVar3 >> 8;
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        jd->dctr = jd->dctr - 2;
        if (((uVar3 & 0xff) << 8 | uVar14 & 0xd8) != uVar4) {
          return JPEG_ERR_BAD_DATA;
        }
        if (((uVar2 ^ uVar14) & 7) != 0) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar9 = JPEG_ERR_OK;
      }
      jVar6 = (*_DAT_fffd71f8)(jd,2,0,jd->workbuf_y);
      jVar7 = (*_DAT_fffd720c)(jd,1,1,jd->workbuf_u);
      jVar8 = (*_DAT_fffd7220)(jd,1,2,jd->workbuf_v);
      jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
      (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar11 + (uint)jd->pixel * iVar12);
      iVar12 = iVar12 + jd->w_h[2];
    } while (iVar12 <= (int)((uint)jd->width - (int)jd->w_h[2]));
  }
  if ((int)uVar17 % iVar10 == 0) {
    sVar13 = jd->w_h[3];
    iVar10 = (int)sVar13;
  }
  else {
    if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
      uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
      jd->rsc = uVar2 + 1;
      jd->bits_left = 0;
      jd->get_buffer = 0;
      if ((uint)jd->dctr < 2) {
        return JPEG_ERR_NO_MORE_DATA;
      }
      uVar3 = *(ushort *)jd->dptr;
      uVar15 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
      jd->dptr = (uint8_t *)((int)jd->dptr + 2);
      uVar5 = _DAT_fffd7324;
      uVar4 = _DAT_fffd731c;
      jd->dctr = jd->dctr - 2;
      if (((uVar15 & uVar4) != uVar5) || (((uVar15 ^ uVar2) & 7) != 0)) {
        return JPEG_ERR_BAD_DATA;
      }
      jd->dcv[0] = 0;
      jd->dcv[1] = 0;
      jd->dcv[2] = 0;
      jd->rst = 1;
      jVar9 = JPEG_ERR_OK;
    }
    piVar16 = jd->workbuf_y;
    jd->w_h[2] = 8;
    jd->w_h[3] = 8;
    jVar6 = (*_DAT_fffd7358)(jd,2,0,piVar16);
    jVar7 = (*_DAT_fffd736c)(jd,1,1,jd->workbuf_u);
    jVar8 = (*_DAT_fffd7384)(jd,1,2,jd->workbuf_v);
    (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
    (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
    (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
    (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                       puVar11 + (uint)jd->pixel * ((int)uVar17 / iVar10) * 0x10);
    iVar10 = 8;
    jd->w_h[2] = 0x10;
    jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
    jd->w_h[3] = 8;
    sVar13 = 8;
  }
  uVar2 = (jd->resize).width;
  uVar14 = sVar13 + jd->block_output_line;
  jd->block_output_line = uVar14;
  uVar3 = (jd->resize).height;
  io->out_size = (uint)uVar2 * iVar10 * (uint)jd->pixel;
  if (uVar3 <= uVar14) {
    jd->start_sos = true;
    jd->block_output_line = 0;
  }
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_0_block
 * Purpose: Hot Player path: decodes six blocks per 16x16 YUV420 MCU, runs integer IDCT, converts to RGB565LE and submits block rows through the streaming output callback.
 * Entry: 00017434
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_0_block(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  uint8_t *puVar9;
  int iVar10;
  uint uVar11;
  uint uVar12;
  uint uVar13;
  int iVar14;
  int16_t *piVar15;
  uint uVar16;
  int iVar17;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int width_left@[???] */
  uVar16 = (uint)jd->width;
  iVar14 = (int)jd->w_h[2];
  uVar11 = (uint)jd->block_output_line;
  uVar13 = (uint)(jd->resize).height;
  puVar9 = io->outbuf;
  if ((int)uVar13 < (int)(uVar11 + (int)jd->w_h[3])) {
                    /* Unresolved local var: int left_y@[???] */
    iVar17 = (int)(uint)jd->height % (int)jd->w_h[3];
    if (iVar17 == 0) {
      jVar8 = JPEG_ERR_OK;
    }
    else {
      jd->w_h[2] = 0x10;
      jd->w_h[3] = 8;
                    /* Unresolved local var: int w@[???] */
      if (uVar16 < 0x10) {
        jVar8 = JPEG_ERR_OK;
      }
      else {
        iVar10 = 0;
        jVar8 = JPEG_ERR_OK;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            uVar11 = _DAT_fffd77f8;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            jd->dctr = jd->dctr - 2;
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar11) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd7820)(jd,4,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd7834)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd7848)(jd,1,2,jd->workbuf_v);
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar9 + (uint)jd->pixel * iVar10);
          iVar10 = iVar10 + jd->w_h[2];
        } while (iVar10 <= (int)((uint)jd->width - (int)jd->w_h[2]));
      }
      if ((int)uVar16 % iVar14 != 0) {
        if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
          uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar2 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar12 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          uVar13 = _DAT_fffd7940;
          uVar11 = _DAT_fffd7938;
          jd->dctr = jd->dctr - 2;
          if ((uVar12 & uVar11) != uVar13) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar12 ^ uVar2) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar8 = JPEG_ERR_OK;
        }
        piVar15 = jd->workbuf_y;
        jd->w_h[2] = 8;
        jd->w_h[3] = 8;
        jVar5 = (*_DAT_fffd7974)(jd,4,0,piVar15);
        jVar6 = (*_DAT_fffd7988)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd79a0)(jd,1,2,jd->workbuf_v);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                           puVar9 + (uint)jd->pixel * ((int)uVar16 / iVar14) * 0x10);
      }
      uVar11 = (uint)jd->block_output_line;
      uVar13 = (uint)(jd->resize).height;
      jd->w_h[2] = 0x10;
      jd->w_h[3] = 0x10;
    }
    uVar11 = uVar11 + iVar17;
    uVar2 = (jd->resize).width;
    jd->block_output_line = (uint16_t)uVar11;
    io->out_size = (uint)uVar2 * iVar17 * (uint)jd->pixel;
  }
  else {
                    /* Unresolved local var: int w@[???] */
    if ((int)uVar16 < iVar14) {
      jVar8 = JPEG_ERR_OK;
    }
    else {
      jVar8 = JPEG_ERR_OK;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
      iVar17 = 0;
      do {
        if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
          uVar2 = jd->rsc;
          jd->rsc = uVar2 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          uVar11 = _DAT_fffd74c0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar4 = uVar3 >> 8;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          jd->dctr = jd->dctr - 2;
          if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar11) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar2 ^ uVar4) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar8 = JPEG_ERR_OK;
        }
        jVar5 = (*_DAT_fffd74f4)(jd,4,0,jd->workbuf_y);
        jVar6 = (*_DAT_fffd7508)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd751c)(jd,1,2,jd->workbuf_v);
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar9 + (uint)jd->pixel * iVar17);
        iVar17 = iVar17 + jd->w_h[2];
      } while (iVar17 <= (int)((uint)jd->width - (int)jd->w_h[2]));
    }
    if ((int)uVar16 % iVar14 == 0) {
      uVar11 = (uint)(ushort)jd->w_h[3];
      iVar14 = (int)jd->w_h[3];
    }
    else {
      if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
        uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar2 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar3 = *(ushort *)jd->dptr;
        uVar12 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar13 = _DAT_fffd7660;
        uVar11 = _DAT_fffd7658;
        jd->dctr = jd->dctr - 2;
        if (((uVar12 & uVar11) != uVar13) || (((uVar2 ^ uVar12) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar8 = JPEG_ERR_OK;
      }
      piVar15 = jd->workbuf_y;
      jd->w_h[2] = 8;
      jd->w_h[3] = 0x10;
      jVar5 = (*_DAT_fffd7694)(jd,4,0,piVar15);
      jVar6 = (*_DAT_fffd76a8)(jd,1,1,jd->workbuf_u);
      jVar7 = (*_DAT_fffd76c0)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                         puVar9 + (uint)jd->pixel * ((int)uVar16 / iVar14) * 0x10);
      iVar14 = 0x10;
      jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
      jd->w_h[2] = 0x10;
      jd->w_h[3] = 0x10;
      uVar11 = 0x10;
    }
    uVar2 = (jd->resize).width;
    uVar11 = uVar11 + jd->block_output_line;
    jd->block_output_line = (uint16_t)uVar11;
    uVar13 = (uint)(jd->resize).height;
    io->out_size = (uint)uVar2 * iVar14 * (uint)jd->pixel;
  }
  if (uVar13 <= (uVar11 & 0xffff)) {
    jd->start_sos = true;
    jd->block_output_line = 0;
  }
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_proc_gray_0_variety
 * Purpose: Decodes JPEG MCUs for grayscale Y without rotation using the general path selecting scale, clip and output details; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00017a54
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_gray_0_variety(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  byte bVar1;
  uint16_t uVar2;
  ushort uVar3;
  ushort uVar4;
  ushort uVar5;
  jpeg_dec_io_t *pjVar6;
  jpeg_error_t jVar7;
  jpeg_dec_io_t *pjVar8;
  uint8_t *puVar9;
  uint16_t *puVar10;
  int iVar11;
  uint8_t *puVar12;
  int32_t iVar13;
  uint uVar14;
  int iVar15;
  uint uVar16;
  uint8_t *puVar17;
  int iVar18;
  uint uVar19;
  undefined4 local_50;
  undefined4 uStack_4c;
  jpeg_dec_io_t *pjStack_40;
  int iStack_3c;
  uint8_t *puStack_38;
  uint16_t *puStack_34;
  int32_t iStack_30;
  int iStack_2c;
  jpeg_dec_io_t *pjStack_28;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: uint8_t * tmp_buf@[???]
                       Unresolved local var: uint8_t * tmp_out@[???]
                       Unresolved local var: int16_t[4] tmp_w_h@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int width_left@[???]
                       Unresolved local var: int left_y@[???] */
  puVar10 = jd->huffdata[1][1] + 0xfa;
  uVar16 = (uint)(jd->resize).width;
  iVar15 = (int)jd->w_h[2];
  local_50 = *_DAT_fffd7a58;
  uVar19 = (uint)jd->height;
  iVar18 = (int)jd->w_h[3];
  uStack_4c = _DAT_fffd7a58[1];
  iStack_2c = (int)uVar16 / iVar15;
  pjStack_40 = (jpeg_dec_io_t *)((int)uVar19 / iVar18);
  iStack_3c = (int)uVar16 % iVar15;
  pjVar8 = (jpeg_dec_io_t *)0x0;
  puVar12 = io->outbuf;
                    /* Unresolved local var: int h@[???] */
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
  iVar13 = 0;
  pjVar6 = io;
  if (0 < (int)pjStack_40) {
    while( true ) {
      pjStack_28 = pjVar6;
      iVar18 = 0;
      if (iVar15 <= (int)uVar16) {
        do {
          if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
            uVar3 = jd->rsc;
            jd->rsc = uVar3 + 1;
            jd->bits_left = iVar13;
            jd->get_buffer = iVar13;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar4 = *(ushort *)jd->dptr;
            uVar5 = uVar4 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            uVar16 = _DAT_fffd7ae8;
            uVar3 = uVar3 ^ uVar5;
            jd->dctr = jd->dctr - 2;
            pjVar8 = (jpeg_dec_io_t *)((uint)uVar3 & 7);
            if (((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar16) {
              return JPEG_ERR_BAD_DATA;
            }
            if ((uVar3 & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
          }
          jVar7 = (*_DAT_fffd7b10)(jd,1,iVar13,jd->workbuf_y);
          pjVar8 = (jpeg_dec_io_t *)((uint)pjVar8 | jVar7);
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar12 + (uint)jd->pixel * iVar18);
          iVar18 = iVar18 + jd->w_h[2];
        } while (iVar18 <= (int)((uint)puVar10[6] - (int)jd->w_h[2]));
      }
      if (iStack_3c != 0) {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar3 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar4 = *(ushort *)jd->dptr;
          uVar14 = (uint)uVar4 << 8 | (uint)(uVar4 >> 8);
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          uVar19 = _DAT_fffd7bac;
          uVar16 = _DAT_fffd7ba8;
          jd->dctr = jd->dctr - 2;
          if ((uVar14 & uVar16) != uVar19) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar14 ^ uVar3) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          pjVar8 = (jpeg_dec_io_t *)0x0;
        }
        jVar7 = (*_DAT_fffd7bd8)(jd,1,iVar13,jd->workbuf_y);
        pjVar8 = (jpeg_dec_io_t *)((uint)pjVar8 | jVar7);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
        iVar18 = iStack_3c;
                    /* Unresolved local var: int i@[???] */
        if (0 < jd->w_h[3]) {
          uVar16 = (uint)jd->pixel;
          puVar17 = jd->unalign_output;
          puVar9 = puVar12 + uVar16 * iStack_2c * 8;
          iVar15 = 0;
          puStack_38 = puVar12;
          puStack_34 = puVar10;
          iStack_30 = iVar13;
          do {
            (*_DAT_fffd7c3c)(puVar9,puVar17,(int)(short)uVar16 * (int)(short)iVar18);
            bVar1 = jd->pixel;
            uVar16 = (uint)bVar1;
            iVar15 = iVar15 + 1;
            puVar17 = puVar17 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
            puVar9 = puVar9 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
            puVar10 = puStack_34;
            puVar12 = puStack_38;
            iVar13 = iStack_30;
          } while (iVar15 < jd->w_h[3]);
        }
      }
      pjStack_40 = (jpeg_dec_io_t *)((int)pjStack_40 + -1);
                    /* Unresolved local var: int w@[???] */
      uVar16 = (uint)puVar10[6];
      puVar12 = puVar12 + jd->out_h;
      if (pjStack_40 == (jpeg_dec_io_t *)0x0) break;
      iVar15 = (int)jd->w_h[2];
      pjVar6 = pjStack_28;
    }
    uVar19 = (uint)jd->height;
    iVar18 = (int)jd->w_h[3];
    io = pjStack_28;
  }
  iVar18 = (int)uVar19 % iVar18;
  if (iVar18 == 0) {
    uVar19 = (uint)jd->pixel;
  }
  else {
    iStack_30 = iVar18;
    if ((int)jd->w_h[2] <= (int)uVar16) {
      iVar15 = 0;
      pjStack_40 = pjVar8;
      puStack_38 = puVar12;
      puStack_34 = puVar10;
      pjStack_28 = io;
      do {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar3 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar4 = *(ushort *)jd->dptr;
          uVar5 = uVar4 >> 8;
          jd->dctr = jd->dctr - 2;
          uVar16 = _DAT_fffd7cfc;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          if (((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar16) {
            return JPEG_ERR_BAD_DATA;
          }
          uVar3 = uVar3 ^ uVar5;
          pjStack_40 = (jpeg_dec_io_t *)((uint)uVar3 & 7);
          if ((uVar3 & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
        }
        jVar7 = (*_DAT_fffd7d2c)(jd,1,0,jd->workbuf_y);
        pjStack_40 = (jpeg_dec_io_t *)((uint)pjStack_40 | jVar7);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
        uVar16 = (uint)jd->pixel;
        puVar12 = jd->unalign_output;
                    /* Unresolved local var: int i@[???] */
        iVar11 = 0;
        puVar9 = puStack_38 + uVar16 * iVar15;
        do {
          (*_DAT_fffd7d80)(puVar9,puVar12,(int)jd->w_h[2] * uVar16);
          bVar1 = jd->pixel;
          uVar16 = (uint)bVar1;
          iVar11 = iVar11 + 1;
          puVar12 = puVar12 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
          puVar9 = puVar9 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
        } while (iVar18 != iVar11);
        iVar15 = iVar15 + jd->w_h[2];
        pjVar8 = pjStack_40;
        puVar10 = puStack_34;
        puVar12 = puStack_38;
        io = pjStack_28;
      } while (iVar15 <= (int)((uint)puStack_34[6] - (int)jd->w_h[2]));
    }
    if (iStack_3c == 0) {
      uVar16 = (uint)puVar10[6];
      uVar19 = (uint)jd->pixel;
    }
    else {
      if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
        uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar3 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar4 = *(ushort *)jd->dptr;
        uVar14 = (uint)uVar4 << 8 | (uint)(uVar4 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar19 = _DAT_fffd7e10;
        uVar16 = _DAT_fffd7e08;
        jd->dctr = jd->dctr - 2;
        if (((uVar14 & uVar16) != uVar19) || (((uVar3 ^ uVar14) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        pjVar8 = (jpeg_dec_io_t *)0x0;
      }
      pjStack_40 = io;
      jVar7 = (*_DAT_fffd7e3c)(jd,1,0,jd->workbuf_y);
      pjVar8 = (jpeg_dec_io_t *)((uint)pjVar8 | jVar7);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
      uVar19 = (uint)jd->pixel;
      puVar9 = jd->unalign_output;
                    /* Unresolved local var: int i@[???] */
      puVar12 = puVar12 + uVar19 * iStack_2c * 8;
      iVar18 = 0;
      do {
        (*_DAT_fffd7e9c)(puVar12,puVar9,(int)(short)uVar19 * (int)(short)iStack_3c);
        bVar1 = jd->pixel;
        uVar19 = (uint)bVar1;
        iVar18 = iVar18 + 1;
        puVar9 = puVar9 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
        puVar12 = puVar12 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
      } while (iStack_30 != iVar18);
      uVar16 = (uint)puVar10[6];
      io = pjStack_40;
    }
  }
  io->out_size = puVar10[7] * uVar16 * uVar19;
  *(undefined1 *)(puVar10 + 0x79) = 1;
  return (jpeg_error_t)pjVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv444_0_variety
 * Purpose: Decodes JPEG MCUs for YUV444 without rotation using the general path selecting scale, clip and output details; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00017ee0
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv444_0_variety(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  byte bVar1;
  uint16_t uVar2;
  ushort uVar3;
  ushort uVar4;
  ushort uVar5;
  jpeg_dec_io_t *pjVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  jpeg_dec_io_t *pjVar10;
  uint8_t *puVar11;
  uint16_t *puVar12;
  int iVar13;
  uint8_t *puVar14;
  jpeg_error_t jVar15;
  uint uVar16;
  int iVar17;
  uint uVar18;
  uint8_t *puVar19;
  int iVar20;
  uint uVar21;
  undefined4 local_50;
  undefined4 uStack_4c;
  jpeg_dec_io_t *pjStack_40;
  int iStack_3c;
  uint8_t *puStack_38;
  uint16_t *puStack_34;
  jpeg_error_t jStack_30;
  int iStack_2c;
  jpeg_dec_io_t *pjStack_28;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: uint8_t * tmp_buf@[???]
                       Unresolved local var: uint8_t * tmp_out@[???]
                       Unresolved local var: int16_t[4] tmp_w_h@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int width_left@[???]
                       Unresolved local var: int left_y@[???] */
  puVar12 = jd->huffdata[1][1] + 0xfa;
  uVar18 = (uint)(jd->resize).width;
  iVar17 = (int)jd->w_h[2];
  local_50 = *_DAT_fffd7ee4;
  uVar21 = (uint)jd->height;
  iVar20 = (int)jd->w_h[3];
  uStack_4c = _DAT_fffd7ee4[1];
  iStack_2c = (int)uVar18 / iVar17;
  pjStack_40 = (jpeg_dec_io_t *)((int)uVar21 / iVar20);
  iStack_3c = (int)uVar18 % iVar17;
  pjVar10 = (jpeg_dec_io_t *)0x0;
  puVar14 = io->outbuf;
                    /* Unresolved local var: int h@[???] */
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
  jVar15 = JPEG_ERR_OK;
  pjVar6 = io;
  if (0 < (int)pjStack_40) {
    while( true ) {
      pjStack_28 = pjVar6;
      iVar20 = 0;
      if (iVar17 <= (int)uVar18) {
        do {
          if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
            uVar3 = jd->rsc;
            jd->rsc = uVar3 + 1;
            jd->bits_left = jVar15;
            jd->get_buffer = jVar15;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar4 = *(ushort *)jd->dptr;
            uVar5 = uVar4 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            uVar18 = _DAT_fffd7f74;
            jd->dctr = jd->dctr - 2;
            if (((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar18) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar3 ^ uVar5) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            pjVar10 = (jpeg_dec_io_t *)jVar15;
          }
          jVar7 = (*_DAT_fffd7f9c)(jd,1,jVar15,jd->workbuf_y);
          jVar8 = (*_DAT_fffd7fb0)(jd,1,1,jd->workbuf_u);
          jVar9 = (*_DAT_fffd7fc4)(jd,1,2,jd->workbuf_v);
          pjVar10 = (jpeg_dec_io_t *)((uint)pjVar10 | jVar7 | jVar8 | jVar9);
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar14 + (uint)jd->pixel * iVar20);
          iVar20 = iVar20 + jd->w_h[2];
        } while (iVar20 <= (int)((uint)puVar12[6] - (int)jd->w_h[2]));
      }
      if (iStack_3c != 0) {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar3 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar4 = *(ushort *)jd->dptr;
          uVar16 = (uint)uVar4 << 8 | (uint)(uVar4 >> 8);
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          uVar21 = _DAT_fffd8090;
          uVar18 = _DAT_fffd808c;
          jd->dctr = jd->dctr - 2;
          if ((uVar16 & uVar18) != uVar21) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar16 ^ uVar3) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          pjVar10 = (jpeg_dec_io_t *)0x0;
        }
        jVar7 = (*_DAT_fffd80bc)(jd,1,jVar15,jd->workbuf_y);
        jVar8 = (*_DAT_fffd80d0)(jd,1,1,jd->workbuf_u);
        jVar9 = (*_DAT_fffd80e4)(jd,1,2,jd->workbuf_v);
        pjVar10 = (jpeg_dec_io_t *)((uint)pjVar10 | jVar7 | jVar8 | jVar9);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
        iVar20 = iStack_3c;
                    /* Unresolved local var: int i@[???] */
        if (0 < jd->w_h[3]) {
          uVar18 = (uint)jd->pixel;
          puVar19 = jd->unalign_output;
          puVar11 = puVar14 + uVar18 * iStack_2c * 8;
          iVar17 = 0;
          puStack_38 = puVar14;
          puStack_34 = puVar12;
          jStack_30 = jVar15;
          do {
            (*_DAT_fffd8178)(puVar11,puVar19,(int)(short)uVar18 * (int)(short)iVar20);
            bVar1 = jd->pixel;
            uVar18 = (uint)bVar1;
            iVar17 = iVar17 + 1;
            puVar19 = puVar19 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
            puVar11 = puVar11 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
            puVar12 = puStack_34;
            puVar14 = puStack_38;
            jVar15 = jStack_30;
          } while (iVar17 < jd->w_h[3]);
        }
      }
      pjStack_40 = (jpeg_dec_io_t *)((int)pjStack_40 + -1);
                    /* Unresolved local var: int w@[???] */
      uVar18 = (uint)puVar12[6];
      puVar14 = puVar14 + jd->out_h;
      if (pjStack_40 == (jpeg_dec_io_t *)0x0) break;
      iVar17 = (int)jd->w_h[2];
      pjVar6 = pjStack_28;
    }
    uVar21 = (uint)jd->height;
    iVar20 = (int)jd->w_h[3];
    io = pjStack_28;
  }
  iVar20 = (int)uVar21 % iVar20;
  if (iVar20 == 0) {
    uVar21 = (uint)jd->pixel;
  }
  else {
    jStack_30 = iVar20;
    if ((int)jd->w_h[2] <= (int)uVar18) {
      iVar17 = 0;
      pjStack_40 = pjVar10;
      puStack_38 = puVar14;
      puStack_34 = puVar12;
      pjStack_28 = io;
      do {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar3 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar4 = *(ushort *)jd->dptr;
          uVar5 = uVar4 >> 8;
          jd->dctr = jd->dctr - 2;
          uVar18 = _DAT_fffd8238;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          if (((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar18) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar3 ^ uVar5) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          pjStack_40 = (jpeg_dec_io_t *)0x0;
        }
        jVar15 = (*_DAT_fffd8268)(jd,1,0,jd->workbuf_y);
        jVar15 = (uint)pjStack_40 | jVar15;
        jVar7 = (*_DAT_fffd8280)(jd,1,1,jd->workbuf_u);
        jVar8 = (*_DAT_fffd8294)(jd,1,2,jd->workbuf_v);
        pjStack_40 = (jpeg_dec_io_t *)(jVar15 | jVar7 | jVar8);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
        uVar18 = (uint)jd->pixel;
        puVar14 = jd->unalign_output;
                    /* Unresolved local var: int i@[???] */
        iVar13 = 0;
        puVar11 = puStack_38 + uVar18 * iVar17;
        do {
          (*_DAT_fffd8318)(puVar11,puVar14,(int)jd->w_h[2] * uVar18);
          bVar1 = jd->pixel;
          uVar18 = (uint)bVar1;
          iVar13 = iVar13 + 1;
          puVar14 = puVar14 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
          puVar11 = puVar11 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
        } while (iVar20 != iVar13);
        iVar17 = iVar17 + jd->w_h[2];
        pjVar10 = pjStack_40;
        puVar12 = puStack_34;
        puVar14 = puStack_38;
        io = pjStack_28;
      } while (iVar17 <= (int)((uint)puStack_34[6] - (int)jd->w_h[2]));
    }
    if (iStack_3c == 0) {
      uVar18 = (uint)puVar12[6];
      uVar21 = (uint)jd->pixel;
    }
    else {
      if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
        uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar3 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar4 = *(ushort *)jd->dptr;
        uVar16 = (uint)uVar4 << 8 | (uint)(uVar4 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar21 = _DAT_fffd83a8;
        uVar18 = _DAT_fffd83a0;
        jd->dctr = jd->dctr - 2;
        if (((uVar16 & uVar18) != uVar21) || (((uVar3 ^ uVar16) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        pjVar10 = (jpeg_dec_io_t *)0x0;
      }
      pjStack_40 = io;
      jVar15 = (*_DAT_fffd83d4)(jd,1,0,jd->workbuf_y);
      jVar7 = (*_DAT_fffd83e8)(jd,1,1,jd->workbuf_u);
      jVar8 = (*_DAT_fffd8400)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
      uVar21 = (uint)jd->pixel;
      puVar11 = jd->unalign_output;
      pjVar10 = (jpeg_dec_io_t *)((uint)pjVar10 | jVar15 | jVar7 | jVar8);
      puVar14 = puVar14 + uVar21 * iStack_2c * 8;
                    /* Unresolved local var: int i@[???] */
      iVar20 = 0;
      do {
        (*_DAT_fffd8490)(puVar14,puVar11,(int)(short)uVar21 * (int)(short)iStack_3c);
        bVar1 = jd->pixel;
        uVar21 = (uint)bVar1;
        iVar20 = iVar20 + 1;
        puVar11 = puVar11 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
        puVar14 = puVar14 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
      } while (jStack_30 != iVar20);
      uVar18 = (uint)puVar12[6];
      io = pjStack_40;
    }
  }
  io->out_size = puVar12[7] * uVar18 * uVar21;
  *(undefined1 *)(puVar12 + 0x79) = 1;
  return (jpeg_error_t)pjVar10;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_0_unalign
 * Purpose: Decodes JPEG MCUs for YUV420 without rotation using the edge path for dimensions not aligned to an MCU; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 000184d4
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_0_unalign(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  int iVar9;
  uint8_t *puVar10;
  uint uVar11;
  int iVar12;
  int iVar13;
  int iVar14;
  uint uVar15;
  uint uVar16;
  int iVar17;
  int16_t *piVar18;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int width_left@[???]
                       Unresolved local var: int left_y@[???] */
  uVar16 = (uint)jd->width;
  iVar14 = (int)jd->w_h[2];
  uVar11 = (uint)jd->height;
  iVar12 = (int)uVar16 / iVar14;
  iVar17 = (int)jd->w_h[3];
  iVar13 = (int)uVar16 % iVar14;
  iVar9 = (int)uVar11 / iVar17;
  jVar8 = JPEG_ERR_OK;
  puVar10 = io->outbuf;
                    /* Unresolved local var: int h@[???]
                       Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
  if (0 < iVar9) {
    while( true ) {
      iVar17 = 0;
      if (iVar14 <= (int)uVar16) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dctr = jd->dctr - 2;
            uVar11 = _DAT_fffd8550;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar11) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd8580)(jd,4,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd8594)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd85a8)(jd,1,2,jd->workbuf_v);
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar10 + (uint)jd->pixel * iVar17);
          iVar17 = iVar17 + jd->w_h[2];
        } while (iVar17 <= (int)((uint)jd->width - (int)jd->w_h[2]));
      }
      if (iVar13 != 0) {
        if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
          uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar2 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar15 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          uVar16 = _DAT_fffd86d4;
          uVar11 = _DAT_fffd86d0;
          jd->dctr = jd->dctr - 2;
          if ((uVar15 & uVar11) != uVar16) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar2 ^ uVar15) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar8 = JPEG_ERR_OK;
        }
        piVar18 = jd->workbuf_y;
        jd->w_h[3] = 0x10;
        jd->w_h[2] = 8;
        jVar5 = (*_DAT_fffd870c)(jd,4,0,piVar18);
        jVar6 = (*_DAT_fffd871c)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd8734)(jd,1,2,jd->workbuf_v);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
        (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar10 + (uint)jd->pixel * iVar12 * 0x10);
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        jd->w_h[2] = 0x10;
        jd->w_h[3] = 0x10;
      }
      iVar9 = iVar9 + -1;
      puVar10 = puVar10 + jd->out_h;
      if (iVar9 == 0) break;
      uVar16 = (uint)jd->width;
      iVar14 = (int)jd->w_h[2];
    }
    uVar11 = (uint)jd->height;
    iVar17 = (int)jd->w_h[3];
  }
  if ((int)uVar11 % iVar17 != 0) {
    jd->w_h[2] = 0x10;
                    /* Unresolved local var: int w@[???] */
    uVar2 = jd->width;
    jd->w_h[3] = 8;
    if (0xf < uVar2) {
      iVar9 = 0;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
      do {
        if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
          uVar2 = jd->rsc;
          jd->rsc = uVar2 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar4 = uVar3 >> 8;
          jd->dctr = jd->dctr - 2;
          uVar11 = _DAT_fffd8868;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar11) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar4 ^ uVar2) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar8 = JPEG_ERR_OK;
        }
        jVar5 = (*_DAT_fffd8890)(jd,4,0,jd->workbuf_y);
        jVar6 = (*_DAT_fffd88a4)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd88b8)(jd,1,2,jd->workbuf_v);
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar10 + (uint)jd->pixel * iVar9);
        iVar9 = iVar9 + jd->w_h[2];
      } while (iVar9 <= (int)((uint)jd->width - (int)jd->w_h[2]));
    }
    if (iVar13 != 0) {
      if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
        uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar2 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar3 = *(ushort *)jd->dptr;
        uVar15 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar16 = _DAT_fffd89a8;
        uVar11 = _DAT_fffd89a4;
        jd->dctr = jd->dctr - 2;
        if (((uVar15 & uVar11) != uVar16) || (((uVar15 ^ uVar2) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar8 = JPEG_ERR_OK;
      }
      piVar18 = jd->workbuf_y;
      jd->w_h[2] = 8;
      jd->w_h[3] = 8;
      jVar5 = (*_DAT_fffd89dc)(jd,4,0,piVar18);
      jVar6 = (*_DAT_fffd89ec)(jd,1,1,jd->workbuf_u);
      jVar7 = (*_DAT_fffd8a04)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
      (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar10 + (uint)jd->pixel * iVar12 * 0x10);
    }
    jd->w_h[2] = 0x10;
    jd->w_h[3] = 0x10;
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_90_unalign
 * Purpose: Decodes JPEG MCUs for YUV420 with 90-degree rotation using the edge path for dimensions not aligned to an MCU; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00018a9c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_90_unalign(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  uint16_t uVar1;
  ushort uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  int iVar9;
  uint8_t *puVar10;
  uint uVar11;
  int iVar12;
  int iVar13;
  int iVar14;
  uint uVar15;
  uint uVar16;
  int iVar17;
  int16_t *piVar18;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int width_left@[???]
                       Unresolved local var: int left_y@[???] */
  uVar16 = (uint)jd->width;
  iVar14 = (int)jd->w_h[3];
  uVar11 = (uint)jd->height;
  iVar12 = (int)uVar16 / iVar14;
  iVar17 = (int)jd->w_h[2];
  iVar13 = (int)uVar16 % iVar14;
  iVar9 = (int)uVar11 / iVar17;
  jVar8 = JPEG_ERR_OK;
  puVar10 = io->outbuf + jd->out_start_pos;
                    /* Unresolved local var: int h@[???]
                       Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
  if (0 < iVar9) {
    while( true ) {
      iVar17 = 0;
      if (iVar14 <= (int)uVar16) {
        do {
          if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
            uVar2 = jd->rsc;
            jd->rsc = uVar2 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dctr = jd->dctr - 2;
            uVar11 = _DAT_fffd8b1c;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar11) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar2 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd8b4c)(jd,4,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd8b60)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd8b74)(jd,1,2,jd->workbuf_v);
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar10 + jd->w_h[0] * iVar17 * (uint)jd->pixel);
          iVar17 = iVar17 + jd->w_h[3];
        } while (iVar17 <= (int)((uint)jd->width - (int)jd->w_h[3]));
      }
      if (iVar13 != 0) {
        if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
          uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar2 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar15 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          uVar16 = _DAT_fffd8ca8;
          uVar11 = _DAT_fffd8ca4;
          jd->dctr = jd->dctr - 2;
          if ((uVar15 & uVar11) != uVar16) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar2 ^ uVar15) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar8 = JPEG_ERR_OK;
        }
        piVar18 = jd->workbuf_y;
        jd->w_h[2] = 0x10;
        jd->w_h[3] = 8;
        jVar5 = (*_DAT_fffd8ce0)(jd,4,0,piVar18);
        jVar6 = (*_DAT_fffd8cf4)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd8d0c)(jd,1,2,jd->workbuf_v);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                           puVar10 + jd->w_h[0] * iVar12 * (uint)jd->pixel * 0x10);
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        jd->w_h[2] = 0x10;
        jd->w_h[3] = 0x10;
      }
      iVar9 = iVar9 + -1;
      puVar10 = puVar10 + jd->out_h;
      if (iVar9 == 0) break;
      uVar16 = (uint)jd->width;
      iVar14 = (int)jd->w_h[3];
    }
    uVar11 = (uint)jd->height;
    iVar17 = (int)jd->w_h[2];
  }
  if ((int)uVar11 % iVar17 != 0) {
    jd->w_h[2] = 8;
                    /* Unresolved local var: int w@[???] */
    uVar2 = jd->width;
    jd->w_h[3] = 0x10;
    if (0xf < uVar2) {
      iVar9 = 0;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
      do {
        if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
          uVar2 = jd->rsc;
          jd->rsc = uVar2 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar4 = uVar3 >> 8;
          jd->dctr = jd->dctr - 2;
          uVar11 = _DAT_fffd8e3c;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar11) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar4 ^ uVar2) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar8 = JPEG_ERR_OK;
        }
        jVar5 = (*_DAT_fffd8e68)(jd,4,0,jd->workbuf_y);
        jVar6 = (*_DAT_fffd8e7c)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd8e90)(jd,1,2,jd->workbuf_v);
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
        (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans_90)
                  (jd->y,jd->u,jd->v,jd->w_h,puVar10 + (jd->w_h[0] * iVar9 + 8) * (uint)jd->pixel);
        iVar9 = iVar9 + jd->w_h[3];
      } while (iVar9 <= (int)((uint)jd->width - (int)jd->w_h[3]));
    }
    if (iVar13 != 0) {
      if ((jd->nrst != 0) && (uVar1 = jd->rst, jd->rst = uVar1 + 1, jd->nrst == uVar1)) {
        uVar2 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar2 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar3 = *(ushort *)jd->dptr;
        uVar15 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar16 = _DAT_fffd8f84;
        uVar11 = _DAT_fffd8f80;
        jd->dctr = jd->dctr - 2;
        if (((uVar15 & uVar11) != uVar16) || (((uVar15 ^ uVar2) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar8 = JPEG_ERR_OK;
      }
      piVar18 = jd->workbuf_y;
      jd->w_h[2] = 8;
      jd->w_h[3] = 8;
      jVar5 = (*_DAT_fffd8fb8)(jd,4,0,piVar18);
      jVar6 = (*_DAT_fffd8fc8)(jd,1,1,jd->workbuf_u);
      jVar7 = (*_DAT_fffd8fe0)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
      (*jd->color_trans_90)
                (jd->y,jd->u,jd->v,jd->w_h,
                 puVar10 + (jd->w_h[0] * iVar12 * 0x10 + 8) * (uint)jd->pixel);
    }
    jd->w_h[2] = 0x10;
    jd->w_h[3] = 0x10;
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_180_unalign
 * Purpose: Decodes JPEG MCUs for YUV420 with 180-degree rotation using the edge path for dimensions not aligned to an MCU; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00019080
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_180_unalign(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  ushort uVar1;
  uint16_t uVar2;
  ushort uVar3;
  ushort uVar4;
  short sVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  int iVar10;
  uint8_t *puVar11;
  int iVar12;
  uint uVar13;
  uint uVar14;
  int iVar15;
  uint uVar16;
  int iVar17;
  int16_t *piVar18;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int width_left@[???]
                       Unresolved local var: int left_y@[???] */
  uVar14 = (uint)jd->width;
  iVar15 = (int)jd->w_h[2];
  uVar16 = (uint)jd->height;
  iVar17 = (int)jd->w_h[3];
  iVar12 = (int)uVar14 % iVar15;
  iVar10 = (int)uVar16 / iVar17;
  puVar11 = io->outbuf + jd->out_start_pos;
                    /* Unresolved local var: int h@[???] */
  sVar5 = (short)((int)uVar14 / iVar15);
  if (iVar10 < 1) {
    jVar9 = JPEG_ERR_OK;
  }
  else {
    jVar9 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    while( true ) {
      iVar17 = 0;
      if (iVar15 <= (int)uVar14) {
        do {
          if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
            uVar1 = jd->rsc;
            jd->rsc = uVar1 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dctr = jd->dctr - 2;
            uVar14 = _DAT_fffd910c;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar14) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar1 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar9 = JPEG_ERR_OK;
          }
          jVar6 = (*_DAT_fffd913c)(jd,4,0,jd->workbuf_y);
          jVar7 = (*_DAT_fffd9150)(jd,1,1,jd->workbuf_u);
          jVar8 = (*_DAT_fffd9164)(jd,1,2,jd->workbuf_v);
          jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar11 + -(int)(short)((ushort)jd->pixel * (short)iVar17));
          iVar17 = iVar17 + jd->w_h[2];
        } while (iVar17 <= (int)((uint)jd->width - (int)jd->w_h[2]));
      }
      if (iVar12 != 0) {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar1 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar1 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar13 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          uVar16 = _DAT_fffd9294;
          uVar14 = _DAT_fffd9290;
          jd->dctr = jd->dctr - 2;
          if ((uVar13 & uVar14) != uVar16) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar1 ^ uVar13) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar9 = JPEG_ERR_OK;
        }
        piVar18 = jd->workbuf_y;
        jd->w_h[3] = 0x10;
        jd->w_h[2] = 8;
        jVar6 = (*_DAT_fffd92cc)(jd,4,0,piVar18);
        jVar7 = (*_DAT_fffd92dc)(jd,1,1,jd->workbuf_u);
        jVar8 = (*_DAT_fffd92f4)(jd,1,2,jd->workbuf_v);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
        (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y,8);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans_90)
                  (jd->y,jd->u,jd->v,jd->w_h,
                   puVar11 + -(int)(short)((ushort)jd->pixel * (sVar5 * 0x10 + -8)));
        jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
        jd->w_h[2] = 0x10;
        jd->w_h[3] = 0x10;
      }
      iVar10 = iVar10 + -1;
      puVar11 = puVar11 + jd->out_h;
      if (iVar10 == 0) break;
      uVar14 = (uint)jd->width;
      iVar15 = (int)jd->w_h[2];
    }
    uVar16 = (uint)jd->height;
    iVar17 = (int)jd->w_h[3];
  }
  if ((int)uVar16 % iVar17 != 0) {
    uVar1 = jd->width;
    iVar10 = (uint)jd->pixel * (uint)uVar1;
    jd->w_h[2] = 0x10;
    jd->w_h[3] = 8;
                    /* Unresolved local var: int w@[???] */
    if (0xf < uVar1) {
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
      iVar15 = 0;
      do {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar1 = jd->rsc;
          jd->rsc = uVar1 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          uVar14 = _DAT_fffd9434;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar4 = uVar3 >> 8;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          jd->dctr = jd->dctr - 2;
          if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar14) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar1 ^ uVar4) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar9 = JPEG_ERR_OK;
        }
        jVar6 = (*_DAT_fffd9464)(jd,4,0,jd->workbuf_y);
        jVar7 = (*_DAT_fffd9478)(jd,1,1,jd->workbuf_u);
        jVar8 = (*_DAT_fffd948c)(jd,1,2,jd->workbuf_v);
        jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u + 0x20,jd->v + 0x20,jd->w_h,
                           puVar11 + (iVar10 * 8 - (int)(short)((ushort)jd->pixel * (short)iVar15)))
        ;
        iVar15 = iVar15 + jd->w_h[2];
      } while (iVar15 <= (int)((uint)jd->width - (int)jd->w_h[2]));
    }
    if (iVar12 != 0) {
      if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
        uVar1 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar1 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar3 = *(ushort *)jd->dptr;
        uVar13 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar16 = _DAT_fffd9588;
        uVar14 = _DAT_fffd9584;
        jd->dctr = jd->dctr - 2;
        if (((uVar13 & uVar14) != uVar16) || (((uVar13 ^ uVar1) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar9 = JPEG_ERR_OK;
      }
      piVar18 = jd->workbuf_y;
      jd->w_h[2] = 8;
      jd->w_h[3] = 8;
      jVar6 = (*_DAT_fffd95bc)(jd,4,0,piVar18);
      jVar7 = (*_DAT_fffd95d0)(jd,1,1,jd->workbuf_u);
      jVar8 = (*_DAT_fffd95e8)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      jVar9 = jVar9 | jVar6 | jVar7 | jVar8;
      (*jd->color_trans_90)
                (jd->y,jd->u + 0x20,jd->v + 0x20,jd->w_h,
                 puVar11 + (iVar10 * 8 - (int)(short)((ushort)jd->pixel * (sVar5 * 0x10 + -8))));
    }
    jd->w_h[2] = 0x10;
    jd->w_h[3] = 0x10;
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar9;
}

/* ==================================================================
 * jpeg_dec_proc_yuv420_270_unalign
 * Purpose: Decodes JPEG MCUs for YUV420 with 270-degree rotation using the edge path for dimensions not aligned to an MCU; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00019690
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv420_270_unalign(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  ushort uVar1;
  uint16_t uVar2;
  ushort uVar3;
  ushort uVar4;
  jpeg_error_t jVar5;
  jpeg_error_t jVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  int iVar9;
  uint8_t *puVar10;
  int iVar11;
  int iVar12;
  uint uVar13;
  uint uVar14;
  int iVar15;
  uint uVar16;
  int iVar17;
  int16_t *piVar18;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int width_left@[???]
                       Unresolved local var: int left_y@[???] */
  uVar14 = (uint)jd->width;
  iVar15 = (int)jd->w_h[3];
  uVar16 = (uint)jd->height;
  iVar17 = (int)jd->w_h[2];
  iVar12 = (int)uVar14 % iVar15;
  iVar9 = (int)uVar16 / iVar17;
  puVar10 = io->outbuf + jd->out_start_pos;
  iVar11 = (int)uVar14 / iVar15;
                    /* Unresolved local var: int h@[???] */
  if (iVar9 < 1) {
    jVar8 = JPEG_ERR_OK;
  }
  else {
    jVar8 = JPEG_ERR_OK;
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
    while( true ) {
      iVar17 = 0;
      if (iVar15 <= (int)uVar14) {
        do {
          if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
            uVar1 = jd->rsc;
            jd->rsc = uVar1 + 1;
            jd->bits_left = 0;
            jd->get_buffer = 0;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar3 = *(ushort *)jd->dptr;
            uVar4 = uVar3 >> 8;
            jd->dctr = jd->dctr - 2;
            uVar14 = _DAT_fffd9718;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar14) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar1 ^ uVar4) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            jVar8 = JPEG_ERR_OK;
          }
          jVar5 = (*_DAT_fffd9748)(jd,4,0,jd->workbuf_y);
          jVar6 = (*_DAT_fffd975c)(jd,1,1,jd->workbuf_u);
          jVar7 = (*_DAT_fffd9770)(jd,1,2,jd->workbuf_v);
          jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 0x80,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 0x88,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0xc0,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                             puVar10 + -(jd->w_h[0] * iVar17 * (uint)jd->pixel));
          iVar17 = iVar17 + jd->w_h[3];
        } while (iVar17 <= (int)((uint)jd->width - (int)jd->w_h[3]));
      }
      if (iVar12 != 0) {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar1 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar1 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar13 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          uVar16 = _DAT_fffd98a0;
          uVar14 = _DAT_fffd989c;
          jd->dctr = jd->dctr - 2;
          if ((uVar13 & uVar14) != uVar16) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar1 ^ uVar13) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar8 = JPEG_ERR_OK;
        }
        piVar18 = jd->workbuf_y;
        jd->w_h[2] = 0x10;
        jd->w_h[3] = 8;
        jVar5 = (*_DAT_fffd98d8)(jd,4,0,piVar18);
        jVar6 = (*_DAT_fffd98ec)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd9904)(jd,1,2,jd->workbuf_v);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x80,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u + 0x20,jd->v + 0x20,jd->w_h,
                           puVar10 + -((int)jd->w_h[0] * (iVar11 * 0x10 + -8) * (uint)jd->pixel));
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        jd->w_h[2] = 0x10;
        jd->w_h[3] = 0x10;
      }
      iVar9 = iVar9 + -1;
      puVar10 = puVar10 + jd->out_h;
      if (iVar9 == 0) break;
      uVar14 = (uint)jd->width;
      iVar15 = (int)jd->w_h[3];
    }
    uVar16 = (uint)jd->height;
    iVar17 = (int)jd->w_h[2];
  }
  if ((int)uVar16 % iVar17 != 0) {
    jd->w_h[2] = 8;
                    /* Unresolved local var: int w@[???] */
    uVar1 = jd->width;
    jd->w_h[3] = 0x10;
    if (0xf < uVar1) {
      iVar9 = 0;
      do {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar1 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar1 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar3 = *(ushort *)jd->dptr;
          uVar4 = uVar3 >> 8;
          jd->dctr = jd->dctr - 2;
          uVar14 = _DAT_fffd9a3c;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          if (((uVar3 & 0xff) << 8 | uVar4 & 0xd8) != uVar14) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar4 ^ uVar1) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          jVar8 = JPEG_ERR_OK;
        }
        jVar5 = (*_DAT_fffd9a6c)(jd,4,0,jd->workbuf_y);
        jVar6 = (*_DAT_fffd9a80)(jd,1,1,jd->workbuf_u);
        jVar7 = (*_DAT_fffd9a94)(jd,1,2,jd->workbuf_v);
        jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y + 0x40,8);
        (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y,8);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,
                           puVar10 + -(jd->w_h[0] * iVar9 * (uint)jd->pixel));
        iVar9 = iVar9 + jd->w_h[3];
      } while (iVar9 <= (int)((uint)jd->width - (int)jd->w_h[3]));
    }
    if (iVar12 != 0) {
      if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
        uVar1 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar1 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar3 = *(ushort *)jd->dptr;
        uVar13 = (uint)uVar3 << 8 | (uint)(uVar3 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar16 = _DAT_fffd9b8c;
        uVar14 = _DAT_fffd9b88;
        jd->dctr = jd->dctr - 2;
        if (((uVar13 & uVar14) != uVar16) || (((uVar13 ^ uVar1) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        jVar8 = JPEG_ERR_OK;
      }
      piVar18 = jd->workbuf_y;
      jd->w_h[2] = 8;
      jd->w_h[3] = 8;
      jVar5 = (*_DAT_fffd9bc0)(jd,4,0,piVar18);
      jVar6 = (*_DAT_fffd9bd4)(jd,1,1,jd->workbuf_u);
      jVar7 = (*_DAT_fffd9bec)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,8);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      jVar8 = jVar8 | jVar5 | jVar6 | jVar7;
      (*jd->color_trans)(jd->y,jd->u + 0x20,jd->v + 0x20,jd->w_h,
                         puVar10 + -((iVar11 * 0x10 + -8) * (int)jd->w_h[0] * (uint)jd->pixel));
    }
    jd->w_h[2] = 0x10;
    jd->w_h[3] = 0x10;
  }
  io->out_size = (uint)(jd->resize).width * (uint)(jd->resize).height * (uint)jd->pixel;
  jd->start_sos = true;
  return jVar8;
}

/* ==================================================================
 * jpeg_dec_proc_yuv422_0_variety
 * Purpose: Decodes JPEG MCUs for YUV422 without rotation using the general path selecting scale, clip and output details; orchestrates entropy decode, dequantization, IDCT and color packing.
 * Entry: 00019c94
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_proc_yuv422_0_variety(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  byte bVar1;
  uint16_t uVar2;
  ushort uVar3;
  ushort uVar4;
  ushort uVar5;
  jpeg_dec_io_t *pjVar6;
  jpeg_error_t jVar7;
  jpeg_error_t jVar8;
  jpeg_error_t jVar9;
  jpeg_dec_io_t *pjVar10;
  uint8_t *puVar11;
  uint16_t *puVar12;
  int iVar13;
  uint8_t *puVar14;
  jpeg_error_t jVar15;
  uint uVar16;
  int iVar17;
  uint uVar18;
  uint8_t *puVar19;
  int iVar20;
  uint uVar21;
  undefined4 local_50;
  undefined4 uStack_4c;
  jpeg_dec_io_t *pjStack_40;
  int iStack_3c;
  uint8_t *puStack_38;
  uint16_t *puStack_34;
  jpeg_error_t jStack_30;
  int iStack_2c;
  jpeg_dec_io_t *pjStack_28;

                    /* Unresolved local var: jpeg_error_t ret@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: uint8_t * tmp_buf@[???]
                       Unresolved local var: uint8_t * tmp_out@[???]
                       Unresolved local var: int16_t[4] tmp_w_h@[???]
                       Unresolved local var: int height_loop@[???]
                       Unresolved local var: int width_loop@[???]
                       Unresolved local var: int width_left@[???]
                       Unresolved local var: int left_y@[???] */
  puVar12 = jd->huffdata[1][1] + 0xfa;
  uVar18 = (uint)(jd->resize).width;
  iVar17 = (int)jd->w_h[2];
  local_50 = *_DAT_fffd9c98;
  uVar21 = (uint)jd->height;
  iVar20 = (int)jd->w_h[3];
  uStack_4c = _DAT_fffd9c98[1];
  iStack_2c = (int)uVar18 / iVar17;
  pjStack_40 = (jpeg_dec_io_t *)((int)uVar21 / iVar20);
  iStack_3c = (int)uVar18 % iVar17;
  pjVar10 = (jpeg_dec_io_t *)0x0;
  puVar14 = io->outbuf;
                    /* Unresolved local var: int h@[???] */
                    /* Unresolved local var: int w@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
  jVar15 = JPEG_ERR_OK;
  pjVar6 = io;
  if (0 < (int)pjStack_40) {
    while( true ) {
      pjStack_28 = pjVar6;
      iVar20 = 0;
      if (iVar17 <= (int)uVar18) {
        do {
          if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
            uVar3 = jd->rsc;
            jd->rsc = uVar3 + 1;
            jd->bits_left = jVar15;
            jd->get_buffer = jVar15;
            if ((uint)jd->dctr < 2) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            uVar4 = *(ushort *)jd->dptr;
            uVar5 = uVar4 >> 8;
            jd->dptr = (uint8_t *)((int)jd->dptr + 2);
            uVar18 = _DAT_fffd9d28;
            jd->dctr = jd->dctr - 2;
            if (((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar18) {
              return JPEG_ERR_BAD_DATA;
            }
            if (((uVar3 ^ uVar5) & 7) != 0) {
              return JPEG_ERR_BAD_DATA;
            }
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->rst = 1;
            pjVar10 = (jpeg_dec_io_t *)jVar15;
          }
          jVar7 = (*_DAT_fffd9d50)(jd,2,jVar15,jd->workbuf_y);
          jVar8 = (*_DAT_fffd9d64)(jd,1,1,jd->workbuf_u);
          jVar9 = (*_DAT_fffd9d78)(jd,1,2,jd->workbuf_v);
          pjVar10 = (jpeg_dec_io_t *)((uint)pjVar10 | jVar7 | jVar8 | jVar9);
          (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
          (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
          (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
          (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
          (*jd->color_trans)(jd->y,jd->u,jd->v,jd->w_h,puVar14 + (uint)jd->pixel * iVar20);
          iVar20 = iVar20 + jd->w_h[2];
        } while (iVar20 <= (int)((uint)puVar12[6] - (int)jd->w_h[2]));
      }
      if (iStack_3c != 0) {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar3 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar4 = *(ushort *)jd->dptr;
          uVar16 = (uint)uVar4 << 8 | (uint)(uVar4 >> 8);
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          uVar21 = _DAT_fffd9e64;
          uVar18 = _DAT_fffd9e60;
          jd->dctr = jd->dctr - 2;
          if ((uVar16 & uVar18) != uVar21) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar16 ^ uVar3) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          pjVar10 = (jpeg_dec_io_t *)0x0;
        }
        jVar7 = (*_DAT_fffd9e90)(jd,2,jVar15,jd->workbuf_y);
        jVar8 = (*_DAT_fffd9ea4)(jd,1,1,jd->workbuf_u);
        jVar9 = (*_DAT_fffd9eb8)(jd,1,2,jd->workbuf_v);
        pjVar10 = (jpeg_dec_io_t *)((uint)pjVar10 | jVar7 | jVar8 | jVar9);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
        iVar20 = iStack_3c;
                    /* Unresolved local var: int i@[???] */
        if (0 < jd->w_h[3]) {
          uVar18 = (uint)jd->pixel;
          puVar19 = jd->unalign_output;
          puVar11 = puVar14 + uVar18 * iStack_2c * 0x10;
          iVar17 = 0;
          puStack_38 = puVar14;
          puStack_34 = puVar12;
          jStack_30 = jVar15;
          do {
            (*_DAT_fffd9f6c)(puVar11,puVar19,(int)(short)uVar18 * (int)(short)iVar20);
            bVar1 = jd->pixel;
            uVar18 = (uint)bVar1;
            iVar17 = iVar17 + 1;
            puVar19 = puVar19 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
            puVar11 = puVar11 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
            puVar12 = puStack_34;
            puVar14 = puStack_38;
            jVar15 = jStack_30;
          } while (iVar17 < jd->w_h[3]);
        }
      }
      pjStack_40 = (jpeg_dec_io_t *)((int)pjStack_40 + -1);
                    /* Unresolved local var: int w@[???] */
      uVar18 = (uint)puVar12[6];
      puVar14 = puVar14 + jd->out_h;
      if (pjStack_40 == (jpeg_dec_io_t *)0x0) break;
      iVar17 = (int)jd->w_h[2];
      pjVar6 = pjStack_28;
    }
    uVar21 = (uint)jd->height;
    iVar20 = (int)jd->w_h[3];
    io = pjStack_28;
  }
  iVar20 = (int)uVar21 % iVar20;
  if (iVar20 == 0) {
    uVar21 = (uint)jd->pixel;
  }
  else {
    jStack_30 = iVar20;
    if ((int)jd->w_h[2] <= (int)uVar18) {
      iVar17 = 0;
      pjStack_40 = pjVar10;
      puStack_38 = puVar14;
      puStack_34 = puVar12;
      pjStack_28 = io;
      do {
        if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
          uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
          jd->rsc = uVar3 + 1;
          jd->bits_left = 0;
          jd->get_buffer = 0;
          if ((uint)jd->dctr < 2) {
            return JPEG_ERR_NO_MORE_DATA;
          }
          uVar4 = *(ushort *)jd->dptr;
          uVar5 = uVar4 >> 8;
          jd->dctr = jd->dctr - 2;
          uVar18 = _DAT_fffda02c;
          jd->dptr = (uint8_t *)((int)jd->dptr + 2);
          if (((uVar4 & 0xff) << 8 | uVar5 & 0xd8) != uVar18) {
            return JPEG_ERR_BAD_DATA;
          }
          if (((uVar3 ^ uVar5) & 7) != 0) {
            return JPEG_ERR_BAD_DATA;
          }
          jd->dcv[0] = 0;
          jd->dcv[1] = 0;
          jd->dcv[2] = 0;
          jd->rst = 1;
          pjStack_40 = (jpeg_dec_io_t *)0x0;
        }
        jVar15 = (*_DAT_fffda05c)(jd,2,0,jd->workbuf_y);
        jVar15 = (uint)pjStack_40 | jVar15;
        jVar7 = (*_DAT_fffda074)(jd,1,1,jd->workbuf_u);
        jVar8 = (*_DAT_fffda088)(jd,1,2,jd->workbuf_v);
        pjStack_40 = (jpeg_dec_io_t *)(jVar15 | jVar7 | jVar8);
        (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
        (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
        (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
        (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
        (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
        uVar18 = (uint)jd->pixel;
        puVar14 = jd->unalign_output;
                    /* Unresolved local var: int i@[???] */
        iVar13 = 0;
        puVar11 = puStack_38 + uVar18 * iVar17;
        do {
          (*_DAT_fffda12c)(puVar11,puVar14,(int)jd->w_h[2] * uVar18);
          bVar1 = jd->pixel;
          uVar18 = (uint)bVar1;
          iVar13 = iVar13 + 1;
          puVar14 = puVar14 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
          puVar11 = puVar11 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
        } while (iVar20 != iVar13);
        iVar17 = iVar17 + jd->w_h[2];
        pjVar10 = pjStack_40;
        puVar12 = puStack_34;
        puVar14 = puStack_38;
        io = pjStack_28;
      } while (iVar17 <= (int)((uint)puStack_34[6] - (int)jd->w_h[2]));
    }
    if (iStack_3c == 0) {
      uVar18 = (uint)puVar12[6];
      uVar21 = (uint)jd->pixel;
    }
    else {
      if ((jd->nrst != 0) && (uVar2 = jd->rst, jd->rst = uVar2 + 1, jd->nrst == uVar2)) {
        uVar3 = jd->rsc;
                    /* Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t dc@[???]
                       Unresolved local var: uint16_t d@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int c@[???] */
        jd->rsc = uVar3 + 1;
        jd->bits_left = 0;
        jd->get_buffer = 0;
        if ((uint)jd->dctr < 2) {
          return JPEG_ERR_NO_MORE_DATA;
        }
        uVar4 = *(ushort *)jd->dptr;
        uVar16 = (uint)uVar4 << 8 | (uint)(uVar4 >> 8);
        jd->dptr = (uint8_t *)((int)jd->dptr + 2);
        uVar21 = _DAT_fffda1bc;
        uVar18 = _DAT_fffda1b4;
        jd->dctr = jd->dctr - 2;
        if (((uVar16 & uVar18) != uVar21) || (((uVar3 ^ uVar16) & 7) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        jd->dcv[0] = 0;
        jd->dcv[1] = 0;
        jd->dcv[2] = 0;
        jd->rst = 1;
        pjVar10 = (jpeg_dec_io_t *)0x0;
      }
      pjStack_40 = io;
      jVar15 = (*_DAT_fffda1e8)(jd,2,0,jd->workbuf_y);
      jVar7 = (*_DAT_fffda1fc)(jd,1,1,jd->workbuf_u);
      jVar8 = (*_DAT_fffda214)(jd,1,2,jd->workbuf_v);
      (*jd->idct_y)(jd->workbuf_y,8,jd->qttbl[jd->qtid[0]],jd->y,0x10);
      (*jd->idct_y)(jd->workbuf_y + 0x40,8,jd->qttbl[jd->qtid[0]],jd->y + 8,0x10);
      (*jd->idct_uv)(jd->workbuf_u,8,jd->qttbl[jd->qtid[1]],jd->u,8);
      (*jd->idct_uv)(jd->workbuf_v,8,jd->qttbl[jd->qtid[2]],jd->v,8);
      (*jd->color_trans)(jd->y,jd->u,jd->v,(int16_t *)&local_50,jd->unalign_output);
      uVar21 = (uint)jd->pixel;
      puVar11 = jd->unalign_output;
      pjVar10 = (jpeg_dec_io_t *)((uint)pjVar10 | jVar15 | jVar7 | jVar8);
                    /* Unresolved local var: int i@[???] */
      puVar14 = puVar14 + uVar21 * iStack_2c * 0x10;
      iVar20 = 0;
      do {
        (*_DAT_fffda2c4)(puVar14,puVar11,(int)(short)uVar21 * (int)(short)iStack_3c);
        bVar1 = jd->pixel;
        uVar21 = (uint)bVar1;
        iVar20 = iVar20 + 1;
        puVar11 = puVar11 + (int)(short)uStack_4c * (int)(short)(ushort)bVar1;
        puVar14 = puVar14 + (int)jd->w_h[0] * (int)(short)(ushort)bVar1;
      } while (jStack_30 != iVar20);
      uVar18 = (uint)puVar12[6];
      io = pjStack_40;
    }
  }
  io->out_size = puVar12[7] * uVar18 * uVar21;
  *(undefined1 *)(puVar12 + 0x79) = 1;
  return (jpeg_error_t)pjVar10;
}

/* ==================================================================
 * jpeg_dec_process_0
 * Purpose: Dispatches non-rotated decoding to the prepared grayscale, YUV444, YUV422 or YUV420 MCU kernel.
 * Entry: 0001a308
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void jpeg_dec_process_0(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  ushort uVar1;
  jpeg_decoder_prefer_t jVar2;
  idct_func p_Var3;
  uint uVar4;
  decode_process_func p_Var5;
  idct_func p_Var6;
  uint uVar7;
  color_func_with_scale p_Var8;
  color_func p_Var9;
  jpeg_rotate_t jVar10;
  color_func p_Var11;
  int iVar12;
  short sVar13;
  int iVar14;
  int iVar15;

  uVar4 = 0;
  if (jd->component_num != 1) {
    uVar4 = ((uint)(ushort)jd->msx + (uint)(ushort)jd->msy) - 1 & 0xff;
  }
  jd->process_id = (uint8_t)uVar4;
  iVar12 = _DAT_fffda388;
  p_Var6 = _DAT_fffda348;
  uVar1 = (jd->resize).width;
  jVar10 = jd->rotate;
  uVar7 = (uint)jd->color_id;
  if (jd->scale_enable == false) {
    iVar14 = (int)jd->msy << 3;
    sVar13 = (short)iVar14;
    iVar15 = (int)jd->msx << 3;
    jd->w_h[0] = uVar1;
    jVar2 = jd->prefer_type;
    jd->out_h = (int)(short)(ushort)jd->pixel * (int)sVar13 * (uint)uVar1;
    jd->w_h[1] = 0;
    jd->w_h[2] = (int16_t)iVar15;
    jd->w_h[3] = sVar13;
    jd->out_start_pos = 0;
    p_Var6 = *(idct_func *)(jVar10 * 4 + iVar12);
    if (jVar2 == JPEG_PREFER_QUALITY) {
      iVar12 = uVar4 * 4 + uVar7;
      p_Var11 = *(color_func *)(iVar12 * 4 + _DAT_fffda3d8);
      p_Var9 = *(color_func *)(iVar12 * 4 + _DAT_fffda3e0);
      jd->idct_y = p_Var6;
      jd->idct_uv = p_Var6;
      jd->color_trans = p_Var11;
    }
    else {
      iVar12 = 0;
      if (uVar4 != 0) {
        iVar12 = 3;
      }
      iVar12 = iVar12 * 4 + uVar7;
      p_Var3 = *(idct_func *)((uVar4 * 4 + jVar10) * 4 + _DAT_fffda3ac);
      p_Var11 = *(color_func *)(iVar12 * 4 + _DAT_fffda3b4);
      p_Var9 = *(color_func *)(iVar12 * 4 + _DAT_fffda3c0);
      jd->idct_y = p_Var6;
      jd->idct_uv = p_Var3;
      jd->color_trans = p_Var11;
    }
    iVar12 = uVar4 * 4;
    jd->color_trans_90 = p_Var9;
    if (jd->block_enable == false) {
      if (jd->clipper_enable == false) {
        if (((int)(uint)jd->width % iVar15 == 0) && ((int)(uint)jd->height % iVar14 == 0)) {
          p_Var5 = *(decode_process_func *)(_DAT_fffda428 + iVar12);
        }
        else if (((jd->width & 7) == 0) && ((jd->height & 7) == 0)) {
          p_Var5 = *(decode_process_func *)(_DAT_fffda444 + iVar12);
        }
        else {
          p_Var5 = *(decode_process_func *)(_DAT_fffda44c + iVar12);
        }
      }
      else {
        p_Var5 = *(decode_process_func *)(_DAT_fffda410 + iVar12);
      }
    }
    else {
      p_Var5 = *(decode_process_func *)(_DAT_fffda3fc + iVar12);
    }
  }
  else {
    p_Var8 = *(color_func_with_scale *)((uVar7 * 4 + jVar10) * 4 + _DAT_fffda338);
    jd->out_h = (uint)jd->pixel * (uint)uVar1 * 8;
    p_Var5 = _DAT_fffda354;
    jd->w_h[0] = uVar1;
    jd->w_h[1] = 8;
    jd->w_h[2] = uVar1;
    jd->out_start_pos = 0;
    jd->idct_y = p_Var6;
    jd->idct_uv = p_Var6;
    jd->color_trans_scale = p_Var8;
  }
  jd->process = p_Var5;
  return;
}

/* ==================================================================
 * jpeg_dec_process_90
 * Purpose: Dispatches decoding to the prepared 90-degree rotation kernel.
 * Entry: 0001a458
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void jpeg_dec_process_90(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  ushort uVar1;
  uint16_t uVar2;
  uint uVar3;
  idct_func p_Var4;
  uint uVar5;
  decode_process_func p_Var6;
  idct_func p_Var7;
  uint uVar8;
  color_func_with_scale p_Var9;
  color_func p_Var10;
  jpeg_rotate_t jVar11;
  color_func p_Var12;
  int iVar13;
  short sVar14;
  int iVar15;
  jpeg_decoder_prefer_t jVar16;
  int iVar17;

  uVar5 = 0;
  if (jd->component_num != 1) {
    uVar5 = ((uint)(ushort)jd->msx + (uint)(ushort)jd->msy) - 1 & 0xff;
  }
  jd->process_id = (uint8_t)uVar5;
  iVar17 = _DAT_fffda4dc;
  p_Var6 = _DAT_fffda4a8;
  p_Var7 = _DAT_fffda498;
  uVar1 = (jd->resize).height;
  uVar3 = (uint)jd->pixel;
  jVar11 = jd->rotate;
  uVar8 = (uint)jd->color_id;
  if (jd->scale_enable == false) {
    iVar15 = (int)jd->msy << 3;
    sVar14 = (short)iVar15;
    iVar13 = (int)jd->msx << 3;
    jd->w_h[0] = uVar1;
    jd->out_h = -(int)sVar14 * uVar3;
    jVar16 = jd->prefer_type;
    jd->out_start_pos = ((uint)uVar1 - (int)sVar14) * uVar3;
    jd->w_h[1] = 1;
    jd->w_h[2] = sVar14;
    jd->w_h[3] = (int16_t)iVar13;
    p_Var7 = *(idct_func *)(jVar11 * 4 + iVar17);
    if (jVar16 == JPEG_PREFER_QUALITY) {
      iVar17 = uVar5 * 4 + uVar8;
      p_Var12 = *(color_func *)(iVar17 * 4 + _DAT_fffda534);
      p_Var10 = *(color_func *)(iVar17 * 4 + _DAT_fffda53c);
      jd->idct_y = p_Var7;
      jd->idct_uv = p_Var7;
      jd->color_trans = p_Var12;
    }
    else {
      iVar17 = 3;
      if (uVar5 == 0) {
        iVar17 = 0;
      }
      iVar17 = iVar17 * 4 + uVar8;
      p_Var4 = *(idct_func *)((uVar5 * 4 + jVar11) * 4 + _DAT_fffda508);
      p_Var12 = *(color_func *)(iVar17 * 4 + _DAT_fffda510);
      p_Var10 = *(color_func *)(iVar17 * 4 + _DAT_fffda51c);
      jd->idct_y = p_Var7;
      jd->idct_uv = p_Var4;
      jd->color_trans = p_Var12;
    }
    iVar17 = uVar5 * 4;
    jd->color_trans_90 = p_Var10;
    if (jd->clipper_enable == false) {
      if (((int)(uint)jd->width % iVar13 == 0) && ((int)(uint)jd->height % iVar15 == 0)) {
        p_Var6 = *(decode_process_func *)(_DAT_fffda574 + iVar17);
      }
      else {
        p_Var6 = *(decode_process_func *)(_DAT_fffda580 + iVar17);
      }
    }
    else {
      p_Var6 = *(decode_process_func *)(_DAT_fffda558 + iVar17);
    }
  }
  else {
    uVar2 = (jd->resize).width;
    p_Var9 = *(color_func_with_scale *)((uVar8 * 4 + jVar11) * 4 + _DAT_fffda488);
    jd->w_h[0] = uVar1;
    jd->w_h[1] = 8;
    jd->w_h[2] = uVar2;
    jd->out_h = uVar3 * -8;
    jd->out_start_pos = (uVar1 - 1) * uVar3;
    jd->idct_y = p_Var7;
    jd->idct_uv = p_Var7;
    jd->color_trans_scale = p_Var9;
  }
  jd->process = p_Var6;
  return;
}

/* ==================================================================
 * jpeg_dec_process_180
 * Purpose: Dispatches decoding to the prepared 180-degree rotation kernel.
 * Entry: 0001a58c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void jpeg_dec_process_180(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  ushort uVar1;
  short sVar2;
  int iVar3;
  short sVar4;
  int iVar5;
  uint uVar6;
  idct_func p_Var7;
  uint uVar8;
  uint uVar9;
  color_func p_Var10;
  decode_process_func p_Var11;
  uint uVar12;
  color_func_with_scale p_Var13;
  undefined4 *puVar14;
  jpeg_decoder_prefer_t jVar15;
  int iVar16;
  color_func p_Var17;
  idct_func p_Var18;
  jpeg_rotate_t jVar19;
  uint uVar20;

  uVar8 = 0;
  if (jd->component_num != 1) {
    uVar8 = ((uint)(ushort)jd->msx + (uint)(ushort)jd->msy) - 1 & 0xff;
  }
  jd->process_id = (uint8_t)uVar8;
  p_Var11 = _DAT_fffda5e0;
  p_Var7 = _DAT_fffda5d4;
  uVar1 = (jd->resize).width;
  uVar12 = (uint)uVar1;
  uVar20 = (uint)jd->pixel;
  uVar6 = (uint)(jd->resize).height;
  jVar19 = jd->rotate;
  uVar9 = (uint)jd->color_id;
  if (jd->scale_enable == false) {
    iVar5 = (int)jd->msy << 3;
    sVar4 = (short)iVar5;
    iVar3 = (int)jd->msx << 3;
    sVar2 = (short)iVar3;
    puVar14 = (undefined4 *)(jVar19 * 4 + _DAT_fffda628);
    jd->out_start_pos = (((uVar6 - (int)sVar4) + 1) * uVar12 - (int)sVar2) * uVar20;
    p_Var7 = (idct_func)*puVar14;
    jVar15 = jd->prefer_type;
    jd->w_h[0] = uVar1;
    jd->w_h[1] = 2;
    jd->w_h[2] = sVar2;
    jd->w_h[3] = sVar4;
    jd->out_h = -(int)sVar4 * uVar12 * uVar20;
    if (jVar15 == JPEG_PREFER_QUALITY) {
      iVar16 = uVar8 * 4 + uVar9;
      p_Var17 = *(color_func *)(iVar16 * 4 + _DAT_fffda680);
      p_Var10 = *(color_func *)(iVar16 * 4 + _DAT_fffda68c);
      jd->idct_y = p_Var7;
      jd->idct_uv = p_Var7;
      jd->color_trans = p_Var17;
    }
    else {
      iVar16 = 3;
      if (uVar8 == 0) {
        iVar16 = 0;
      }
      iVar16 = iVar16 * 4 + uVar9;
      p_Var18 = *(idct_func *)((uVar8 * 4 + jVar19) * 4 + _DAT_fffda658);
      p_Var17 = *(color_func *)(iVar16 * 4 + _DAT_fffda660);
      p_Var10 = *(color_func *)(iVar16 * 4 + _DAT_fffda668);
      jd->idct_y = p_Var7;
      jd->idct_uv = p_Var18;
      jd->color_trans = p_Var17;
    }
    iVar16 = uVar8 * 4;
    jd->color_trans_90 = p_Var10;
    if (jd->clipper_enable == false) {
      if (((int)(uint)jd->width % iVar3 == 0) && ((int)(uint)jd->height % iVar5 == 0)) {
        p_Var11 = *(decode_process_func *)(_DAT_fffda6c4 + iVar16);
      }
      else {
        p_Var11 = *(decode_process_func *)(_DAT_fffda6d0 + iVar16);
      }
    }
    else {
      p_Var11 = *(decode_process_func *)(_DAT_fffda6a8 + iVar16);
    }
  }
  else {
    p_Var13 = *(color_func_with_scale *)((uVar9 * 4 + jVar19) * 4 + _DAT_fffda5c0);
    jd->w_h[0] = uVar1;
    jd->w_h[1] = 8;
    jd->w_h[2] = uVar1;
    jd->out_h = uVar12 * uVar20 * -8;
    jd->out_start_pos = (uVar6 - 1) * uVar12 * uVar20;
    jd->idct_y = p_Var7;
    jd->idct_uv = p_Var7;
    jd->color_trans_scale = p_Var13;
  }
  jd->process = p_Var11;
  return;
}

/* ==================================================================
 * jpeg_dec_process_270
 * Purpose: Dispatches decoding to the prepared 270-degree rotation kernel.
 * Entry: 0001a6dc
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void jpeg_dec_process_270(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  byte bVar1;
  ushort uVar2;
  ushort uVar3;
  uint uVar4;
  jpeg_decoder_prefer_t jVar5;
  idct_func p_Var6;
  uint uVar7;
  idct_func p_Var8;
  uint uVar9;
  color_func p_Var10;
  decode_process_func p_Var11;
  jpeg_rotate_t jVar12;
  color_func_with_scale p_Var13;
  color_func p_Var14;
  short sVar15;
  int iVar16;
  short sVar17;
  int iVar18;
  int iVar19;

  uVar7 = 0;
  if (jd->component_num != 1) {
    uVar7 = ((uint)(ushort)jd->msx + (uint)(ushort)jd->msy) - 1 & 0xff;
  }
  jd->process_id = (uint8_t)uVar7;
  p_Var11 = _DAT_fffda72c;
  p_Var8 = _DAT_fffda720;
  uVar2 = (jd->resize).height;
  bVar1 = jd->pixel;
  uVar4 = (uint)bVar1;
  uVar3 = (jd->resize).width;
  jVar12 = jd->rotate;
  uVar9 = (uint)jd->color_id;
  if (jd->scale_enable == false) {
    iVar16 = (int)jd->msx << 3;
    sVar15 = (short)iVar16;
    iVar18 = (int)jd->msy << 3;
    sVar17 = (short)iVar18;
    jd->w_h[0] = uVar2;
    iVar19 = _DAT_fffda76c;
    jd->w_h[1] = 3;
    jVar5 = jd->prefer_type;
    jd->out_start_pos = ((uint)uVar3 - (int)sVar15) * (uint)uVar2 * uVar4;
    jd->w_h[2] = sVar17;
    jd->w_h[3] = sVar15;
    jd->out_h = (int)(short)(ushort)bVar1 * (int)sVar17;
    p_Var8 = *(idct_func *)(jVar12 * 4 + iVar19);
    if (jVar5 == JPEG_PREFER_QUALITY) {
      iVar19 = uVar7 * 4 + uVar9;
      p_Var14 = *(color_func *)(iVar19 * 4 + _DAT_fffda7b8);
      p_Var10 = *(color_func *)(iVar19 * 4 + _DAT_fffda7c4);
      jd->idct_y = p_Var8;
      jd->idct_uv = p_Var8;
      jd->color_trans = p_Var14;
    }
    else {
      iVar19 = 0;
      if (uVar7 != 0) {
        iVar19 = 3;
      }
      iVar19 = iVar19 * 4 + uVar9;
      p_Var6 = *(idct_func *)((uVar7 * 4 + jVar12) * 4 + _DAT_fffda790);
      p_Var14 = *(color_func *)(iVar19 * 4 + _DAT_fffda798);
      p_Var10 = *(color_func *)(iVar19 * 4 + _DAT_fffda7a4);
      jd->idct_y = p_Var8;
      jd->idct_uv = p_Var6;
      jd->color_trans = p_Var14;
    }
    iVar19 = uVar7 * 4;
    jd->color_trans_90 = p_Var10;
    if (jd->clipper_enable == false) {
      if (((int)(uint)jd->width % iVar16 == 0) && ((int)(uint)jd->height % iVar18 == 0)) {
        p_Var11 = *(decode_process_func *)(_DAT_fffda7fc + iVar19);
      }
      else {
        p_Var11 = *(decode_process_func *)(_DAT_fffda804 + iVar19);
      }
    }
    else {
      p_Var11 = *(decode_process_func *)(_DAT_fffda7e0 + iVar19);
    }
  }
  else {
    p_Var13 = *(color_func_with_scale *)((uVar9 * 4 + jVar12) * 4 + _DAT_fffda710);
    jd->w_h[0] = uVar2;
    jd->w_h[1] = 8;
    jd->w_h[2] = uVar3;
    jd->out_h = uVar4 << 3;
    jd->out_start_pos = (uVar3 - 1) * (uint)uVar2 * uVar4;
    jd->idct_y = p_Var8;
    jd->idct_uv = p_Var8;
    jd->color_trans_scale = p_Var13;
  }
  jd->process = p_Var11;
  return;
}
