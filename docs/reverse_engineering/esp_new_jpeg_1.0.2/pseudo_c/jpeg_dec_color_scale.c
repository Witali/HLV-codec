/* ESP_NEW_JPEG 1.0.2 ESP32 decoder Ghidra pseudo-C.
 * Derived from the Espressif binary; see LICENSE.ESPRESSIF in the analysis directory.
 * Program: jpeg_dec_color_scale.c.obj
 * Language: Xtensa:LE:32:default
 * Types and names are reconstructed and must not be treated as the original source.
 */

/* ==================================================================
 * yuv4442rgb888_mcu
 * Purpose: Converts YUV444 samples to RGB888 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 000100a8
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb888_mcu(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  short sVar1;
  int iVar2;
  int iVar3;
  short *psVar4;
  int iVar5;
  short sVar6;
  int iVar7;
  int iVar8;
  ushort *puVar9;
  int iVar10;
  short *psVar11;
  ushort *local_40;

  iVar3 = _DAT_fffd00d0;
                    /* Unresolved local var: int16_t * ybuf@[???]
                       Unresolved local var: int16_t * ubuf@[???]
                       Unresolved local var: int16_t * vbuf@[???]
                       Unresolved local var: int w_b@[???] */
  iVar2 = _DAT_fffd00cc;
                    /* Unresolved local var: int i@[???] */
  iVar7 = (int)w_h[1];
  if (0 < iVar7) {
    sVar6 = w_h[2];
    iVar5 = (int)sVar6;
    iVar8 = *w_h - iVar5;
    local_40 = (ushort *)y;
    while( true ) {
      if (0 < iVar5) {
                    /* Unresolved local var: int index@[???] */
        iVar10 = 0;
        psVar4 = v;
        puVar9 = local_40;
        psVar11 = u;
        do {
                    /* Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
          sVar6 = *psVar4;
          sVar1 = *psVar11;
          *rgb_out = *(uint8_t *)
                      (iVar2 + (((sVar6 + -0x80) * iVar2 >> 0xc) + (uint)*puVar9 & 0x3ff));
          rgb_out[1] = *(uint8_t *)
                        (iVar2 + ((uint)*puVar9 -
                                  ((sVar1 + -0x80) * 0x581 + (sVar6 + -0x80) * 0xb6d >> 0xc) & 0x3ff
                                 ));
          iVar10 = iVar10 + 1;
          rgb_out[2] = *(uint8_t *)
                        (iVar2 + (((sVar1 + -0x80) * iVar3 >> 0xc) + (uint)*puVar9 & 0x3ff));
          sVar6 = w_h[2];
          rgb_out = rgb_out + 3;
          iVar5 = (int)sVar6;
          psVar4 = psVar4 + 1;
          psVar11 = psVar11 + 1;
          puVar9 = puVar9 + 1;
        } while (iVar10 < iVar5);
      }
      iVar7 = iVar7 + -1;
      if (iVar7 == 0) break;
      u = u + iVar5;
      local_40 = local_40 + iVar5;
      v = v + iVar5;
      rgb_out = rgb_out + iVar8 * 3;
      iVar5 = (int)sVar6;
    }
  }
  return;
}

/* ==================================================================
 * yuv4442rgb888_90_line
 * Purpose: Converts YUV444 samples to RGB888 with 90-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 0001018c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb888_90_line(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  short sVar1;
  short sVar2;
  short sVar3;
  int iVar4;
  int iVar5;
  int iVar6;
  int16_t iVar7;
  short sVar8;
  int iVar9;
  uint8_t *puVar10;
  int iVar11;
  undefined1 uVar12;
  longlong in_ACC;
  longlong lVar13;

  iVar5 = _DAT_fffd01a8;
                    /* Unresolved local var: int w_b@[???] */
  iVar4 = _DAT_fffd01a4;
                    /* Unresolved local var: int i@[???] */
  iVar6 = (int)w_h[2];
  if (0 < iVar6) {
    sVar1 = *w_h;
    iVar7 = w_h[1];
    iVar11 = 0;
    do {
      iVar9 = 0;
      puVar10 = rgb_out;
      if (0 < iVar7) {
        do {
          sVar8 = (short)iVar9;
                    /* Unresolved local var: int index@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
          iVar6 = (int)sVar8 * (int)(short)iVar6 + iVar11;
          sVar2 = v[iVar6];
          sVar3 = u[iVar6];
          *puVar10 = *(uint8_t *)
                      (iVar4 + (((sVar2 + -0x80) * iVar5 >> 0xc) + (uint)(ushort)y[iVar6] & 0x3ff));
          wsr((char)in_ACC,iVar11);
          lVar13 = in_ACC + (longlong)w_h[2] * (longlong)sVar8;
          uVar12 = (undefined1)lVar13;
          iVar6 = rsr(uVar12);
          wsr(uVar12,iVar11);
          puVar10[1] = *(uint8_t *)
                        (iVar4 + ((uint)(ushort)y[iVar6] -
                                  ((sVar3 + -0x80) * 0x581 + (sVar2 + -0x80) * 0xb6d >> 0xc) & 0x3ff
                                 ));
          in_ACC = lVar13 + (longlong)w_h[2] * (longlong)sVar8;
          iVar6 = rsr((char)in_ACC);
          iVar9 = iVar9 + 1;
          puVar10[2] = *(uint8_t *)
                        (iVar4 + (((sVar3 + -0x80) * _DAT_fffd022c >> 0xc) + (uint)(ushort)y[iVar6]
                                 & 0x3ff));
          iVar7 = w_h[1];
          iVar6 = (int)w_h[2];
          puVar10 = puVar10 + -3;
        } while (iVar9 < iVar7);
      }
      iVar11 = iVar11 + 1;
      rgb_out = rgb_out + sVar1 * 3;
    } while (iVar11 < iVar6);
  }
  return;
}

/* ==================================================================
 * yuv4442rgb888_180_line
 * Purpose: Converts YUV444 samples to RGB888 with 180-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 0001027c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb888_180_line(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  short sVar1;
  short sVar2;
  short sVar3;
  int iVar4;
  int iVar5;
  int iVar6;
  short *psVar7;
  int iVar8;
  int16_t iVar9;
  int iVar10;
  ushort *puVar11;
  int iVar12;
  short *psVar13;
  ushort *local_40;

  iVar6 = _DAT_fffd02a4;
  iVar5 = _DAT_fffd02a0;
                    /* Unresolved local var: int16_t * ybuf@[???]
                       Unresolved local var: int16_t * ubuf@[???]
                       Unresolved local var: int16_t * vbuf@[???]
                       Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: int w_b@[???] */
  iVar4 = _DAT_fffd029c;
                    /* Unresolved local var: int i@[???] */
  iVar10 = (int)w_h[1];
  if (0 < iVar10) {
    sVar1 = *w_h;
    iVar9 = w_h[2];
    local_40 = (ushort *)y;
    do {
      iVar8 = (int)iVar9;
      if (0 < iVar8) {
                    /* Unresolved local var: int index@[???] */
        iVar12 = 0;
        psVar7 = v;
        puVar11 = local_40;
        psVar13 = u;
        do {
                    /* Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
          sVar2 = *psVar7;
          sVar3 = *psVar13;
          *rgb_out = *(uint8_t *)
                      (iVar4 + (((sVar2 + -0x80) * iVar5 >> 0xc) + (uint)*puVar11 & 0x3ff));
          rgb_out[1] = *(uint8_t *)
                        (iVar4 + ((uint)*puVar11 -
                                  ((sVar3 + -0x80) * 0x581 + (sVar2 + -0x80) * 0xb6d >> 0xc) & 0x3ff
                                 ));
          iVar12 = iVar12 + 1;
          rgb_out[2] = *(uint8_t *)
                        (iVar4 + (((sVar3 + -0x80) * iVar6 >> 0xc) + (uint)*puVar11 & 0x3ff));
          iVar9 = w_h[2];
          rgb_out = rgb_out + 3;
          iVar8 = (int)iVar9;
          psVar7 = psVar7 + 1;
          psVar13 = psVar13 + 1;
          puVar11 = puVar11 + 1;
        } while (iVar12 < iVar8);
      }
      iVar10 = iVar10 + -1;
      rgb_out = rgb_out + sVar1 * -6;
      u = u + iVar8;
      local_40 = local_40 + iVar8;
      v = v + iVar8;
    } while (iVar10 != 0);
  }
  return;
}

/* ==================================================================
 * yuv4442rgb888_270_line
 * Purpose: Converts YUV444 samples to RGB888 with 270-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 0001035c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb888_270_line(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  short sVar1;
  short sVar2;
  short sVar3;
  int iVar4;
  int iVar5;
  int iVar6;
  int16_t iVar7;
  short sVar8;
  int iVar9;
  uint8_t *puVar10;
  int iVar11;
  undefined1 uVar12;
  longlong in_ACC;
  longlong lVar13;

  iVar5 = _DAT_fffd037c;
                    /* Unresolved local var: int w_b@[???] */
  iVar4 = _DAT_fffd0378;
                    /* Unresolved local var: int i@[???] */
  iVar6 = (int)w_h[2];
  if (0 < iVar6) {
    sVar1 = *w_h;
    iVar7 = w_h[1];
    iVar11 = 0;
    do {
      iVar9 = 0;
      puVar10 = rgb_out;
      if (0 < iVar7) {
        do {
          sVar8 = (short)iVar9;
                    /* Unresolved local var: int index@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
          wsr((char)in_ACC,iVar11);
          iVar6 = (int)sVar8 * (int)(short)iVar6 + iVar11;
          sVar2 = v[iVar6];
          sVar3 = u[iVar6];
          *puVar10 = *(uint8_t *)
                      (iVar4 + (((sVar2 + -0x80) * iVar5 >> 0xc) + (uint)(ushort)y[iVar6] & 0x3ff));
          lVar13 = in_ACC + (longlong)w_h[2] * (longlong)sVar8;
          uVar12 = (undefined1)lVar13;
          iVar6 = rsr(uVar12);
          wsr(uVar12,iVar11);
          puVar10[1] = *(uint8_t *)
                        (iVar4 + ((uint)(ushort)y[iVar6] -
                                  ((sVar3 + -0x80) * 0x581 + (sVar2 + -0x80) * 0xb6d >> 0xc) & 0x3ff
                                 ));
          in_ACC = lVar13 + (longlong)w_h[2] * (longlong)sVar8;
          iVar6 = rsr((char)in_ACC);
          iVar9 = iVar9 + 1;
          puVar10[2] = *(uint8_t *)
                        (iVar4 + (((sVar3 + -0x80) * _DAT_fffd03fc >> 0xc) + (uint)(ushort)y[iVar6]
                                 & 0x3ff));
          iVar7 = w_h[1];
          iVar6 = (int)w_h[2];
          puVar10 = puVar10 + 3;
        } while (iVar9 < iVar7);
      }
      iVar11 = iVar11 + 1;
      rgb_out = rgb_out + sVar1 * -3;
    } while (iVar11 < iVar6);
  }
  return;
}

/* ==================================================================
 * yuv4442rgb565_270_8align_le_block
 * Purpose: Converts YUV444 samples to little-endian RGB565 with 270-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010450
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb565_270_8align_le_block
               (int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  byte bVar2;
  int iVar3;
  int16_t iVar4;
  short sVar5;
  int iVar6;
  int16_t *piVar7;
  ushort *puVar8;
  byte *pbVar9;
  int iVar10;
  int iVar11;
  uint uVar12;

  iVar6 = _DAT_fffd0474;
                    /* Unresolved local var: uint16_t * out@[???] */
  iVar10 = _DAT_fffd0470;
  sVar5 = w_h[1];
  iVar4 = w_h[2];
  iVar3 = (int)iVar4;
                    /* Unresolved local var: int r@[???]
                       Unresolved local var: int g@[???]
                       Unresolved local var: int b@[???]
                       Unresolved local var: int j@[???] */
  if (0 < sVar5) {
                    /* Unresolved local var: int i@[???] */
    if (0 < iVar3) {
      iVar11 = 0;
                    /* Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
      puVar8 = (ushort *)y;
      pbVar9 = (byte *)y;
      do {
        uVar12 = (uint)*puVar8;
        bVar1 = *(byte *)(iVar10 + (uVar12 - ((*u + -0x80) * 0x581 + (*v + -0x80) * 0xb6d >> 0xc) &
                                   0x3ff));
        bVar2 = *(byte *)(iVar10 + (uVar12 + ((*v + -0x80) * iVar6 >> 0xc) & 0x3ff));
        *pbVar9 = (byte)((int)(uint)*(byte *)(iVar10 + (uVar12 + ((*u + -0x80) * _DAT_fffd04c8 >>
                                                                 0xc) & 0x3ff)) >> 3) |
                  (bVar1 & 0x1c) << 3;
        pbVar9[1] = bVar2 & 0xf8 | (byte)((int)(uint)bVar1 >> 5);
        iVar11 = iVar11 + 1;
        puVar8 = puVar8 + iVar3;
        u = u + iVar3;
        v = v + iVar3;
        pbVar9 = pbVar9 + iVar3 * 2;
      } while (sVar5 != iVar11);
      iVar4 = w_h[2];
    }
                    /* Unresolved local var: int i@[???] */
    iVar3 = (int)iVar4;
  }
  if (0 < iVar3) {
    sVar5 = w_h[1];
    iVar10 = 0;
    do {
      iVar6 = 0;
      piVar7 = (int16_t *)rgb_out;
      if (0 < sVar5) {
        do {
          sVar5 = (short)iVar6;
                    /* Unresolved local var: int index@[???] */
          iVar6 = iVar6 + 1;
          *piVar7 = y[(int)sVar5 * (int)(short)iVar3 + iVar10];
          sVar5 = w_h[1];
          iVar3 = (int)w_h[2];
          piVar7 = piVar7 + 1;
        } while (iVar6 < sVar5);
      }
      iVar10 = iVar10 + 1;
      rgb_out = (uint8_t *)((int)rgb_out + -(int)*w_h * 2);
    } while (iVar10 < iVar3);
  }
  return;
}

/* ==================================================================
 * yuv4442rgb565_270_8align_be_block
 * Purpose: Converts YUV444 samples to big-endian RGB565 with 270-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010598
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb565_270_8align_be_block
               (int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  byte bVar2;
  int iVar3;
  int16_t iVar4;
  short sVar5;
  int iVar6;
  int16_t *piVar7;
  byte *pbVar8;
  ushort *puVar9;
  int iVar10;
  int iVar11;
  uint uVar12;

  iVar6 = _DAT_fffd05bc;
                    /* Unresolved local var: uint16_t * out@[???] */
  iVar10 = _DAT_fffd05b8;
  sVar5 = w_h[1];
  iVar4 = w_h[2];
  iVar3 = (int)iVar4;
                    /* Unresolved local var: int r@[???]
                       Unresolved local var: int g@[???]
                       Unresolved local var: int b@[???]
                       Unresolved local var: int j@[???] */
  if (0 < sVar5) {
                    /* Unresolved local var: int i@[???] */
    if (0 < iVar3) {
      iVar11 = 0;
                    /* Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
      pbVar8 = (byte *)y;
      puVar9 = (ushort *)y;
      do {
        uVar12 = (uint)*puVar9;
        bVar1 = *(byte *)(iVar10 + (uVar12 - ((*u + -0x80) * 0x581 + (*v + -0x80) * 0xb6d >> 0xc) &
                                   0x3ff));
        bVar2 = *(byte *)(iVar10 + (uVar12 + ((*u + -0x80) * iVar6 >> 0xc) & 0x3ff));
        *pbVar8 = *(byte *)(iVar10 + (uVar12 + ((*v + -0x80) * _DAT_fffd0608 >> 0xc) & 0x3ff)) &
                  0xf8 | (byte)((int)(uint)bVar1 >> 5);
        pbVar8[1] = (bVar1 & 0x1c) << 3 | (byte)((int)(uint)bVar2 >> 3);
        iVar11 = iVar11 + 1;
        puVar9 = puVar9 + iVar3;
        u = u + iVar3;
        v = v + iVar3;
        pbVar8 = pbVar8 + iVar3 * 2;
      } while (sVar5 != iVar11);
      iVar4 = w_h[2];
    }
                    /* Unresolved local var: int i@[???] */
    iVar3 = (int)iVar4;
  }
  if (0 < iVar3) {
    sVar5 = w_h[1];
    iVar10 = 0;
    do {
      iVar6 = 0;
      piVar7 = (int16_t *)rgb_out;
      if (0 < sVar5) {
        do {
          sVar5 = (short)iVar6;
                    /* Unresolved local var: int index@[???] */
          iVar6 = iVar6 + 1;
          *piVar7 = y[(int)sVar5 * (int)(short)iVar3 + iVar10];
          sVar5 = w_h[1];
          iVar3 = (int)w_h[2];
          piVar7 = piVar7 + 1;
        } while (iVar6 < sVar5);
      }
      iVar10 = iVar10 + 1;
      rgb_out = (uint8_t *)((int)rgb_out + -(int)*w_h * 2);
    } while (iVar10 < iVar3);
  }
  return;
}

/* ==================================================================
 * yuv444tuyvy_block
 * Purpose: Converts YUV444 samples to packed UYVY without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 000106e0
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv444tuyvy_block(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  ushort *puVar1;
  int iVar2;
  int iVar3;
  ushort *puVar4;
  ushort *puVar5;
  ushort uVar6;
  int iVar7;
  int iVar8;
  int iVar9;

                    /* Unresolved local var: int16_t * ybuf@[???]
                       Unresolved local var: int16_t * ubuf@[???]
                       Unresolved local var: int16_t * vbuf@[???]
                       Unresolved local var: int w_b@[???] */
  iVar2 = _DAT_fffd06f8;
                    /* Unresolved local var: int i@[???] */
  iVar9 = (int)w_h[1];
  if (0 < iVar9) {
    uVar6 = w_h[2];
    iVar8 = (int)(short)uVar6;
    iVar3 = *w_h - iVar8;
    while( true ) {
      iVar7 = 0;
      puVar4 = (ushort *)v;
      puVar5 = (ushort *)u;
      if (1 < iVar8) {
        do {
                    /* Unresolved local var: int index@[???] */
          iVar7 = iVar7 + 1;
          *rgb_out = (uint8_t)((int)((uint)*puVar5 + (uint)puVar5[1]) >> 1);
          puVar5 = puVar5 + 2;
          rgb_out[1] = *(uint8_t *)(iVar2 + ((ushort)*y & 0x3ff));
          puVar1 = puVar4 + 1;
          uVar6 = *puVar4;
          puVar4 = puVar4 + 2;
          rgb_out[2] = (uint8_t)((int)((uint)uVar6 + (uint)*puVar1) >> 1);
          puVar1 = (ushort *)(y + 1);
          y = y + 2;
          rgb_out[3] = *(uint8_t *)(iVar2 + (*puVar1 & 0x3ff));
          uVar6 = w_h[2];
          rgb_out = rgb_out + 4;
          iVar8 = (int)(short)uVar6;
        } while (iVar7 < (int)((uint)(uVar6 >> 0xf) + iVar8) >> 1);
      }
      iVar9 = iVar9 + -1;
      if (iVar9 == 0) break;
      u = u + iVar8;
      v = v + iVar8;
      rgb_out = rgb_out + iVar3 * 2;
      iVar8 = (int)(short)uVar6;
    }
  }
  return;
}

/* ==================================================================
 * yuv444tuyvy_180_block
 * Purpose: Converts YUV444 samples to packed UYVY with 180-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010770
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv444tuyvy_180_block(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  ushort *puVar1;
  int iVar2;
  uint8_t *puVar3;
  uint8_t *puVar4;
  ushort *puVar5;
  ushort *puVar6;
  ushort uVar7;
  int iVar8;
  int iVar9;
  int iVar10;

                    /* Unresolved local var: int16_t * ybuf@[???]
                       Unresolved local var: int16_t * ubuf@[???]
                       Unresolved local var: int16_t * vbuf@[???] */
  iVar2 = _DAT_fffd0780;
                    /* Unresolved local var: int i@[???] */
  iVar9 = (int)w_h[1];
  if (0 < iVar9) {
    uVar7 = w_h[2];
    iVar10 = (int)(short)uVar7;
    puVar3 = rgb_out + iVar10 * 2 + -4;
    while( true ) {
      iVar8 = 0;
      puVar4 = puVar3;
      puVar5 = (ushort *)v;
      puVar6 = (ushort *)u;
      if (1 < iVar10) {
        do {
                    /* Unresolved local var: int index@[???] */
          puVar3 = puVar4 + -4;
          *puVar4 = (uint8_t)((int)((uint)*puVar6 + (uint)puVar6[1]) >> 1);
          iVar8 = iVar8 + 1;
          puVar6 = puVar6 + 2;
          puVar4[3] = *(uint8_t *)(iVar2 + ((ushort)*y & 0x3ff));
          puVar1 = puVar5 + 1;
          uVar7 = *puVar5;
          puVar5 = puVar5 + 2;
          puVar4[2] = (uint8_t)((int)((uint)uVar7 + (uint)*puVar1) >> 1);
          puVar1 = (ushort *)(y + 1);
          y = y + 2;
          puVar4[1] = *(uint8_t *)(iVar2 + (*puVar1 & 0x3ff));
          uVar7 = w_h[2];
          iVar10 = (int)(short)uVar7;
          puVar4 = puVar3;
        } while (iVar8 < (int)((uint)(uVar7 >> 0xf) + iVar10) >> 1);
      }
      iVar9 = iVar9 + -1;
      if (iVar9 == 0) break;
      u = u + iVar10;
      v = v + iVar10;
      iVar10 = (int)(short)uVar7;
    }
  }
  return;
}

/* ==================================================================
 * yuv4442rgb565_8align_le_block
 * Purpose: Converts YUV444 samples to little-endian RGB565 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010800
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb565_8align_le_block(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  int iVar4;
  int iVar5;
  int iVar6;
  int iVar7;
  uint uVar8;

  iVar5 = _DAT_fffd0828;
  iVar4 = _DAT_fffd0824;
  sVar3 = w_h[1];
                    /* Unresolved local var: int r@[???]
                       Unresolved local var: int g@[???]
                       Unresolved local var: int b@[???]
                       Unresolved local var: int j@[???] */
                    /* Unresolved local var: int i@[???] */
  if ((0 < sVar3) && (iVar6 = (int)w_h[2], 0 < iVar6)) {
                    /* Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
    iVar7 = 0;
    do {
      uVar8 = (uint)(ushort)*y;
      bVar1 = *(byte *)(iVar4 + (uVar8 - ((*u + -0x80) * 0x581 + (*v + -0x80) * 0xb6d >> 0xc) &
                                0x3ff));
      bVar2 = *(byte *)(iVar4 + (uVar8 + ((*v + -0x80) * iVar4 >> 0xc) & 0x3ff));
      *rgb_out = (byte)((int)(uint)*(byte *)(iVar4 + (uVar8 + ((*u + -0x80) * iVar5 >> 0xc) & 0x3ff)
                                            ) >> 3) | (bVar1 & 0x1c) << 3;
      rgb_out[1] = bVar2 & 0xf8 | (byte)((int)(uint)bVar1 >> 5);
      iVar7 = iVar7 + 1;
      y = y + iVar6;
      u = u + iVar6;
      v = v + iVar6;
      rgb_out = rgb_out + iVar6 * 2;
    } while (sVar3 != iVar7);
  }
  return;
}

/* ==================================================================
 * yuv4442rgb565_8align_be_block
 * Purpose: Converts YUV444 samples to big-endian RGB565 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 000108f4
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb565_8align_be_block(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  int iVar4;
  int iVar5;
  int iVar6;
  int iVar7;
  uint uVar8;

  iVar5 = _DAT_fffd091c;
  iVar4 = _DAT_fffd0918;
  sVar3 = w_h[1];
                    /* Unresolved local var: int r@[???]
                       Unresolved local var: int g@[???]
                       Unresolved local var: int b@[???]
                       Unresolved local var: int j@[???] */
                    /* Unresolved local var: int i@[???] */
  if ((0 < sVar3) && (iVar6 = (int)w_h[2], 0 < iVar6)) {
                    /* Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
    iVar7 = 0;
    do {
      uVar8 = (uint)(ushort)*y;
      bVar1 = *(byte *)(iVar4 + (uVar8 - ((*u + -0x80) * 0x581 + (*v + -0x80) * 0xb6d >> 0xc) &
                                0x3ff));
      bVar2 = *(byte *)(iVar4 + (uVar8 + ((*u + -0x80) * iVar4 >> 0xc) & 0x3ff));
      *rgb_out = *(byte *)(iVar4 + (uVar8 + ((*v + -0x80) * iVar5 >> 0xc) & 0x3ff)) & 0xf8 |
                 (byte)((int)(uint)bVar1 >> 5);
      rgb_out[1] = (bVar1 & 0x1c) << 3 | (byte)((int)(uint)bVar2 >> 3);
      iVar7 = iVar7 + 1;
      y = y + iVar6;
      u = u + iVar6;
      v = v + iVar6;
      rgb_out = rgb_out + iVar6 * 2;
    } while (sVar3 != iVar7);
  }
  return;
}

/* ==================================================================
 * yuv4442rgb565_180_8align_be_block
 * Purpose: Converts YUV444 samples to big-endian RGB565 with 180-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 000109e8
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb565_180_8align_be_block
               (int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  byte bVar2;
  int iVar3;
  uint uVar4;
  int16_t iVar5;
  ushort *puVar6;
  int iVar7;
  int iVar8;
  int iVar9;
  byte *pbVar10;
  int iVar11;

  iVar3 = _DAT_fffd0a14;
  iVar11 = _DAT_fffd0a10;
  iVar5 = w_h[1];
  iVar8 = (int)iVar5;
                    /* Unresolved local var: int r@[???]
                       Unresolved local var: int g@[???]
                       Unresolved local var: int b@[???]
                       Unresolved local var: int j@[???] */
  if (0 < iVar8) {
    iVar9 = (int)w_h[2];
                    /* Unresolved local var: int i@[???] */
    if (0 < iVar9) {
                    /* Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
      iVar7 = 0;
      puVar6 = (ushort *)y;
      pbVar10 = (byte *)y;
      do {
        uVar4 = (uint)*puVar6;
        bVar1 = *(byte *)(iVar11 + (uVar4 - ((*u + -0x80) * 0x581 + (*v + -0x80) * 0xb6d >> 0xc) &
                                   0x3ff));
        bVar2 = *(byte *)(iVar11 + (uVar4 + ((*u + -0x80) * iVar3 >> 0xc) & 0x3ff));
        *pbVar10 = *(byte *)(iVar11 + (uVar4 + ((*v + -0x80) * iVar3 >> 0xc) & 0x3ff)) & 0xf8 |
                   (byte)((int)(uint)bVar1 >> 5);
        pbVar10[1] = (bVar1 & 0x1c) << 3 | (byte)((int)(uint)bVar2 >> 3);
        iVar7 = iVar7 + 1;
        puVar6 = puVar6 + iVar9;
        u = u + iVar9;
        v = v + iVar9;
        pbVar10 = pbVar10 + iVar9 * 2;
      } while (iVar8 != iVar7);
      iVar5 = w_h[1];
    }
                    /* Unresolved local var: size_t i@[???] */
    iVar8 = (int)iVar5;
  }
  if (iVar8 != 0) {
    iVar11 = (int)w_h[2];
    uVar4 = 0;
    do {
      (*_DAT_fffd0b08)(rgb_out,y,iVar11 * 2);
      iVar11 = (int)w_h[2];
      uVar4 = uVar4 + 1;
      rgb_out = rgb_out + iVar11 * -2;
      y = y + iVar11;
    } while (uVar4 < (uint)(int)w_h[1]);
  }
  return;
}

/* ==================================================================
 * yuv4442rgb565_180_8align_le_block
 * Purpose: Converts YUV444 samples to little-endian RGB565 with 180-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010b20
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb565_180_8align_le_block
               (int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  byte bVar2;
  int iVar3;
  uint uVar4;
  int16_t iVar5;
  ushort *puVar6;
  int iVar7;
  int iVar8;
  int iVar9;
  byte *pbVar10;
  int iVar11;

  iVar3 = _DAT_fffd0b4c;
  iVar11 = _DAT_fffd0b48;
  iVar5 = w_h[1];
  iVar8 = (int)iVar5;
                    /* Unresolved local var: int r@[???]
                       Unresolved local var: int g@[???]
                       Unresolved local var: int b@[???]
                       Unresolved local var: int j@[???] */
  if (0 < iVar8) {
    iVar9 = (int)w_h[2];
                    /* Unresolved local var: int i@[???] */
    if (0 < iVar9) {
                    /* Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
      iVar7 = 0;
      puVar6 = (ushort *)y;
      pbVar10 = (byte *)y;
      do {
        uVar4 = (uint)*puVar6;
        bVar1 = *(byte *)(iVar11 + (uVar4 - ((*u + -0x80) * 0x581 + (*v + -0x80) * 0xb6d >> 0xc) &
                                   0x3ff));
        bVar2 = *(byte *)(iVar11 + (uVar4 + ((*v + -0x80) * iVar3 >> 0xc) & 0x3ff));
        *pbVar10 = (byte)((int)(uint)*(byte *)(iVar11 + (uVar4 + ((*u + -0x80) * iVar3 >> 0xc) &
                                                        0x3ff)) >> 3) | (bVar1 & 0x1c) << 3;
        pbVar10[1] = bVar2 & 0xf8 | (byte)((int)(uint)bVar1 >> 5);
        iVar7 = iVar7 + 1;
        puVar6 = puVar6 + iVar9;
        u = u + iVar9;
        v = v + iVar9;
        pbVar10 = pbVar10 + iVar9 * 2;
      } while (iVar8 != iVar7);
      iVar5 = w_h[1];
    }
                    /* Unresolved local var: size_t i@[???] */
    iVar8 = (int)iVar5;
  }
  if (iVar8 != 0) {
    iVar11 = (int)w_h[2];
    uVar4 = 0;
    do {
      (*_DAT_fffd0c40)(rgb_out,y,iVar11 * 2);
      iVar11 = (int)w_h[2];
      uVar4 = uVar4 + 1;
      rgb_out = rgb_out + iVar11 * -2;
      y = y + iVar11;
    } while (uVar4 < (uint)(int)w_h[1]);
  }
  return;
}

/* ==================================================================
 * yuv4442rgb565_90_8align_le_block
 * Purpose: Converts YUV444 samples to little-endian RGB565 with 90-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010c58
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb565_90_8align_le_block
               (int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  byte bVar2;
  int iVar3;
  int16_t iVar4;
  short sVar5;
  int iVar6;
  int16_t *piVar7;
  ushort *puVar8;
  byte *pbVar9;
  int iVar10;
  int iVar11;
  uint uVar12;

  iVar6 = _DAT_fffd0c7c;
                    /* Unresolved local var: uint16_t * out@[???] */
  iVar10 = _DAT_fffd0c78;
  sVar5 = w_h[1];
  iVar4 = w_h[2];
  iVar3 = (int)iVar4;
                    /* Unresolved local var: int r@[???]
                       Unresolved local var: int g@[???]
                       Unresolved local var: int b@[???]
                       Unresolved local var: int j@[???] */
  if (0 < sVar5) {
                    /* Unresolved local var: int i@[???] */
    if (0 < iVar3) {
      iVar11 = 0;
                    /* Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
      puVar8 = (ushort *)y;
      pbVar9 = (byte *)y;
      do {
        uVar12 = (uint)*puVar8;
        bVar1 = *(byte *)(iVar10 + (uVar12 - ((*u + -0x80) * 0x581 + (*v + -0x80) * 0xb6d >> 0xc) &
                                   0x3ff));
        bVar2 = *(byte *)(iVar10 + (uVar12 + ((*v + -0x80) * iVar6 >> 0xc) & 0x3ff));
        *pbVar9 = (byte)((int)(uint)*(byte *)(iVar10 + (uVar12 + ((*u + -0x80) * _DAT_fffd0cd0 >>
                                                                 0xc) & 0x3ff)) >> 3) |
                  (bVar1 & 0x1c) << 3;
        pbVar9[1] = bVar2 & 0xf8 | (byte)((int)(uint)bVar1 >> 5);
        iVar11 = iVar11 + 1;
        puVar8 = puVar8 + iVar3;
        u = u + iVar3;
        v = v + iVar3;
        pbVar9 = pbVar9 + iVar3 * 2;
      } while (sVar5 != iVar11);
      iVar4 = w_h[2];
    }
                    /* Unresolved local var: int i@[???] */
    iVar3 = (int)iVar4;
  }
  if (0 < iVar3) {
    sVar5 = w_h[1];
    iVar10 = 0;
    do {
      iVar6 = 0;
      piVar7 = (int16_t *)rgb_out;
      if (0 < sVar5) {
        do {
          sVar5 = (short)iVar6;
                    /* Unresolved local var: int index@[???] */
          iVar6 = iVar6 + 1;
          *piVar7 = y[(int)sVar5 * (int)(short)iVar3 + iVar10];
          sVar5 = w_h[1];
          iVar3 = (int)w_h[2];
          piVar7 = piVar7 + -1;
        } while (iVar6 < sVar5);
      }
      iVar10 = iVar10 + 1;
      rgb_out = (uint8_t *)((int)rgb_out + *w_h * 2);
    } while (iVar10 < iVar3);
  }
  return;
}

/* ==================================================================
 * yuv4442rgb565_90_8align_be_block
 * Purpose: Converts YUV444 samples to big-endian RGB565 with 90-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010da0
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv4442rgb565_90_8align_be_block
               (int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  byte bVar2;
  int iVar3;
  int16_t iVar4;
  short sVar5;
  int iVar6;
  int16_t *piVar7;
  byte *pbVar8;
  ushort *puVar9;
  int iVar10;
  int iVar11;
  uint uVar12;

  iVar6 = _DAT_fffd0dc4;
                    /* Unresolved local var: uint16_t * out@[???] */
  iVar10 = _DAT_fffd0dc0;
  sVar5 = w_h[1];
  iVar4 = w_h[2];
  iVar3 = (int)iVar4;
                    /* Unresolved local var: int r@[???]
                       Unresolved local var: int g@[???]
                       Unresolved local var: int b@[???]
                       Unresolved local var: int j@[???] */
  if (0 < sVar5) {
                    /* Unresolved local var: int i@[???] */
    if (0 < iVar3) {
      iVar11 = 0;
                    /* Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
      pbVar8 = (byte *)y;
      puVar9 = (ushort *)y;
      do {
        uVar12 = (uint)*puVar9;
        bVar1 = *(byte *)(iVar10 + (uVar12 - ((*u + -0x80) * 0x581 + (*v + -0x80) * 0xb6d >> 0xc) &
                                   0x3ff));
        bVar2 = *(byte *)(iVar10 + (uVar12 + ((*u + -0x80) * iVar6 >> 0xc) & 0x3ff));
        *pbVar8 = *(byte *)(iVar10 + (uVar12 + ((*v + -0x80) * _DAT_fffd0e10 >> 0xc) & 0x3ff)) &
                  0xf8 | (byte)((int)(uint)bVar1 >> 5);
        pbVar8[1] = (bVar1 & 0x1c) << 3 | (byte)((int)(uint)bVar2 >> 3);
        iVar11 = iVar11 + 1;
        puVar9 = puVar9 + iVar3;
        u = u + iVar3;
        v = v + iVar3;
        pbVar8 = pbVar8 + iVar3 * 2;
      } while (sVar5 != iVar11);
      iVar4 = w_h[2];
    }
                    /* Unresolved local var: int i@[???] */
    iVar3 = (int)iVar4;
  }
  if (0 < iVar3) {
    sVar5 = w_h[1];
    iVar10 = 0;
    do {
      iVar6 = 0;
      piVar7 = (int16_t *)rgb_out;
      if (0 < sVar5) {
        do {
          sVar5 = (short)iVar6;
                    /* Unresolved local var: int index@[???] */
          iVar6 = iVar6 + 1;
          *piVar7 = y[(int)sVar5 * (int)(short)iVar3 + iVar10];
          sVar5 = w_h[1];
          iVar3 = (int)w_h[2];
          piVar7 = piVar7 + -1;
        } while (iVar6 < sVar5);
      }
      iVar10 = iVar10 + 1;
      rgb_out = (uint8_t *)((int)rgb_out + *w_h * 2);
    } while (iVar10 < iVar3);
  }
  return;
}

/* ==================================================================
 * yuv444tuyvy_270_block
 * Purpose: Converts YUV444 samples to packed UYVY with 270-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010ee8
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv444tuyvy_270_block(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  int16_t iVar1;
  ushort uVar2;
  int16_t *piVar3;
  int iVar4;
  short sVar5;
  ushort *puVar6;
  ushort *puVar7;
  ushort *puVar8;
  int iVar9;
  int iVar10;
  int16_t *piVar11;
  ushort *puVar12;

                    /* Unresolved local var: int loop_count@[???]
                       Unresolved local var: uint16_t * out@[???] */
  iVar4 = _DAT_fffd0f00;
  iVar9 = (int)w_h[2];
                    /* Unresolved local var: int8_t * out_buf1@[???]
                       Unresolved local var: int16_t * y_1@[???]
                       Unresolved local var: int index@[???] */
  if (0 < iVar9) {
    *(undefined1 *)((int)y + 1) = *(undefined1 *)(_DAT_fffd0f00 + ((ushort)*y & 0x3ff));
    *(char *)y = (char)*u;
    *(undefined1 *)((int)(y + iVar9) + 1) = *(undefined1 *)(iVar4 + ((ushort)y[iVar9] & 0x3ff));
    *(char *)(y + iVar9) = (char)*v;
    iVar10 = (int)w_h[2];
    puVar6 = (ushort *)(y + iVar10 * 2);
                    /* Unresolved local var: int8_t * out_buf1@[???]
                       Unresolved local var: int16_t * y_1@[???] */
    puVar8 = puVar6 + iVar10;
    piVar11 = u + iVar10 * 2;
                    /* Unresolved local var: int index@[???] */
    piVar3 = v + iVar10 * 2;
    puVar7 = puVar8 + iVar9;
    puVar12 = puVar8;
    do {
      *(undefined1 *)((int)puVar6 + 1) = *(undefined1 *)(iVar4 + (*puVar6 & 0x3ff));
      iVar1 = *piVar11;
      piVar11 = piVar11 + 1;
      *(char *)puVar6 = (char)iVar1;
      uVar2 = *puVar12;
      puVar6 = puVar6 + 1;
      puVar12 = puVar6 + iVar10;
      *(undefined1 *)((int)puVar8 + 1) = *(undefined1 *)(iVar4 + (uVar2 & 0x3ff));
      iVar1 = *piVar3;
      piVar3 = piVar3 + 1;
      *(char *)puVar8 = (char)iVar1;
      puVar8 = puVar8 + 1;
    } while (puVar7 != puVar8);
    iVar10 = (int)w_h[2];
    puVar6 = (ushort *)(y + iVar10 * 4);
                    /* Unresolved local var: int8_t * out_buf1@[???]
                       Unresolved local var: int16_t * y_1@[???] */
    puVar8 = puVar6 + iVar10;
    piVar11 = u + iVar10 * 4;
                    /* Unresolved local var: int index@[???] */
    piVar3 = v + iVar10 * 4;
    puVar7 = puVar8 + iVar9;
    puVar12 = puVar8;
    do {
      *(undefined1 *)((int)puVar6 + 1) = *(undefined1 *)(iVar4 + (*puVar6 & 0x3ff));
      iVar1 = *piVar11;
      piVar11 = piVar11 + 1;
      *(char *)puVar6 = (char)iVar1;
      uVar2 = *puVar12;
      puVar6 = puVar6 + 1;
      puVar12 = puVar6 + iVar10;
      *(undefined1 *)((int)puVar8 + 1) = *(undefined1 *)(iVar4 + (uVar2 & 0x3ff));
      iVar1 = *piVar3;
      piVar3 = piVar3 + 1;
      *(char *)puVar8 = (char)iVar1;
      puVar8 = puVar8 + 1;
    } while (puVar8 != puVar7);
    iVar10 = (int)w_h[2];
    puVar7 = (ushort *)(y + iVar10 * 6);
                    /* Unresolved local var: int8_t * out_buf1@[???]
                       Unresolved local var: int16_t * y_1@[???] */
    puVar6 = puVar7 + iVar10;
    piVar3 = u + iVar10 * 6;
                    /* Unresolved local var: int index@[???] */
    piVar11 = v + iVar10 * 6;
    puVar8 = puVar6 + iVar9;
    puVar12 = puVar6;
    do {
      *(undefined1 *)((int)puVar7 + 1) = *(undefined1 *)(iVar4 + (*puVar7 & 0x3ff));
      iVar1 = *piVar3;
      piVar3 = piVar3 + 1;
      *(char *)puVar7 = (char)iVar1;
      uVar2 = *puVar12;
      puVar7 = puVar7 + 1;
      puVar12 = puVar7 + iVar10;
      *(undefined1 *)((int)puVar6 + 1) = *(undefined1 *)(iVar4 + (uVar2 & 0x3ff));
      iVar1 = *piVar11;
      piVar11 = piVar11 + 1;
      *(char *)puVar6 = (char)iVar1;
      puVar6 = puVar6 + 1;
    } while (puVar6 != puVar8);
                    /* Unresolved local var: int i@[???] */
    iVar4 = (int)w_h[2];
    if (0 < iVar4) {
      sVar5 = w_h[1];
      iVar9 = 0;
      do {
        iVar10 = 0;
        piVar3 = (int16_t *)rgb_out;
        if (0 < sVar5) {
          do {
            sVar5 = (short)iVar10;
                    /* Unresolved local var: int index@[???] */
            iVar10 = iVar10 + 1;
            *piVar3 = y[(int)sVar5 * (int)(short)iVar4 + iVar9];
            sVar5 = w_h[1];
            iVar4 = (int)w_h[2];
            piVar3 = piVar3 + 1;
          } while (iVar10 < sVar5);
        }
        iVar9 = iVar9 + 1;
        rgb_out = (uint8_t *)((int)rgb_out + -(int)*w_h * 2);
      } while (iVar9 < iVar4);
    }
  }
  return;
}

/* ==================================================================
 * yuv444tuyvy_90_block
 * Purpose: Converts YUV444 samples to packed UYVY with 90-degree rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 0001107c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv444tuyvy_90_block(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  int16_t iVar1;
  ushort uVar2;
  int16_t *piVar3;
  int iVar4;
  short sVar5;
  ushort *puVar6;
  ushort *puVar7;
  ushort *puVar8;
  int iVar9;
  int iVar10;
  int16_t *piVar11;
  ushort *puVar12;

                    /* Unresolved local var: int loop_count@[???]
                       Unresolved local var: uint16_t * out@[???] */
  iVar4 = _DAT_fffd1094;
  iVar9 = (int)w_h[2];
                    /* Unresolved local var: int8_t * out_buf1@[???]
                       Unresolved local var: int16_t * y_1@[???]
                       Unresolved local var: int index@[???] */
  if (0 < iVar9) {
    *(undefined1 *)((int)y + 1) = *(undefined1 *)(_DAT_fffd1094 + ((ushort)*y & 0x3ff));
    *(char *)y = (char)*v;
    *(undefined1 *)((int)(y + iVar9) + 1) = *(undefined1 *)(iVar4 + ((ushort)y[iVar9] & 0x3ff));
    *(char *)(y + iVar9) = (char)*u;
    iVar10 = (int)w_h[2];
    puVar6 = (ushort *)(y + iVar10 * 2);
                    /* Unresolved local var: int8_t * out_buf1@[???]
                       Unresolved local var: int16_t * y_1@[???] */
    puVar8 = puVar6 + iVar10;
    piVar11 = v + iVar10 * 2;
                    /* Unresolved local var: int index@[???] */
    piVar3 = u + iVar10 * 2;
    puVar7 = puVar8 + iVar9;
    puVar12 = puVar8;
    do {
      *(undefined1 *)((int)puVar6 + 1) = *(undefined1 *)(iVar4 + (*puVar6 & 0x3ff));
      iVar1 = *piVar11;
      piVar11 = piVar11 + 1;
      *(char *)puVar6 = (char)iVar1;
      uVar2 = *puVar12;
      puVar6 = puVar6 + 1;
      puVar12 = puVar6 + iVar10;
      *(undefined1 *)((int)puVar8 + 1) = *(undefined1 *)(iVar4 + (uVar2 & 0x3ff));
      iVar1 = *piVar3;
      piVar3 = piVar3 + 1;
      *(char *)puVar8 = (char)iVar1;
      puVar8 = puVar8 + 1;
    } while (puVar7 != puVar8);
    iVar10 = (int)w_h[2];
    puVar6 = (ushort *)(y + iVar10 * 4);
                    /* Unresolved local var: int8_t * out_buf1@[???]
                       Unresolved local var: int16_t * y_1@[???] */
    puVar8 = puVar6 + iVar10;
    piVar11 = v + iVar10 * 4;
                    /* Unresolved local var: int index@[???] */
    piVar3 = u + iVar10 * 4;
    puVar7 = puVar8 + iVar9;
    puVar12 = puVar8;
    do {
      *(undefined1 *)((int)puVar6 + 1) = *(undefined1 *)(iVar4 + (*puVar6 & 0x3ff));
      iVar1 = *piVar11;
      piVar11 = piVar11 + 1;
      *(char *)puVar6 = (char)iVar1;
      uVar2 = *puVar12;
      puVar6 = puVar6 + 1;
      puVar12 = puVar6 + iVar10;
      *(undefined1 *)((int)puVar8 + 1) = *(undefined1 *)(iVar4 + (uVar2 & 0x3ff));
      iVar1 = *piVar3;
      piVar3 = piVar3 + 1;
      *(char *)puVar8 = (char)iVar1;
      puVar8 = puVar8 + 1;
    } while (puVar8 != puVar7);
    iVar10 = (int)w_h[2];
    puVar7 = (ushort *)(y + iVar10 * 6);
                    /* Unresolved local var: int8_t * out_buf1@[???]
                       Unresolved local var: int16_t * y_1@[???] */
    puVar6 = puVar7 + iVar10;
    piVar3 = v + iVar10 * 6;
                    /* Unresolved local var: int index@[???] */
    piVar11 = u + iVar10 * 6;
    puVar8 = puVar6 + iVar9;
    puVar12 = puVar6;
    do {
      *(undefined1 *)((int)puVar7 + 1) = *(undefined1 *)(iVar4 + (*puVar7 & 0x3ff));
      iVar1 = *piVar3;
      piVar3 = piVar3 + 1;
      *(char *)puVar7 = (char)iVar1;
      uVar2 = *puVar12;
      puVar7 = puVar7 + 1;
      puVar12 = puVar7 + iVar10;
      *(undefined1 *)((int)puVar6 + 1) = *(undefined1 *)(iVar4 + (uVar2 & 0x3ff));
      iVar1 = *piVar11;
      piVar11 = piVar11 + 1;
      *(char *)puVar6 = (char)iVar1;
      puVar6 = puVar6 + 1;
    } while (puVar6 != puVar8);
                    /* Unresolved local var: int i@[???] */
    iVar4 = (int)w_h[2];
    if (0 < iVar4) {
      sVar5 = w_h[1];
      iVar9 = 0;
      do {
        iVar10 = 0;
        piVar3 = (int16_t *)rgb_out;
        if (0 < sVar5) {
          do {
            sVar5 = (short)iVar10;
                    /* Unresolved local var: int index@[???] */
            iVar10 = iVar10 + 1;
            *piVar3 = y[(int)sVar5 * (int)(short)iVar4 + iVar9];
            sVar5 = w_h[1];
            iVar4 = (int)w_h[2];
            piVar3 = piVar3 + -1;
          } while (iVar10 < sVar5);
        }
        iVar9 = iVar9 + 1;
        rgb_out = (uint8_t *)((int)rgb_out + *w_h * 2);
      } while (iVar9 < iVar4);
    }
  }
  return;
}
