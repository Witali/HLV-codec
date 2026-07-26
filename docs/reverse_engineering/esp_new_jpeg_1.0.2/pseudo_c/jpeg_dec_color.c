/* ESP_NEW_JPEG 1.0.2 ESP32 decoder Ghidra pseudo-C.
 * Derived from the Espressif binary; see LICENSE.ESPRESSIF in the analysis directory.
 * Program: jpeg_dec_color.c.obj
 * Language: Xtensa:LE:32:default
 * Types and names are reconstructed and must not be treated as the original source.
 */

/* ==================================================================
 * y_to_rgb888
 * Purpose: Expands an 8x8 grayscale luma block to RGB888 by copying each clipped Y sample into the red, green and blue channels.
 * Entry: 00010088
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void y_to_rgb888(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  uint8_t uVar1;
  ulonglong uVar2;
  int iVar3;
  uint uVar4;
  uint8_t *puVar5;
  uint uVar6;
  short sVar7;
  uint uVar8;
  uint8_t *puVar9;
  uint8_t *puVar10;
  short sVar11;
  int iVar12;
  int iVar13;
  uint uVar14;
  int16_t *piVar15;

                    /* Unresolved local var: uint8_t * rgb_out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???] */
  uVar4 = _DAT_fffd00ac;
                    /* Unresolved local var: size_t j@[???] */
  sVar7 = w_h[3];
  if (sVar7 != 0) {
    sVar11 = w_h[2];
    iVar12 = (int)sVar11;
    puVar5 = rgb_out + *w_h * 3;
    iVar3 = *w_h * 6;
    piVar15 = y + iVar12;
    uVar6 = 0;
    iVar13 = iVar12;
    while( true ) {
      uVar14 = 0;
      puVar9 = puVar5;
      puVar10 = rgb_out;
      if (iVar13 != 0) {
        do {
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int index@[???] */
          uVar2 = (ulonglong)uVar14;
          uVar14 = uVar14 + 6;
          uVar8 = (uint)(uVar2 * uVar4 >> 0x21);
          uVar1 = *(uint8_t *)(uVar4 + ((ushort)y[uVar8] & 0x3ff));
          *puVar10 = uVar1;
          puVar10[1] = uVar1;
          puVar10[2] = uVar1;
          uVar1 = *(uint8_t *)(uVar4 + ((ushort)y[uVar8 + 1] & 0x3ff));
          puVar10[3] = uVar1;
          puVar10[4] = uVar1;
          puVar10[5] = uVar1;
          puVar10 = puVar10 + 6;
          uVar1 = *(uint8_t *)(uVar4 + ((ushort)piVar15[uVar8] & 0x3ff));
          *puVar9 = uVar1;
          puVar9[1] = uVar1;
          puVar9[2] = uVar1;
          uVar1 = *(uint8_t *)(uVar4 + ((ushort)piVar15[uVar8 + 1] & 0x3ff));
          puVar9[3] = uVar1;
          puVar9[4] = uVar1;
          puVar9[5] = uVar1;
          sVar11 = w_h[2];
          puVar9 = puVar9 + 6;
        } while (uVar14 < (uint)(sVar11 * 3));
        sVar7 = w_h[3];
      }
      uVar6 = uVar6 + 2;
      if ((uint)(int)sVar7 <= uVar6) break;
      iVar13 = (int)sVar11;
      rgb_out = rgb_out + iVar3;
      puVar5 = puVar5 + iVar3;
      y = y + iVar12 * 2;
      piVar15 = piVar15 + iVar12 * 2;
    }
  }
  return;
}

/* ==================================================================
 * y_to_rgb565le
 * Purpose: Converts an 8x8 grayscale luma block to little-endian RGB565 by clipping Y and replicating its high bits into R, G and B.
 * Entry: 0001015c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void y_to_rgb565le(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  ushort *puVar2;
  int iVar3;
  ushort *puVar4;
  uint uVar5;
  uint uVar6;
  short sVar7;
  short sVar8;
  ushort *puVar9;
  int iVar10;
  ushort *puVar11;
  int iVar12;
  int iVar13;
  ushort *puVar14;
  ushort *puVar15;
  ushort *local_40;

                    /* Unresolved local var: uint8_t r@[???]
                       Unresolved local var: uint8_t g@[???]
                       Unresolved local var: uint8_t b@[???]
                       Unresolved local var: uint16_t * out@[???]
                       Unresolved local var: uint16_t * out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???] */
  iVar3 = _DAT_fffd0184;
                    /* Unresolved local var: size_t j@[???] */
  sVar8 = w_h[3];
  if (sVar8 != 0) {
    iVar13 = (int)*w_h;
    sVar7 = w_h[2];
    puVar4 = (ushort *)(rgb_out + iVar13 * 2);
    iVar10 = (int)sVar7;
    local_40 = (ushort *)(y + iVar10);
    uVar6 = 0;
    iVar12 = iVar10;
                    /* Unresolved local var: size_t i@[???] */
    while( true ) {
      if (iVar12 != 0) {
        uVar5 = 0;
        puVar9 = puVar4;
        puVar11 = local_40;
        puVar14 = (ushort *)rgb_out;
        puVar15 = (ushort *)y;
        do {
          uVar5 = uVar5 + 2;
          bVar1 = *(byte *)(iVar3 + (*puVar15 & 0x3ff));
          *puVar14 = (ushort)(bVar1 >> 3) | (bVar1 & 0xf8) << 8 | (bVar1 & 0xfc) << 3;
          puVar2 = puVar15 + 1;
          puVar15 = puVar15 + 2;
          bVar1 = *(byte *)(iVar3 + (*puVar2 & 0x3ff));
          puVar14[1] = (ushort)(bVar1 >> 3) | (bVar1 & 0xf8) << 8 | (bVar1 & 0xfc) << 3;
          puVar14 = puVar14 + 2;
          bVar1 = *(byte *)(iVar3 + (*puVar11 & 0x3ff));
          *puVar9 = (ushort)(bVar1 >> 3) | (bVar1 & 0xf8) << 8 | (bVar1 & 0xfc) << 3;
          puVar2 = puVar11 + 1;
          puVar11 = puVar11 + 2;
          bVar1 = *(byte *)(iVar3 + (*puVar2 & 0x3ff));
          puVar9[1] = (ushort)(bVar1 >> 3) | (bVar1 & 0xf8) << 8 | (bVar1 & 0xfc) << 3;
          sVar7 = w_h[2];
          puVar9 = puVar9 + 2;
        } while (uVar5 < (uint)(int)sVar7);
        sVar8 = w_h[3];
      }
      uVar6 = uVar6 + 2;
      if ((uint)(int)sVar8 <= uVar6) break;
      rgb_out = (uint8_t *)((int)rgb_out + iVar13 * 2 * 2);
      puVar4 = puVar4 + iVar13 * 2;
      y = y + iVar10 * 2;
      local_40 = local_40 + iVar10 * 2;
      iVar12 = (int)sVar7;
    }
  }
  return;
}

/* ==================================================================
 * y_to_rgb565be
 * Purpose: Converts an 8x8 grayscale luma block to big-endian RGB565 by clipping Y and replicating its high bits into R, G and B.
 * Entry: 00010274
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void y_to_rgb565be(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  ushort *puVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  short sVar6;
  ushort *puVar7;
  int iVar8;
  short sVar9;
  ushort *puVar10;
  int iVar11;
  ushort *puVar12;
  int iVar13;
  ushort *puVar14;
  ushort *puVar15;
  ushort *local_40;

                    /* Unresolved local var: uint8_t r@[???]
                       Unresolved local var: uint8_t g@[???]
                       Unresolved local var: uint8_t b@[???]
                       Unresolved local var: uint16_t * out@[???]
                       Unresolved local var: uint16_t * out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???] */
  iVar3 = _DAT_fffd029c;
                    /* Unresolved local var: size_t j@[???] */
  sVar9 = w_h[3];
  if (sVar9 != 0) {
    sVar6 = w_h[2];
    iVar8 = (int)*w_h;
    iVar11 = (int)sVar6;
    local_40 = (ushort *)(y + iVar11);
    puVar7 = (ushort *)(rgb_out + iVar8 * 2);
    uVar4 = 0;
    iVar13 = iVar11;
                    /* Unresolved local var: size_t i@[???] */
    while( true ) {
      if (iVar13 != 0) {
        uVar5 = 0;
        puVar10 = puVar7;
        puVar12 = local_40;
        puVar14 = (ushort *)rgb_out;
        puVar15 = (ushort *)y;
        do {
          uVar5 = uVar5 + 2;
          bVar1 = *(byte *)(iVar3 + (*puVar15 & 0x3ff));
          *puVar14 = (ushort)(bVar1 >> 5) | bVar1 & 0xfff8 |
                     (ushort)(bVar1 >> 2) << 0xd | (ushort)(bVar1 >> 3) << 8;
          puVar2 = puVar15 + 1;
          puVar15 = puVar15 + 2;
          bVar1 = *(byte *)(iVar3 + (*puVar2 & 0x3ff));
          puVar14[1] = (ushort)(bVar1 >> 5) | bVar1 & 0xfff8 |
                       (ushort)(bVar1 >> 2) << 0xd | (ushort)(bVar1 >> 3) << 8;
          puVar14 = puVar14 + 2;
          bVar1 = *(byte *)(iVar3 + (*puVar12 & 0x3ff));
          *puVar10 = (ushort)(bVar1 >> 5) | bVar1 & 0xfff8 |
                     (ushort)(bVar1 >> 2) << 0xd | (ushort)(bVar1 >> 3) << 8;
          puVar2 = puVar12 + 1;
          puVar12 = puVar12 + 2;
          bVar1 = *(byte *)(iVar3 + (*puVar2 & 0x3ff));
          puVar10[1] = (ushort)(bVar1 >> 5) | bVar1 & 0xfff8 |
                       (ushort)(bVar1 >> 2) << 0xd | (ushort)(bVar1 >> 3) << 8;
          sVar6 = w_h[2];
          puVar10 = puVar10 + 2;
        } while (uVar5 < (uint)(int)sVar6);
        sVar9 = w_h[3];
      }
      uVar4 = uVar4 + 2;
      if ((uint)(int)sVar9 <= uVar4) break;
      y = y + iVar11 * 2;
      local_40 = local_40 + iVar11 * 2;
      rgb_out = (uint8_t *)((int)rgb_out + iVar8 * 2 * 2);
      puVar7 = puVar7 + iVar8 * 2;
      iVar13 = (int)sVar6;
    }
  }
  return;
}

/* ==================================================================
 * y_to_uyvy
 * Purpose: Packs an 8x8 grayscale luma block as UYVY, inserting neutral chroma values around pairs of clipped Y samples.
 * Entry: 000103b8
 * ================================================================== */

void y_to_uyvy(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  int16_t iVar1;
  uint8_t *puVar2;
  int16_t *piVar3;
  uint uVar4;
  int iVar5;
  int iVar6;

                    /* Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: uint8_t * out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???] */
                    /* Unresolved local var: size_t j@[???] */
  if (w_h[3] != 0) {
    iVar5 = (int)w_h[2];
    puVar2 = rgb_out + *w_h * 2;
    piVar3 = y + iVar5;
    iVar6 = *w_h * 4;
    uVar4 = 0;
    do {
      *rgb_out = 0x80;
      iVar1 = *y;
      rgb_out[2] = 0x80;
      rgb_out[1] = (uint8_t)iVar1;
      iVar1 = y[1];
      rgb_out[4] = 0x80;
      rgb_out[3] = (uint8_t)iVar1;
      iVar1 = y[2];
      rgb_out[6] = 0x80;
      rgb_out[5] = (uint8_t)iVar1;
      iVar1 = y[3];
      rgb_out[8] = 0x80;
      rgb_out[7] = (uint8_t)iVar1;
      iVar1 = y[4];
      rgb_out[10] = 0x80;
      rgb_out[9] = (uint8_t)iVar1;
      iVar1 = y[5];
      rgb_out[0xc] = 0x80;
      rgb_out[0xb] = (uint8_t)iVar1;
      iVar1 = y[6];
      rgb_out[0xe] = 0x80;
      rgb_out[0xd] = (uint8_t)iVar1;
      uVar4 = uVar4 + 2;
      rgb_out[0xf] = (uint8_t)y[7];
      *puVar2 = 0x80;
      iVar1 = *piVar3;
      puVar2[2] = 0x80;
      puVar2[1] = (uint8_t)iVar1;
      iVar1 = piVar3[1];
      puVar2[4] = 0x80;
      puVar2[3] = (uint8_t)iVar1;
      rgb_out = rgb_out + iVar6;
      puVar2[5] = (uint8_t)piVar3[2];
      puVar2[6] = 0x80;
      iVar1 = piVar3[3];
      puVar2[8] = 0x80;
      puVar2[7] = (uint8_t)iVar1;
      iVar1 = piVar3[4];
      puVar2[10] = 0x80;
      puVar2[9] = (uint8_t)iVar1;
      iVar1 = piVar3[5];
      puVar2[0xc] = 0x80;
      puVar2[0xb] = (uint8_t)iVar1;
      iVar1 = piVar3[6];
      puVar2[0xe] = 0x80;
      puVar2[0xd] = (uint8_t)iVar1;
      y = y + iVar5 * 2;
      puVar2[0xf] = (uint8_t)piVar3[7];
      puVar2 = puVar2 + iVar6;
      piVar3 = piVar3 + iVar5 * 2;
    } while (uVar4 < (uint)(int)w_h[3]);
  }
  return;
}

/* ==================================================================
 * yuv420_to_rgb888
 * Purpose: Converts YUV420 samples to RGB888 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010480
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv420_to_rgb888(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  int iVar1;
  int iVar2;
  ushort *puVar3;
  uint uVar4;
  uint uVar5;
  ushort *puVar6;
  int16_t *piVar7;
  short sVar8;
  uint uVar9;
  short sVar10;
  int iVar11;
  int iVar12;
  uint uVar13;
  ushort *puVar14;
  int iVar15;
  int iVar16;
  uint8_t *puVar17;
  uint8_t *puVar18;
  uint8_t *puVar19;
  int16_t *local_40;
  int16_t *piStack_3c;

                    /* Unresolved local var: uint8_t * rgb_out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???] */
  iVar2 = _DAT_fffd04a8;
                    /* Unresolved local var: size_t j@[???] */
  sVar8 = w_h[3];
  if (sVar8 != 0) {
    sVar10 = w_h[2];
    iVar12 = (int)sVar10;
    puVar17 = rgb_out + *w_h * 3;
    iVar1 = *w_h * 6;
    uVar5 = 0;
    piVar7 = y + iVar12;
    iVar15 = iVar12;
    local_40 = u;
    piStack_3c = v;
    while( true ) {
      uVar4 = 0;
      puVar18 = puVar17;
      puVar19 = rgb_out;
      if (iVar15 != 0) {
        do {
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int index@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
          uVar13 = (uint)((ulonglong)uVar4 * (ulonglong)_DAT_fffd04c0 >> 0x20);
          uVar4 = uVar4 + 6;
          uVar9 = uVar13 >> 2;
          sVar8 = piStack_3c[uVar9];
          uVar13 = uVar13 >> 1;
          puVar3 = (ushort *)(y + uVar13);
          sVar10 = local_40[uVar9];
          iVar16 = (sVar8 + -0x80) * _DAT_fffd04d4 >> 0xc;
          *puVar19 = *(uint8_t *)(iVar2 + (iVar16 + (uint)*puVar3 & 0x3ff));
          iVar15 = _DAT_fffd052c;
          iVar11 = (sVar10 + -0x80) * 0x581 + (sVar8 + -0x80) * 0xb6d >> 0xc;
          puVar19[1] = *(uint8_t *)(iVar2 + ((uint)*puVar3 - iVar11 & 0x3ff));
          iVar15 = (sVar10 + -0x80) * iVar15 >> 0xc;
          puVar19[2] = *(uint8_t *)(iVar2 + (iVar15 + (uint)*puVar3 & 0x3ff));
          puVar3 = (ushort *)(y + uVar13 + 1);
          puVar14 = (ushort *)(piVar7 + uVar13);
          puVar6 = (ushort *)(piVar7 + uVar13 + 1);
          puVar19[3] = *(uint8_t *)(iVar2 + (iVar16 + (uint)*puVar3 & 0x3ff));
          puVar19[4] = *(uint8_t *)(iVar2 + ((uint)*puVar3 - iVar11 & 0x3ff));
          puVar19[5] = *(uint8_t *)(iVar2 + (iVar15 + (uint)*puVar3 & 0x3ff));
          puVar19 = puVar19 + 6;
          *puVar18 = *(uint8_t *)(iVar2 + (iVar16 + (uint)*puVar14 & 0x3ff));
          puVar18[1] = *(uint8_t *)(iVar2 + ((uint)*puVar14 - iVar11 & 0x3ff));
          puVar18[2] = *(uint8_t *)(iVar2 + (iVar15 + (uint)*puVar14 & 0x3ff));
          puVar18[3] = *(uint8_t *)(iVar2 + (iVar16 + (uint)*puVar6 & 0x3ff));
          puVar18[4] = *(uint8_t *)(iVar2 + ((uint)*puVar6 - iVar11 & 0x3ff));
          puVar18[5] = *(uint8_t *)(iVar2 + (iVar15 + (uint)*puVar6 & 0x3ff));
          puVar18 = puVar18 + 6;
          sVar10 = w_h[2];
        } while (uVar4 < (uint)(sVar10 * 3));
        sVar8 = w_h[3];
      }
      uVar5 = uVar5 + 2;
      if ((uint)(int)sVar8 <= uVar5) break;
      rgb_out = rgb_out + iVar1;
      local_40 = local_40 + 8;
      puVar17 = puVar17 + iVar1;
      piStack_3c = piStack_3c + 8;
      y = y + iVar12 * 2;
      piVar7 = piVar7 + iVar12 * 2;
      iVar15 = (int)sVar10;
    }
  }
  return;
}

/* ==================================================================
 * yuv420_to_rgb565le
 * Purpose: Hot Player color path: reuses each chroma sample for a 2x2 luma group, converts fixed-point YCbCr to RGB and packs two-byte little-endian RGB565 pixels.
 * Entry: 00010638
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv420_to_rgb565le(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  int iVar1;
  ushort *puVar2;
  ushort *puVar3;
  ushort *puVar4;
  ushort *puVar5;
  ushort *puVar6;
  short sVar7;
  int iVar8;
  short sVar9;
  int iVar10;
  uint uVar11;
  uint uVar12;
  int iVar13;
  int iVar14;
  uint uVar15;
  int iVar16;
  int16_t *local_50;
  int16_t *piStack_4c;
  ushort *puStack_44;

                    /* Unresolved local var: uint8_t r@[???]
                       Unresolved local var: uint8_t g@[???]
                       Unresolved local var: uint8_t b@[???]
                       Unresolved local var: uint16_t * out@[???]
                       Unresolved local var: uint16_t * out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???] */
  iVar1 = _DAT_fffd0674;
                    /* Unresolved local var: size_t j@[???] */
  sVar9 = w_h[3];
  if (sVar9 != 0) {
    iVar10 = (int)*w_h;
    sVar7 = w_h[2];
    puVar5 = (ushort *)(rgb_out + iVar10 * 2);
    iVar14 = (int)sVar7;
    puStack_44 = (ushort *)(y + iVar14);
    uVar11 = 0;
    iVar16 = iVar14;
    local_50 = u;
    piStack_4c = v;
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
    while( true ) {
      if (iVar16 != 0) {
        uVar15 = 0;
        puVar2 = (ushort *)y;
        puVar3 = (ushort *)rgb_out;
        puVar4 = puStack_44;
        puVar6 = puVar5;
        do {
          iVar16 = *(short *)((int)piStack_4c + uVar15) + -0x80;
          iVar13 = *(short *)((int)local_50 + uVar15) + -0x80;
          uVar12 = (uint)*puVar2;
          iVar8 = iVar16 * _DAT_fffd06b4 >> 0xc;
          iVar16 = iVar13 * 0x581 + iVar16 * 0xb6d >> 0xc;
          iVar13 = iVar13 * _DAT_fffd06c0 >> 0xc;
          *puVar3 = (ushort)(*(byte *)(iVar1 + (uVar12 + iVar13 & 0x3ff)) >> 3) |
                    (*(byte *)(iVar1 + (uVar12 + iVar8 & 0x3ff)) & 0xf8) << 8 |
                    (*(byte *)(iVar1 + (uVar12 - iVar16 & 0x3ff)) & 0xfc) << 3;
          uVar12 = (uint)puVar2[1];
          uVar15 = uVar15 + 2;
          puVar3[1] = (ushort)(*(byte *)(iVar1 + (iVar13 + uVar12 & 0x3ff)) >> 3) |
                      (*(byte *)(iVar1 + (iVar8 + uVar12 & 0x3ff)) & 0xf8) << 8 |
                      (*(byte *)(iVar1 + (uVar12 - iVar16 & 0x3ff)) & 0xfc) << 3;
          uVar12 = (uint)*puVar4;
          puVar2 = puVar2 + 2;
          *puVar6 = (ushort)(*(byte *)(iVar1 + (iVar13 + uVar12 & 0x3ff)) >> 3) |
                    (*(byte *)(iVar1 + (iVar8 + uVar12 & 0x3ff)) & 0xf8) << 8 |
                    (*(byte *)(iVar1 + (uVar12 - iVar16 & 0x3ff)) & 0xfc) << 3;
          uVar12 = (uint)puVar4[1];
          puVar3 = puVar3 + 2;
          puVar6[1] = (ushort)(*(byte *)(iVar1 + (iVar13 + uVar12 & 0x3ff)) >> 3) |
                      (*(byte *)(iVar1 + (iVar8 + uVar12 & 0x3ff)) & 0xf8) << 8 |
                      (*(byte *)(iVar1 + (uVar12 - iVar16 & 0x3ff)) & 0xfc) << 3;
          sVar7 = w_h[2];
          puVar4 = puVar4 + 2;
          puVar6 = puVar6 + 2;
        } while (uVar15 < (uint)(int)sVar7);
        sVar9 = w_h[3];
      }
      uVar11 = uVar11 + 2;
      if ((uint)(int)sVar9 <= uVar11) break;
      rgb_out = (uint8_t *)((int)rgb_out + iVar10 * 2 * 2);
      puVar5 = puVar5 + iVar10 * 2;
      y = y + iVar14 * 2;
      puStack_44 = puStack_44 + iVar14 * 2;
      local_50 = local_50 + 8;
      piStack_4c = piStack_4c + 8;
      iVar16 = (int)sVar7;
    }
  }
  return;
}

/* ==================================================================
 * yuv420_to_rgb565be
 * Purpose: Converts YUV420 samples to big-endian RGB565 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010818
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv420_to_rgb565be(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  int iVar2;
  ushort *puVar3;
  ushort *puVar4;
  ushort *puVar5;
  ushort *puVar6;
  short sVar7;
  short sVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  ushort *puVar12;
  uint uVar13;
  uint uVar14;
  int iVar15;
  uint uVar16;
  int iVar17;
  ushort *puVar18;
  int16_t *local_50;
  int16_t *piStack_4c;

                    /* Unresolved local var: uint8_t r@[???]
                       Unresolved local var: uint8_t g@[???]
                       Unresolved local var: uint8_t b@[???]
                       Unresolved local var: uint16_t * out@[???]
                       Unresolved local var: uint16_t * out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???] */
  iVar2 = _DAT_fffd0844;
                    /* Unresolved local var: size_t j@[???] */
  sVar8 = w_h[3];
  if (sVar8 != 0) {
    iVar10 = (int)*w_h;
    sVar7 = w_h[2];
    puVar3 = (ushort *)(rgb_out + iVar10 * 2);
    iVar15 = (int)sVar7;
    puVar12 = (ushort *)(y + iVar15);
    uVar13 = 0;
    iVar17 = iVar15;
    local_50 = u;
    piStack_4c = v;
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
    while( true ) {
      if (iVar17 != 0) {
        uVar16 = 0;
        puVar4 = (ushort *)y;
        puVar5 = (ushort *)rgb_out;
        puVar6 = puVar12;
        puVar18 = puVar3;
        do {
          uVar14 = (uint)*puVar4;
          iVar9 = *(short *)((int)piStack_4c + uVar16) + -0x80;
          iVar17 = *(short *)((int)local_50 + uVar16) + -0x80;
          iVar11 = iVar17 * _DAT_fffd0888 >> 0xc;
          iVar17 = iVar17 * 0x581 + iVar9 * 0xb6d >> 0xc;
          iVar9 = iVar9 * _DAT_fffd089c >> 0xc;
          bVar1 = *(byte *)(iVar2 + (uVar14 - iVar17 & 0x3ff));
          *puVar5 = *(byte *)(iVar2 + (uVar14 + iVar9 & 0x3ff)) & 0xfff8 |
                    CONCAT11(*(byte *)(iVar2 + (uVar14 + iVar11 & 0x3ff)) >> 3,bVar1 >> 5) |
                    (ushort)(bVar1 >> 2) << 0xd;
          uVar14 = (uint)puVar4[1];
          uVar16 = uVar16 + 2;
          bVar1 = *(byte *)(iVar2 + (uVar14 - iVar17 & 0x3ff));
          puVar5[1] = *(byte *)(iVar2 + (iVar9 + uVar14 & 0x3ff)) & 0xfff8 |
                      CONCAT11(*(byte *)(iVar2 + (iVar11 + uVar14 & 0x3ff)) >> 3,bVar1 >> 5) |
                      (ushort)(bVar1 >> 2) << 0xd;
          uVar14 = (uint)*puVar6;
          puVar4 = puVar4 + 2;
          bVar1 = *(byte *)(iVar2 + (uVar14 - iVar17 & 0x3ff));
          *puVar18 = *(byte *)(iVar2 + (iVar9 + uVar14 & 0x3ff)) & 0xfff8 |
                     CONCAT11(*(byte *)(iVar2 + (iVar11 + uVar14 & 0x3ff)) >> 3,bVar1 >> 5) |
                     (ushort)(bVar1 >> 2) << 0xd;
          uVar14 = (uint)puVar6[1];
          puVar5 = puVar5 + 2;
          bVar1 = *(byte *)(iVar2 + (uVar14 - iVar17 & 0x3ff));
          puVar18[1] = *(byte *)(iVar2 + (iVar9 + uVar14 & 0x3ff)) & 0xfff8 |
                       CONCAT11(*(byte *)(iVar2 + (iVar11 + uVar14 & 0x3ff)) >> 3,bVar1 >> 5) |
                       (ushort)(bVar1 >> 2) << 0xd;
          sVar7 = w_h[2];
          puVar6 = puVar6 + 2;
          puVar18 = puVar18 + 2;
        } while (uVar16 < (uint)(int)sVar7);
        sVar8 = w_h[3];
      }
      uVar13 = uVar13 + 2;
      if ((uint)(int)sVar8 <= uVar13) break;
      rgb_out = (uint8_t *)((int)rgb_out + iVar10 * 2 * 2);
      puVar3 = puVar3 + iVar10 * 2;
      y = y + iVar15 * 2;
      puVar12 = puVar12 + iVar15 * 2;
      local_50 = local_50 + 8;
      piStack_4c = piStack_4c + 8;
      iVar17 = (int)sVar7;
    }
  }
  return;
}

/* ==================================================================
 * yuv420_to_uyvy
 * Purpose: Converts YUV420 samples to packed UYVY without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010a18
 * ================================================================== */

void yuv420_to_uyvy(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  int16_t *piVar1;
  uint8_t *puVar2;
  int16_t *piVar3;
  int iVar4;
  uint uVar5;
  int iVar6;

                    /* Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: uint8_t * out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???] */
  iVar6 = (int)w_h[2];
  puVar2 = rgb_out + *w_h * 2;
                    /* Unresolved local var: size_t j@[???] */
  piVar3 = y + iVar6;
  iVar4 = *w_h * 4;
  if (iVar6 == 8) {
                    /* Unresolved local var: size_t j@[???] */
    if (w_h[3] != 0) {
      uVar5 = 0;
      do {
        uVar5 = uVar5 + 2;
        *rgb_out = (uint8_t)*u;
        rgb_out[1] = (uint8_t)*y;
        rgb_out[2] = (uint8_t)*v;
        rgb_out[3] = (uint8_t)y[1];
        rgb_out[4] = (uint8_t)u[1];
        rgb_out[5] = (uint8_t)y[2];
        rgb_out[6] = (uint8_t)v[1];
        rgb_out[7] = (uint8_t)y[3];
        rgb_out[8] = (uint8_t)u[2];
        rgb_out[9] = (uint8_t)y[4];
        rgb_out[10] = (uint8_t)v[2];
        rgb_out[0xb] = (uint8_t)y[5];
        rgb_out[0xc] = (uint8_t)u[3];
        rgb_out[0xd] = (uint8_t)y[6];
        rgb_out[0xe] = (uint8_t)v[3];
        piVar1 = y + 7;
        y = y + 0x10;
        rgb_out[0xf] = (uint8_t)*piVar1;
        rgb_out = rgb_out + iVar4;
        *puVar2 = (uint8_t)*u;
        puVar2[1] = (uint8_t)*piVar3;
        puVar2[2] = (uint8_t)*v;
        puVar2[3] = (uint8_t)piVar3[1];
        puVar2[4] = (uint8_t)u[1];
        puVar2[5] = (uint8_t)piVar3[2];
        puVar2[6] = (uint8_t)v[1];
        puVar2[7] = (uint8_t)piVar3[3];
        puVar2[8] = (uint8_t)u[2];
        puVar2[9] = (uint8_t)piVar3[4];
        puVar2[10] = (uint8_t)v[2];
        puVar2[0xb] = (uint8_t)piVar3[5];
        piVar1 = u + 3;
        u = u + 8;
        puVar2[0xc] = (uint8_t)*piVar1;
        puVar2[0xd] = (uint8_t)piVar3[6];
        piVar1 = v + 3;
        v = v + 8;
        puVar2[0xe] = (uint8_t)*piVar1;
        piVar1 = piVar3 + 7;
        piVar3 = piVar3 + 0x10;
        puVar2[0xf] = (uint8_t)*piVar1;
        puVar2 = puVar2 + iVar4;
      } while (uVar5 < (uint)(int)w_h[3]);
    }
  }
  else {
    uVar5 = 0;
    if (w_h[3] != 0) {
      do {
        uVar5 = uVar5 + 2;
        *rgb_out = (uint8_t)*u;
        rgb_out[1] = (uint8_t)*y;
        rgb_out[2] = (uint8_t)*v;
        rgb_out[3] = (uint8_t)y[1];
        rgb_out[4] = (uint8_t)u[1];
        rgb_out[5] = (uint8_t)y[2];
        rgb_out[6] = (uint8_t)v[1];
        rgb_out[7] = (uint8_t)y[3];
        rgb_out[8] = (uint8_t)u[2];
        rgb_out[9] = (uint8_t)y[4];
        rgb_out[10] = (uint8_t)v[2];
        rgb_out[0xb] = (uint8_t)y[5];
        rgb_out[0xc] = (uint8_t)u[3];
        rgb_out[0xd] = (uint8_t)y[6];
        rgb_out[0xe] = (uint8_t)v[3];
        rgb_out[0xf] = (uint8_t)y[7];
        rgb_out[0x10] = (uint8_t)u[4];
        rgb_out[0x11] = (uint8_t)y[8];
        rgb_out[0x12] = (uint8_t)v[4];
        rgb_out[0x13] = (uint8_t)y[9];
        rgb_out[0x14] = (uint8_t)u[5];
        rgb_out[0x15] = (uint8_t)y[10];
        rgb_out[0x16] = (uint8_t)v[5];
        rgb_out[0x17] = (uint8_t)y[0xb];
        rgb_out[0x18] = (uint8_t)u[6];
        rgb_out[0x19] = (uint8_t)y[0xc];
        rgb_out[0x1a] = (uint8_t)v[6];
        rgb_out[0x1b] = (uint8_t)y[0xd];
        rgb_out[0x1c] = (uint8_t)u[7];
        rgb_out[0x1d] = (uint8_t)y[0xe];
        rgb_out[0x1e] = (uint8_t)v[7];
        piVar1 = y + 0xf;
        y = y + iVar6 * 2;
        rgb_out[0x1f] = (uint8_t)*piVar1;
        rgb_out = rgb_out + iVar4;
        *puVar2 = (uint8_t)*u;
        puVar2[1] = (uint8_t)*piVar3;
        puVar2[2] = (uint8_t)*v;
        puVar2[3] = (uint8_t)piVar3[1];
        puVar2[4] = (uint8_t)u[1];
        puVar2[5] = (uint8_t)piVar3[2];
        puVar2[6] = (uint8_t)v[1];
        puVar2[7] = (uint8_t)piVar3[3];
        puVar2[8] = (uint8_t)u[2];
        puVar2[9] = (uint8_t)piVar3[4];
        puVar2[10] = (uint8_t)v[2];
        puVar2[0xb] = (uint8_t)piVar3[5];
        puVar2[0xc] = (uint8_t)u[3];
        puVar2[0xd] = (uint8_t)piVar3[6];
        puVar2[0xe] = (uint8_t)v[3];
        puVar2[0xf] = (uint8_t)piVar3[7];
        puVar2[0x10] = (uint8_t)u[4];
        puVar2[0x11] = (uint8_t)piVar3[8];
        puVar2[0x12] = (uint8_t)v[4];
        puVar2[0x13] = (uint8_t)piVar3[9];
        puVar2[0x14] = (uint8_t)u[5];
        puVar2[0x15] = (uint8_t)piVar3[10];
        puVar2[0x16] = (uint8_t)v[5];
        puVar2[0x17] = (uint8_t)piVar3[0xb];
        puVar2[0x18] = (uint8_t)u[6];
        puVar2[0x19] = (uint8_t)piVar3[0xc];
        puVar2[0x1a] = (uint8_t)v[6];
        puVar2[0x1b] = (uint8_t)piVar3[0xd];
        piVar1 = u + 7;
        u = u + 8;
        puVar2[0x1c] = (uint8_t)*piVar1;
        puVar2[0x1d] = (uint8_t)piVar3[0xe];
        piVar1 = v + 7;
        v = v + 8;
        puVar2[0x1e] = (uint8_t)*piVar1;
        piVar1 = piVar3 + 0xf;
        piVar3 = piVar3 + iVar6 * 2;
        puVar2[0x1f] = (uint8_t)*piVar1;
        puVar2 = puVar2 + iVar4;
      } while (uVar5 < (uint)(int)w_h[3]);
    }
  }
  return;
}

/* ==================================================================
 * yuv422_to_rgb888
 * Purpose: Converts YUV422 samples to RGB888 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010cbc
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv422_to_rgb888(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  ulonglong uVar1;
  int iVar2;
  ushort *puVar3;
  uint uVar4;
  uint8_t *puVar5;
  uint8_t *puVar6;
  short sVar7;
  uint uVar8;
  int iVar9;
  int16_t iVar10;
  short sVar11;
  int iVar12;
  int iVar13;
  uint uVar14;
  int iVar15;
  int iVar16;
  uint8_t *puVar17;
  uint uVar18;
  uint uVar19;
  int16_t *local_40;
  int16_t *piStack_38;

  iVar9 = _DAT_fffd0e10;
  uVar18 = _DAT_fffd0cec;
                    /* Unresolved local var: int32_t rgb_step@[???] */
                    /* Unresolved local var: size_t j@[???] */
  sVar7 = w_h[3];
  iVar2 = *w_h * 3;
  local_40 = u;
  if ((w_h[1] & 0xfffdU) == 0) {
    if (sVar7 != 0) {
      iVar10 = w_h[2];
      uVar19 = 0;
      do {
        uVar4 = 0;
        puVar17 = rgb_out;
        if (iVar10 != 0) {
          do {
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int index@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
            uVar14 = (uint)((ulonglong)uVar4 * (ulonglong)uVar18 >> 0x20);
            uVar4 = uVar4 + 6;
            uVar8 = uVar14 >> 2;
            sVar7 = v[uVar8];
            puVar3 = (ushort *)((uVar14 & 0xfffffffe) + (int)y);
            sVar11 = local_40[uVar8];
            iVar15 = (sVar7 + -0x80) * _DAT_fffd0d0c >> 0xc;
            *puVar17 = *(uint8_t *)(uVar18 + (iVar15 + (uint)*puVar3 & 0x3ff));
            iVar9 = _DAT_fffd0d64;
            iVar12 = (sVar11 + -0x80) * 0x581 + (sVar7 + -0x80) * 0xb6d >> 0xc;
            puVar17[1] = *(uint8_t *)(uVar18 + ((uint)*puVar3 - iVar12 & 0x3ff));
            iVar9 = (sVar11 + -0x80) * iVar9 >> 0xc;
            puVar17[2] = *(uint8_t *)(uVar18 + (iVar9 + (uint)*puVar3 & 0x3ff));
            puVar17[3] = *(uint8_t *)(uVar18 + (iVar15 + (uint)puVar3[1] & 0x3ff));
            puVar17[4] = *(uint8_t *)(uVar18 + ((uint)puVar3[1] - iVar12 & 0x3ff));
            puVar17[5] = *(uint8_t *)(uVar18 + (iVar9 + (uint)puVar3[1] & 0x3ff));
            puVar17 = puVar17 + 6;
            iVar10 = w_h[2];
          } while (uVar4 < (uint)(iVar10 * 3));
          sVar7 = w_h[3];
          y = y + iVar10;
        }
        uVar19 = uVar19 + 1;
        rgb_out = rgb_out + iVar2;
        local_40 = local_40 + 8;
        v = v + 8;
      } while (uVar19 < (uint)(int)sVar7);
    }
  }
  else {
                    /* Unresolved local var: uint8_t * rgb_out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???]
                       Unresolved local var: size_t j@[???] */
    if (sVar7 != 0) {
      puVar17 = rgb_out + iVar2;
      sVar11 = w_h[2];
      iVar2 = *w_h * 6;
      iVar15 = (int)sVar11;
      piStack_38 = y + iVar15;
      uVar18 = 0;
      iVar12 = iVar15;
      while( true ) {
        uVar19 = 0;
        puVar5 = rgb_out;
        puVar6 = puVar17;
        if (iVar12 != 0) {
          do {
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int index@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
            uVar1 = (ulonglong)uVar19;
            uVar19 = uVar19 + 3;
            uVar4 = (uint)(uVar1 * _DAT_fffd0e2c >> 0x21);
            sVar7 = v[uVar4];
            puVar3 = (ushort *)(y + uVar4);
            sVar11 = local_40[uVar4];
            iVar16 = (sVar7 + -0x80) * _DAT_fffd0e40 >> 0xc;
            *puVar5 = *(uint8_t *)(iVar9 + (iVar16 + (uint)*puVar3 & 0x3ff));
            iVar12 = _DAT_fffd0e94;
            iVar13 = (sVar11 + -0x80) * 0x581 + (sVar7 + -0x80) * 0xb6d >> 0xc;
            puVar5[1] = *(uint8_t *)(iVar9 + ((uint)*puVar3 - iVar13 & 0x3ff));
            iVar12 = (sVar11 + -0x80) * iVar12 >> 0xc;
            puVar5[2] = *(uint8_t *)(iVar9 + (iVar12 + (uint)*puVar3 & 0x3ff));
            puVar3 = (ushort *)(piStack_38 + uVar4);
            puVar5 = puVar5 + 3;
            *puVar6 = *(uint8_t *)(iVar9 + (iVar16 + (uint)*puVar3 & 0x3ff));
            puVar6[1] = *(uint8_t *)(iVar9 + ((uint)*puVar3 - iVar13 & 0x3ff));
            puVar6[2] = *(uint8_t *)(iVar9 + (iVar12 + (uint)*puVar3 & 0x3ff));
            puVar6 = puVar6 + 3;
            sVar11 = w_h[2];
          } while (uVar19 < (uint)(sVar11 * 3));
          sVar7 = w_h[3];
        }
        uVar18 = uVar18 + 2;
        if ((uint)(int)sVar7 <= uVar18) break;
        rgb_out = rgb_out + iVar2;
        piStack_38 = piStack_38 + iVar15 * 2;
        puVar17 = puVar17 + iVar2;
        local_40 = local_40 + 8;
        y = y + iVar15 * 2;
        v = v + 8;
        iVar12 = (int)sVar11;
      }
    }
  }
  return;
}

/* ==================================================================
 * yuv422_to_rgb565le
 * Purpose: Converts YUV422 samples to little-endian RGB565 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00010f38
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv422_to_rgb565le(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  int16_t *piVar1;
  ushort *puVar2;
  ushort *puVar3;
  short sVar4;
  int iVar5;
  int iVar6;
  int iVar7;
  short sVar8;
  int iVar9;
  int iVar10;
  uint uVar11;
  uint uVar12;
  uint uVar13;
  int iVar14;
  int iVar15;
  uint uVar16;
  int16_t *local_40;
  int16_t *piStack_38;
  uint8_t *puStack_34;
  uint8_t *puStack_30;

  iVar5 = _DAT_fffd1098;
                    /* Unresolved local var: uint8_t r@[???]
                       Unresolved local var: uint8_t g@[???]
                       Unresolved local var: uint8_t b@[???]
                       Unresolved local var: uint16_t * out@[???] */
  iVar10 = _DAT_fffd105c;
                    /* Unresolved local var: size_t j@[???] */
  sVar4 = w_h[3];
  local_40 = u;
  if ((w_h[1] & 0xfffdU) == 0) {
    if (sVar4 != 0) {
      sVar8 = w_h[2];
      uVar12 = 0;
      do {
        uVar16 = 0;
        puVar2 = (ushort *)y;
        puVar3 = (ushort *)rgb_out;
        if (sVar8 != 0) {
          do {
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
            iVar5 = *(short *)((int)v + uVar16) + -0x80;
            iVar14 = *(short *)((int)local_40 + uVar16) + -0x80;
            uVar11 = (uint)*puVar2;
            iVar6 = iVar5 * _DAT_fffd0f80 >> 0xc;
            iVar5 = iVar14 * 0x581 + iVar5 * 0xb6d >> 0xc;
            iVar14 = iVar14 * _DAT_fffd0f8c >> 0xc;
            *puVar3 = (ushort)(*(byte *)(iVar10 + (uVar11 + iVar14 & 0x3ff)) >> 3) |
                      (*(byte *)(iVar10 + (uVar11 + iVar6 & 0x3ff)) & 0xf8) << 8 |
                      (*(byte *)(iVar10 + (uVar11 - iVar5 & 0x3ff)) & 0xfc) << 3;
            uVar11 = (uint)puVar2[1];
            uVar16 = uVar16 + 2;
            puVar3[1] = (ushort)(*(byte *)(iVar10 + (iVar14 + uVar11 & 0x3ff)) >> 3) |
                        (*(byte *)(iVar10 + (iVar6 + uVar11 & 0x3ff)) & 0xf8) << 8 |
                        (*(byte *)(iVar10 + (uVar11 - iVar5 & 0x3ff)) & 0xfc) << 3;
            sVar8 = w_h[2];
            puVar2 = puVar2 + 2;
            puVar3 = puVar3 + 2;
          } while (uVar16 < (uint)(int)sVar8);
          sVar4 = w_h[3];
          y = y + sVar8;
        }
        uVar12 = uVar12 + 1;
        rgb_out = (uint8_t *)((int)rgb_out + *w_h * 2);
        v = v + 8;
        local_40 = local_40 + 8;
      } while (uVar12 < (uint)(int)sVar4);
    }
  }
  else {
                    /* Unresolved local var: uint16_t * out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???]
                       Unresolved local var: size_t j@[???] */
    if (sVar4 != 0) {
      sVar8 = w_h[2];
      puStack_30 = rgb_out + *w_h * 2;
      iVar6 = (int)sVar8;
      iVar14 = *w_h * 4;
      piVar1 = y + iVar6;
      uVar12 = 0;
      iVar10 = iVar6;
      piStack_38 = y;
      puStack_34 = rgb_out;
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
      while( true ) {
        uVar11 = 0;
        uVar16 = uVar11;
        if (iVar10 != 0) {
          do {
            iVar10 = *(short *)((int)v + uVar16) + -0x80;
            iVar15 = *(short *)((int)local_40 + uVar16) + -0x80;
            uVar13 = (uint)*(ushort *)((int)piStack_38 + uVar16);
            iVar7 = iVar10 * _DAT_fffd10cc >> 0xc;
            iVar9 = iVar15 * _DAT_fffd10dc >> 0xc;
            iVar10 = iVar15 * 0x581 + iVar10 * 0xb6d >> 0xc;
            *(ushort *)(puStack_34 + uVar16) =
                 (ushort)(*(byte *)(iVar5 + (uVar13 + iVar9 & 0x3ff)) >> 3) |
                 (*(byte *)(iVar5 + (uVar13 + iVar7 & 0x3ff)) & 0xf8) << 8 |
                 (*(byte *)(iVar5 + (uVar13 - iVar10 & 0x3ff)) & 0xfc) << 3;
            uVar13 = (uint)*(ushort *)((int)piVar1 + uVar16);
            *(ushort *)(puStack_30 + uVar16) =
                 (ushort)(*(byte *)(iVar5 + (iVar9 + uVar13 & 0x3ff)) >> 3) |
                 (*(byte *)(iVar5 + (iVar7 + uVar13 & 0x3ff)) & 0xf8) << 8 |
                 (*(byte *)(iVar5 + (uVar13 - iVar10 & 0x3ff)) & 0xfc) << 3;
            sVar8 = w_h[2];
            uVar11 = uVar11 + 1;
            uVar16 = uVar16 + 2;
          } while (uVar11 < (uint)(int)sVar8);
          sVar4 = w_h[3];
        }
        uVar12 = uVar12 + 2;
        if ((uint)(int)sVar4 <= uVar12) break;
        piVar1 = piVar1 + iVar6 * 2;
        puStack_34 = puStack_34 + iVar14;
        v = v + 8;
        puStack_30 = puStack_30 + iVar14;
        piStack_38 = piStack_38 + iVar6 * 2;
        local_40 = local_40 + 8;
        iVar10 = (int)sVar8;
      }
    }
  }
  return;
}

/* ==================================================================
 * yuv422_to_rgb565be
 * Purpose: Converts YUV422 samples to big-endian RGB565 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 000111c8
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv422_to_rgb565be(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  uint uVar2;
  ushort *puVar3;
  short sVar4;
  short sVar5;
  int iVar6;
  int iVar7;
  int iVar8;
  int iVar9;
  uint uVar10;
  uint uVar11;
  int iVar12;
  int iVar13;
  uint uVar14;
  ushort *puVar15;
  int16_t *local_40;
  uint8_t *puStack_3c;
  int16_t *piStack_38;

  iVar8 = _DAT_fffd1334;
                    /* Unresolved local var: uint8_t r@[???]
                       Unresolved local var: uint8_t g@[???]
                       Unresolved local var: uint8_t b@[???]
                       Unresolved local var: uint16_t * out@[???] */
  iVar9 = _DAT_fffd12f8;
                    /* Unresolved local var: size_t j@[???] */
  sVar4 = w_h[3];
  if ((w_h[1] & 0xfffdU) == 0) {
    if (sVar4 != 0) {
      sVar5 = w_h[2];
      uVar2 = 0;
      do {
        uVar14 = 0;
        puVar3 = (ushort *)y;
        puVar15 = (ushort *)rgb_out;
        if (sVar5 != 0) {
          do {
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
            iVar6 = *(short *)((int)v + uVar14) + -0x80;
            iVar8 = *(short *)((int)u + uVar14) + -0x80;
            uVar10 = (uint)*puVar3;
            iVar12 = iVar8 * _DAT_fffd120c >> 0xc;
            iVar8 = iVar8 * 0x581 + iVar6 * 0xb6d >> 0xc;
            iVar6 = iVar6 * _DAT_fffd1220 >> 0xc;
            bVar1 = *(byte *)(iVar9 + (uVar10 - iVar8 & 0x3ff));
            *puVar15 = *(byte *)(iVar9 + (uVar10 + iVar6 & 0x3ff)) & 0xfff8 |
                       CONCAT11(*(byte *)(iVar9 + (uVar10 + iVar12 & 0x3ff)) >> 3,bVar1 >> 5) |
                       (ushort)(bVar1 >> 2) << 0xd;
            uVar10 = (uint)puVar3[1];
            uVar14 = uVar14 + 2;
            bVar1 = *(byte *)(iVar9 + (uVar10 - iVar8 & 0x3ff));
            puVar15[1] = *(byte *)(iVar9 + (iVar6 + uVar10 & 0x3ff)) & 0xfff8 |
                         CONCAT11(*(byte *)(iVar9 + (iVar12 + uVar10 & 0x3ff)) >> 3,bVar1 >> 5) |
                         (ushort)(bVar1 >> 2) << 0xd;
            sVar5 = w_h[2];
            puVar3 = puVar3 + 2;
            puVar15 = puVar15 + 2;
          } while (uVar14 < (uint)(int)sVar5);
          sVar4 = w_h[3];
          y = y + sVar5;
        }
        uVar2 = uVar2 + 1;
        rgb_out = (uint8_t *)((int)rgb_out + *w_h * 2);
        u = u + 8;
        v = v + 8;
      } while (uVar2 < (uint)(int)sVar4);
    }
  }
  else {
                    /* Unresolved local var: uint16_t * out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???]
                       Unresolved local var: size_t j@[???] */
    if (sVar4 != 0) {
      sVar5 = w_h[2];
      puStack_3c = rgb_out + *w_h * 2;
      iVar12 = (int)sVar5;
      piStack_38 = y + iVar12;
      iVar6 = *w_h * 4;
      uVar2 = 0;
      iVar9 = iVar12;
      local_40 = y;
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
      while( true ) {
        uVar10 = 0;
        uVar14 = uVar10;
        if (iVar9 != 0) {
          do {
            iVar7 = *(short *)((int)v + uVar14) + -0x80;
            iVar9 = *(short *)((int)u + uVar14) + -0x80;
            uVar11 = (uint)*(ushort *)((int)local_40 + uVar14);
            iVar13 = iVar9 * _DAT_fffd1364 >> 0xc;
            iVar9 = iVar9 * 0x581 + iVar7 * 0xb6d >> 0xc;
            iVar7 = iVar7 * _DAT_fffd137c >> 0xc;
            bVar1 = *(byte *)(iVar8 + (uVar11 - iVar9 & 0x3ff));
            *(ushort *)(rgb_out + uVar14) =
                 *(byte *)(iVar8 + (uVar11 + iVar7 & 0x3ff)) & 0xfff8 |
                 CONCAT11(*(byte *)(iVar8 + (uVar11 + iVar13 & 0x3ff)) >> 3,bVar1 >> 5) |
                 (ushort)(bVar1 >> 2) << 0xd;
            uVar10 = uVar10 + 1;
            uVar11 = (uint)*(ushort *)((int)piStack_38 + uVar14);
            bVar1 = *(byte *)(iVar8 + (uVar11 - iVar9 & 0x3ff));
            *(ushort *)(puStack_3c + uVar14) =
                 *(byte *)(iVar8 + (iVar7 + uVar11 & 0x3ff)) & 0xfff8 |
                 CONCAT11(*(byte *)(iVar8 + (iVar13 + uVar11 & 0x3ff)) >> 3,bVar1 >> 5) |
                 (ushort)(bVar1 >> 2) << 0xd;
            sVar5 = w_h[2];
            uVar14 = uVar14 + 2;
          } while (uVar10 < (uint)(int)sVar5);
          sVar4 = w_h[3];
        }
        uVar2 = uVar2 + 2;
        if ((uint)(int)sVar4 <= uVar2) break;
        rgb_out = rgb_out + iVar6;
        puStack_3c = puStack_3c + iVar6;
        u = u + 8;
        local_40 = local_40 + iVar12 * 2;
        v = v + 8;
        piStack_38 = piStack_38 + iVar12 * 2;
        iVar9 = (int)sVar5;
      }
    }
  }
  return;
}

/* ==================================================================
 * yuv422_to_uyvy
 * Purpose: Converts YUV422 samples to packed UYVY without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00011470
 * ================================================================== */

void yuv422_to_uyvy(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  int16_t *piVar1;
  uint uVar2;
  uint8_t *puVar3;
  int iVar4;
  int16_t *piVar5;
  int iVar6;

                    /* Unresolved local var: size_t j@[???] */
  if ((w_h[1] & 0xfffdU) == 0) {
    uVar2 = 0;
    if (w_h[3] != 0) {
      do {
        uVar2 = uVar2 + 1;
        *rgb_out = (uint8_t)*u;
        rgb_out[1] = (uint8_t)*y;
        rgb_out[2] = (uint8_t)*v;
        rgb_out[3] = (uint8_t)y[1];
        rgb_out[4] = (uint8_t)u[1];
        rgb_out[5] = (uint8_t)y[2];
        rgb_out[6] = (uint8_t)v[1];
        rgb_out[7] = (uint8_t)y[3];
        rgb_out[8] = (uint8_t)u[2];
        rgb_out[9] = (uint8_t)y[4];
        rgb_out[10] = (uint8_t)v[2];
        rgb_out[0xb] = (uint8_t)y[5];
        rgb_out[0xc] = (uint8_t)u[3];
        rgb_out[0xd] = (uint8_t)y[6];
        rgb_out[0xe] = (uint8_t)v[3];
        rgb_out[0xf] = (uint8_t)y[7];
        iVar4 = (int)w_h[2];
        if (iVar4 == 0x10) {
          rgb_out[0x10] = (uint8_t)u[4];
          rgb_out[0x11] = (uint8_t)y[8];
          rgb_out[0x12] = (uint8_t)v[4];
          rgb_out[0x13] = (uint8_t)y[9];
          rgb_out[0x14] = (uint8_t)u[5];
          rgb_out[0x15] = (uint8_t)y[10];
          rgb_out[0x16] = (uint8_t)v[5];
          rgb_out[0x17] = (uint8_t)y[0xb];
          rgb_out[0x18] = (uint8_t)u[6];
          rgb_out[0x19] = (uint8_t)y[0xc];
          rgb_out[0x1a] = (uint8_t)v[6];
          rgb_out[0x1b] = (uint8_t)y[0xd];
          rgb_out[0x1c] = (uint8_t)u[7];
          rgb_out[0x1d] = (uint8_t)y[0xe];
          rgb_out[0x1e] = (uint8_t)v[7];
          rgb_out[0x1f] = (uint8_t)y[0xf];
          iVar4 = (int)w_h[2];
        }
        rgb_out = rgb_out + *w_h * 2;
        y = y + iVar4;
        u = u + 8;
        v = v + 8;
      } while (uVar2 < (uint)(int)w_h[3]);
    }
  }
  else {
                    /* Unresolved local var: uint8_t * out@[???]
                       Unresolved local var: uint8_t * out1@[???]
                       Unresolved local var: int16_t * y1@[???]
                       Unresolved local var: int32_t rgb_step@[???]
                       Unresolved local var: int32_t y_step@[???]
                       Unresolved local var: size_t j@[???] */
    if (w_h[3] != 0) {
      iVar4 = (int)w_h[2];
      puVar3 = rgb_out + *w_h * 2;
      piVar5 = y + iVar4;
      iVar6 = *w_h * 4;
      uVar2 = 0;
      do {
        uVar2 = uVar2 + 2;
        *rgb_out = (uint8_t)*u;
        rgb_out[1] = (uint8_t)*y;
        rgb_out[2] = (uint8_t)v[1];
        rgb_out[3] = (uint8_t)y[1];
        rgb_out[4] = (uint8_t)u[2];
        rgb_out[5] = (uint8_t)y[2];
        rgb_out[6] = (uint8_t)v[3];
        rgb_out[7] = (uint8_t)y[3];
        rgb_out[8] = (uint8_t)u[4];
        rgb_out[9] = (uint8_t)y[4];
        rgb_out[10] = (uint8_t)v[5];
        rgb_out[0xb] = (uint8_t)y[5];
        rgb_out[0xc] = (uint8_t)u[6];
        rgb_out[0xd] = (uint8_t)y[6];
        rgb_out[0xe] = (uint8_t)v[7];
        piVar1 = y + 7;
        y = y + iVar4 * 2;
        rgb_out[0xf] = (uint8_t)*piVar1;
        rgb_out = rgb_out + iVar6;
        *puVar3 = (uint8_t)*u;
        puVar3[1] = (uint8_t)*piVar5;
        puVar3[2] = (uint8_t)v[1];
        puVar3[3] = (uint8_t)piVar5[1];
        puVar3[4] = (uint8_t)u[2];
        puVar3[5] = (uint8_t)piVar5[2];
        puVar3[6] = (uint8_t)v[3];
        puVar3[7] = (uint8_t)piVar5[3];
        puVar3[8] = (uint8_t)u[4];
        puVar3[9] = (uint8_t)piVar5[4];
        puVar3[10] = (uint8_t)v[5];
        puVar3[0xb] = (uint8_t)piVar5[5];
        piVar1 = u + 6;
        u = u + 8;
        puVar3[0xc] = (uint8_t)*piVar1;
        puVar3[0xd] = (uint8_t)piVar5[6];
        piVar1 = v + 7;
        v = v + 8;
        puVar3[0xe] = (uint8_t)*piVar1;
        piVar1 = piVar5 + 7;
        piVar5 = piVar5 + iVar4 * 2;
        puVar3[0xf] = (uint8_t)*piVar1;
        puVar3 = puVar3 + iVar6;
      } while (uVar2 < (uint)(int)w_h[3]);
    }
  }
  return;
}

/* ==================================================================
 * yuv444_to_rgb888
 * Purpose: Converts YUV444 samples to RGB888 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00011664
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv444_to_rgb888(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  ulonglong uVar1;
  int iVar2;
  uint uVar3;
  int iVar4;
  uint uVar5;
  short sVar6;
  uint uVar7;
  uint8_t *puVar8;
  uint uVar9;
  short sVar10;
  ushort *puVar11;

  iVar4 = _DAT_fffd1734;
  uVar3 = _DAT_fffd1730;
                    /* Unresolved local var: size_t j@[???] */
  sVar10 = w_h[3];
  if (sVar10 != 0) {
    sVar6 = w_h[2];
    uVar7 = 0;
    do {
      uVar9 = 0;
      puVar8 = rgb_out;
      if (sVar6 != 0) {
        do {
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int index@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
          uVar1 = (ulonglong)uVar9;
          uVar9 = uVar9 + 3;
          uVar5 = (uint)(uVar1 * uVar3 >> 0x21);
          sVar10 = v[uVar5];
          puVar11 = (ushort *)(y + uVar5);
          sVar6 = u[uVar5];
          *puVar8 = *(uint8_t *)
                     (iVar4 + (((sVar10 + -0x80) * _DAT_fffd1688 >> 0xc) + (uint)*puVar11 & 0x3ff));
          iVar2 = _DAT_fffd16dc;
          puVar8[1] = *(uint8_t *)
                       (iVar4 + ((uint)*puVar11 -
                                 ((sVar6 + -0x80) * 0x581 + (sVar10 + -0x80) * 0xb6d >> 0xc) & 0x3ff
                                ));
          puVar8[2] = *(uint8_t *)
                       (iVar4 + (((sVar6 + -0x80) * iVar2 >> 0xc) + (uint)*puVar11 & 0x3ff));
          sVar6 = w_h[2];
          puVar8 = puVar8 + 3;
        } while (uVar9 < (uint)(sVar6 * 3));
        sVar10 = w_h[3];
        y = y + sVar6;
      }
      uVar7 = uVar7 + 1;
      rgb_out = rgb_out + *w_h * 3;
      u = u + 8;
      v = v + 8;
    } while (uVar7 < (uint)(int)sVar10);
  }
  return;
}

/* ==================================================================
 * yuv444_to_rgb565le
 * Purpose: Converts YUV444 samples to little-endian RGB565 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00011748
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv444_to_rgb565le(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  int iVar1;
  short sVar2;
  int iVar3;
  short sVar4;
  uint uVar5;
  int iVar6;
  uint uVar7;
  uint uVar8;
  uint uVar9;
  int16_t *local_30;

                    /* Unresolved local var: uint8_t r@[???]
                       Unresolved local var: uint8_t g@[???]
                       Unresolved local var: uint8_t b@[???]
                       Unresolved local var: uint16_t * out@[???] */
  iVar1 = _DAT_fffd181c;
                    /* Unresolved local var: size_t j@[???] */
  sVar2 = w_h[3];
  if (sVar2 != 0) {
    sVar4 = w_h[2];
    uVar7 = 0;
    local_30 = y;
    do {
      uVar9 = 0;
      uVar8 = uVar9;
      if (sVar4 != 0) {
        do {
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
          iVar3 = *(short *)((int)v + uVar8) + -0x80;
          iVar6 = *(short *)((int)u + uVar8) + -0x80;
          uVar5 = (uint)*(ushort *)((int)local_30 + uVar8);
          *(ushort *)(rgb_out + uVar8) =
               (ushort)(*(byte *)(iVar1 + (uVar5 + (iVar6 * _DAT_fffd1784 >> 0xc) & 0x3ff)) >> 3) |
               (*(byte *)(iVar1 + (uVar5 + (iVar3 * _DAT_fffd1770 >> 0xc) & 0x3ff)) & 0xf8) << 8 |
               (*(byte *)(iVar1 + (uVar5 - (iVar6 * 0x581 + iVar3 * 0xb6d >> 0xc) & 0x3ff)) & 0xfc)
               << 3;
          sVar4 = w_h[2];
          uVar9 = uVar9 + 1;
          uVar8 = uVar8 + 2;
        } while (uVar9 < (uint)(int)sVar4);
        local_30 = local_30 + sVar4;
        sVar2 = w_h[3];
      }
      uVar7 = uVar7 + 1;
      rgb_out = rgb_out + *w_h * 2;
      u = u + 8;
      v = v + 8;
    } while (uVar7 < (uint)(int)sVar2);
  }
  return;
}

/* ==================================================================
 * yuv444_to_rgb565be
 * Purpose: Converts YUV444 samples to big-endian RGB565 without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00011830
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void yuv444_to_rgb565be(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  byte bVar1;
  int iVar2;
  short sVar3;
  short sVar4;
  uint uVar5;
  int iVar6;
  int iVar7;
  uint uVar8;
  uint uVar9;
  uint uVar10;
  int16_t *local_30;

                    /* Unresolved local var: uint8_t r@[???]
                       Unresolved local var: uint8_t g@[???]
                       Unresolved local var: uint8_t b@[???]
                       Unresolved local var: uint16_t * out@[???] */
  iVar2 = _DAT_fffd190c;
                    /* Unresolved local var: size_t j@[???] */
  sVar3 = w_h[3];
  if (sVar3 != 0) {
    sVar4 = w_h[2];
    uVar10 = 0;
    local_30 = y;
    do {
      uVar9 = 0;
      uVar8 = uVar9;
      if (sVar4 != 0) {
        do {
                    /* Unresolved local var: size_t i@[???]
                       Unresolved local var: int rdif@[???]
                       Unresolved local var: int gdif@[???]
                       Unresolved local var: int bdif@[???] */
          iVar6 = *(short *)((int)v + uVar8) + -0x80;
          iVar7 = *(short *)((int)u + uVar8) + -0x80;
          uVar5 = (uint)*(ushort *)((int)local_30 + uVar8);
          bVar1 = *(byte *)(iVar2 + (uVar5 - (iVar7 * 0x581 + iVar6 * 0xb6d >> 0xc) & 0x3ff));
          *(ushort *)(rgb_out + uVar8) =
               *(byte *)(iVar2 + (uVar5 + (iVar6 * _DAT_fffd187c >> 0xc) & 0x3ff)) & 0xfff8 |
               CONCAT11(*(byte *)(iVar2 + (uVar5 + (iVar7 * _DAT_fffd1864 >> 0xc) & 0x3ff)) >> 3,
                        bVar1 >> 5) | (ushort)(bVar1 >> 2) << 0xd;
          sVar4 = w_h[2];
          uVar9 = uVar9 + 1;
          uVar8 = uVar8 + 2;
        } while (uVar9 < (uint)(int)sVar4);
        local_30 = local_30 + sVar4;
        sVar3 = w_h[3];
      }
      uVar10 = uVar10 + 1;
      rgb_out = rgb_out + *w_h * 2;
      u = u + 8;
      v = v + 8;
    } while (uVar10 < (uint)(int)sVar3);
  }
  return;
}

/* ==================================================================
 * yuv444_to_uyvy
 * Purpose: Converts YUV444 samples to packed UYVY without rotation; performs chroma reuse/upsampling, fixed-point YCbCr conversion and output packing.
 * Entry: 00011920
 * ================================================================== */

void yuv444_to_uyvy(int16_t *y,int16_t *u,int16_t *v,int16_t *w_h,uint8_t *rgb_out)

{
  int16_t *piVar1;
  uint uVar2;

                    /* Unresolved local var: size_t j@[???] */
  if (w_h[3] != 0) {
    uVar2 = 0;
    do {
      uVar2 = uVar2 + 1;
      *rgb_out = (uint8_t)*u;
      rgb_out[1] = (uint8_t)*y;
      rgb_out[2] = (uint8_t)v[1];
      rgb_out[3] = (uint8_t)y[1];
      rgb_out[4] = (uint8_t)u[2];
      rgb_out[5] = (uint8_t)y[2];
      rgb_out[6] = (uint8_t)v[3];
      rgb_out[7] = (uint8_t)y[3];
      rgb_out[8] = (uint8_t)u[4];
      rgb_out[9] = (uint8_t)y[4];
      rgb_out[10] = (uint8_t)v[5];
      rgb_out[0xb] = (uint8_t)y[5];
      piVar1 = u + 6;
      u = u + 8;
      rgb_out[0xc] = (uint8_t)*piVar1;
      rgb_out[0xd] = (uint8_t)y[6];
      piVar1 = v + 7;
      v = v + 8;
      rgb_out[0xe] = (uint8_t)*piVar1;
      rgb_out[0xf] = (uint8_t)y[7];
      rgb_out = rgb_out + *w_h * 2;
      y = y + w_h[2];
    } while (uVar2 < (uint)(int)w_h[3]);
  }
  return;
}
