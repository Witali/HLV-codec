/* ESP_NEW_JPEG 1.0.2 ESP32 decoder Ghidra pseudo-C.
 * Derived from the Espressif binary; see LICENSE.ESPRESSIF in the analysis directory.
 * Program: jpeg_dec_idct.S.obj
 * Language: Xtensa:LE:32:default
 * Types and names are reconstructed and must not be treated as the original source.
 */

/* ==================================================================
 * idct_block_8_8
 * Purpose: Xtensa integer inverse DCT producing a full 8x8 output block without rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 00010430
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_8_8(short *param_1,undefined4 param_2,short *param_3,ushort *param_4,int param_5)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short sVar7;
  short *psVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  short sVar13;
  short sVar14;
  int iVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  int iVar19;
  short sVar20;
  int iVar21;
  int iVar22;
  int iVar23;

  uVar12 = 0;
  do {
    psVar8 = param_1;
    if ((((((psVar8[0x20] == 0 && psVar8[8] == 0) && psVar8[0x28] == 0) && psVar8[0x10] == 0) &&
         psVar8[0x30] == 0) && psVar8[0x18] == 0) && psVar8[0x38] == 0) {
      sVar13 = *psVar8 * *param_3;
      *psVar8 = sVar13;
      psVar8[0x38] = sVar13;
      psVar8[8] = sVar13;
      psVar8[0x30] = sVar13;
      psVar8[0x10] = sVar13;
      psVar8[0x28] = sVar13;
      psVar8[0x18] = sVar13;
      psVar8[0x20] = sVar13;
    }
    else {
      sVar14 = *psVar8 * *param_3;
      iVar16 = (int)psVar8[0x10] * (int)param_3[0x10];
      sVar13 = psVar8[0x20] * param_3[0x20];
      iVar9 = (int)psVar8[0x30] * (int)param_3[0x30];
      sVar20 = sVar14 + sVar13;
      sVar14 = sVar14 - sVar13;
      sVar5 = (short)iVar9 + (short)iVar16;
      sVar3 = (short)((uint)((iVar16 - iVar9) * 0x16a) >> 8) - sVar5;
      sVar13 = sVar20 + sVar5;
      sVar20 = sVar20 - sVar5;
      sVar5 = sVar3 + sVar14;
      sVar14 = sVar14 - sVar3;
      iVar10 = (int)psVar8[8] * (int)param_3[8] - (int)psVar8[0x38] * (int)param_3[0x38];
      iVar9 = (int)psVar8[8] * (int)param_3[8] + (int)psVar8[0x38] * (int)param_3[0x38];
      iVar16 = (int)psVar8[0x28] * (int)param_3[0x28] - (int)psVar8[0x18] * (int)param_3[0x18];
      iVar19 = (int)psVar8[0x18] * (int)param_3[0x18] + (int)psVar8[0x28] * (int)param_3[0x28];
      sVar7 = (short)iVar19 + (short)iVar9;
      sVar3 = (short)((uint)((iVar10 + iVar16) * 0x1d9) >> 8);
      sVar6 = (sVar3 - (short)((uint)(iVar16 * 0x29d) >> 8)) - sVar7;
      sVar4 = (short)((uint)((iVar9 - iVar19) * 0x16a) >> 8) - sVar6;
      sVar3 = (sVar3 - (short)((uint)(iVar10 * 0x115) >> 8)) - sVar4;
      *psVar8 = sVar13 + sVar7;
      psVar8[0x38] = sVar13 - sVar7;
      psVar8[8] = sVar5 + sVar6;
      psVar8[0x30] = sVar5 - sVar6;
      psVar8[0x10] = sVar14 + sVar4;
      psVar8[0x28] = sVar14 - sVar4;
      psVar8[0x18] = sVar20 + sVar3;
      psVar8[0x20] = sVar20 - sVar3;
    }
    param_3 = param_3 + 1;
    uVar12 = uVar12 + 1;
    param_1 = psVar8 + 1;
  } while (uVar12 < 8);
  psVar8 = psVar8 + -7;
  uVar12 = 0;
  do {
    iVar16 = _DAT_fffd05e4;
    iVar9 = *psVar8 + 0x1000 + (int)psVar8[4];
    iVar15 = (*psVar8 + 0x1000) - (int)psVar8[4];
    iVar19 = (int)psVar8[6] + (int)psVar8[2];
    iVar10 = (((int)psVar8[2] - (int)psVar8[6]) * 0x16a >> 8) - iVar19;
    iVar17 = iVar9 + iVar19;
    iVar9 = iVar9 - iVar19;
    iVar18 = iVar10 + iVar15;
    iVar15 = iVar15 - iVar10;
    iVar11 = (int)psVar8[1] - (int)psVar8[7];
    iVar21 = (int)psVar8[1] + (int)psVar8[7];
    iVar22 = (int)psVar8[5] - (int)psVar8[3];
    iVar10 = (int)psVar8[3] + (int)psVar8[5];
    iVar19 = iVar10 + iVar21;
    iVar23 = (iVar11 + iVar22) * 0x1d9 >> 8;
    iVar22 = (iVar23 - (iVar22 * 0x29d >> 8)) - iVar19;
    iVar21 = ((iVar21 - iVar10) * 0x16a >> 8) - iVar22;
    iVar10 = (iVar23 - (iVar11 * 0x115 >> 8)) - iVar21;
    bVar1 = *(byte *)(((uint)(iVar17 - iVar19) >> 5 & 0x3ff) + _DAT_fffd05e4);
    *param_4 = (ushort)*(byte *)(((uint)(iVar17 + iVar19) >> 5 & 0x3ff) + _DAT_fffd05e4);
    bVar2 = *(byte *)(((uint)(iVar18 + iVar22) >> 5 & 0x3ff) + iVar16);
    param_4[7] = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar18 - iVar22) >> 5 & 0x3ff) + iVar16);
    param_4[1] = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar15 + iVar21) >> 5 & 0x3ff) + iVar16);
    param_4[6] = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar15 - iVar21) >> 5 & 0x3ff) + iVar16);
    param_4[2] = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar9 + iVar10) >> 5 & 0x3ff) + iVar16);
    param_4[5] = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar9 - iVar10) >> 5 & 0x3ff) + iVar16);
    param_4[3] = (ushort)bVar2;
    psVar8 = psVar8 + 8;
    param_4[4] = (ushort)bVar1;
    param_4 = param_4 + param_5;
    uVar12 = uVar12 + 1;
  } while (uVar12 < 8);
  return;
}

/* ==================================================================
 * idct_block_8_8_90
 * Purpose: Xtensa integer inverse DCT producing a full 8x8 output block with 90-degree rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 00010660
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_8_8_90(short *param_1,undefined4 param_2,short *param_3,int param_4,int param_5)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short sVar7;
  short *psVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  short sVar13;
  short sVar14;
  int iVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  int iVar19;
  short sVar20;
  int iVar21;
  int iVar22;
  int iVar23;
  int iVar24;

  param_4 = param_4 + 0xe;
  uVar12 = 0;
  do {
    psVar8 = param_1;
    if ((((((psVar8[0x20] == 0 && psVar8[8] == 0) && psVar8[0x28] == 0) && psVar8[0x10] == 0) &&
         psVar8[0x30] == 0) && psVar8[0x18] == 0) && psVar8[0x38] == 0) {
      sVar13 = *psVar8 * *param_3;
      *psVar8 = sVar13;
      psVar8[0x38] = sVar13;
      psVar8[8] = sVar13;
      psVar8[0x30] = sVar13;
      psVar8[0x10] = sVar13;
      psVar8[0x28] = sVar13;
      psVar8[0x18] = sVar13;
      psVar8[0x20] = sVar13;
    }
    else {
      sVar14 = *psVar8 * *param_3;
      iVar23 = (int)psVar8[0x10] * (int)param_3[0x10];
      sVar13 = psVar8[0x20] * param_3[0x20];
      iVar18 = (int)psVar8[0x30] * (int)param_3[0x30];
      sVar20 = sVar14 + sVar13;
      sVar14 = sVar14 - sVar13;
      sVar5 = (short)iVar18 + (short)iVar23;
      sVar3 = (short)((uint)((iVar23 - iVar18) * 0x16a) >> 8) - sVar5;
      sVar13 = sVar20 + sVar5;
      sVar20 = sVar20 - sVar5;
      sVar5 = sVar3 + sVar14;
      sVar14 = sVar14 - sVar3;
      iVar9 = (int)psVar8[8] * (int)param_3[8] - (int)psVar8[0x38] * (int)param_3[0x38];
      iVar18 = (int)psVar8[8] * (int)param_3[8] + (int)psVar8[0x38] * (int)param_3[0x38];
      iVar23 = (int)psVar8[0x28] * (int)param_3[0x28] - (int)psVar8[0x18] * (int)param_3[0x18];
      iVar10 = (int)psVar8[0x18] * (int)param_3[0x18] + (int)psVar8[0x28] * (int)param_3[0x28];
      sVar7 = (short)iVar10 + (short)iVar18;
      sVar3 = (short)((uint)((iVar9 + iVar23) * 0x1d9) >> 8);
      sVar6 = (sVar3 - (short)((uint)(iVar23 * 0x29d) >> 8)) - sVar7;
      sVar4 = (short)((uint)((iVar18 - iVar10) * 0x16a) >> 8) - sVar6;
      sVar3 = (sVar3 - (short)((uint)(iVar9 * 0x115) >> 8)) - sVar4;
      *psVar8 = sVar13 + sVar7;
      psVar8[0x38] = sVar13 - sVar7;
      psVar8[8] = sVar5 + sVar6;
      psVar8[0x30] = sVar5 - sVar6;
      psVar8[0x10] = sVar14 + sVar4;
      psVar8[0x28] = sVar14 - sVar4;
      psVar8[0x18] = sVar20 + sVar3;
      psVar8[0x20] = sVar20 - sVar3;
    }
    param_3 = param_3 + 1;
    uVar12 = uVar12 + 1;
    param_1 = psVar8 + 1;
  } while (uVar12 < 8);
  psVar8 = psVar8 + -7;
  uVar12 = 0;
  iVar23 = 3;
  if (param_5 != 8) {
    iVar23 = 4;
  }
  iVar23 = 0x20 - iVar23;
  do {
    iVar18 = _DAT_fffd081c;
    iVar9 = *psVar8 + 0x1000 + (int)psVar8[4];
    iVar15 = (*psVar8 + 0x1000) - (int)psVar8[4];
    iVar19 = (int)psVar8[6] + (int)psVar8[2];
    iVar10 = (((int)psVar8[2] - (int)psVar8[6]) * 0x16a >> 8) - iVar19;
    iVar16 = iVar9 + iVar19;
    iVar9 = iVar9 - iVar19;
    iVar17 = iVar10 + iVar15;
    iVar15 = iVar15 - iVar10;
    iVar11 = (int)psVar8[1] - (int)psVar8[7];
    iVar21 = (int)psVar8[1] + (int)psVar8[7];
    iVar22 = (int)psVar8[5] - (int)psVar8[3];
    iVar10 = (int)psVar8[3] + (int)psVar8[5];
    iVar19 = iVar10 + iVar21;
    iVar24 = (iVar11 + iVar22) * 0x1d9 >> 8;
    iVar22 = (iVar24 - (iVar22 * 0x29d >> 8)) - iVar19;
    iVar21 = ((iVar21 - iVar10) * 0x16a >> 8) - iVar22;
    iVar10 = (iVar24 - (iVar11 * 0x115 >> 8)) - iVar21;
    bVar1 = *(byte *)(((uint)(iVar16 - iVar19) >> 5 & 0x3ff) + _DAT_fffd081c);
    *(ushort *)((0 << 0x20 - iVar23) + param_4) =
         (ushort)*(byte *)(((uint)(iVar16 + iVar19) >> 5 & 0x3ff) + _DAT_fffd081c);
    bVar2 = *(byte *)(((uint)(iVar17 + iVar22) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((0xe << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar17 - iVar22) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((2 << 0x20 - iVar23) + param_4) = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar15 + iVar21) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((0xc << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar15 - iVar21) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((4 << 0x20 - iVar23) + param_4) = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar9 + iVar10) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((10 << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar9 - iVar10) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((6 << 0x20 - iVar23) + param_4) = (ushort)bVar2;
    psVar8 = psVar8 + 8;
    *(ushort *)((8 << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    param_4 = param_4 + -2;
    uVar12 = uVar12 + 1;
  } while (uVar12 < 8);
  return;
}

/* ==================================================================
 * idct_block_8_8_180
 * Purpose: Xtensa integer inverse DCT producing a full 8x8 output block with 180-degree rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 000108d4
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_8_8_180(short *param_1,undefined4 param_2,short *param_3,int param_4,int param_5)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short sVar7;
  short *psVar8;
  ushort *puVar9;
  int iVar10;
  int iVar11;
  int iVar12;
  uint uVar13;
  short sVar14;
  short sVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  int iVar19;
  int iVar20;
  short sVar21;
  int iVar22;
  int iVar23;
  int iVar24;

  puVar9 = (ushort *)(param_4 + param_5 * 0xe);
  uVar13 = 0;
  do {
    psVar8 = param_1;
    if ((((((psVar8[0x20] == 0 && psVar8[8] == 0) && psVar8[0x28] == 0) && psVar8[0x10] == 0) &&
         psVar8[0x30] == 0) && psVar8[0x18] == 0) && psVar8[0x38] == 0) {
      sVar14 = *psVar8 * *param_3;
      *psVar8 = sVar14;
      psVar8[0x38] = sVar14;
      psVar8[8] = sVar14;
      psVar8[0x30] = sVar14;
      psVar8[0x10] = sVar14;
      psVar8[0x28] = sVar14;
      psVar8[0x18] = sVar14;
      psVar8[0x20] = sVar14;
    }
    else {
      sVar15 = *psVar8 * *param_3;
      iVar17 = (int)psVar8[0x10] * (int)param_3[0x10];
      sVar14 = psVar8[0x20] * param_3[0x20];
      iVar10 = (int)psVar8[0x30] * (int)param_3[0x30];
      sVar21 = sVar15 + sVar14;
      sVar15 = sVar15 - sVar14;
      sVar5 = (short)iVar10 + (short)iVar17;
      sVar3 = (short)((uint)((iVar17 - iVar10) * 0x16a) >> 8) - sVar5;
      sVar14 = sVar21 + sVar5;
      sVar21 = sVar21 - sVar5;
      sVar5 = sVar3 + sVar15;
      sVar15 = sVar15 - sVar3;
      iVar11 = (int)psVar8[8] * (int)param_3[8] - (int)psVar8[0x38] * (int)param_3[0x38];
      iVar10 = (int)psVar8[8] * (int)param_3[8] + (int)psVar8[0x38] * (int)param_3[0x38];
      iVar17 = (int)psVar8[0x28] * (int)param_3[0x28] - (int)psVar8[0x18] * (int)param_3[0x18];
      iVar20 = (int)psVar8[0x18] * (int)param_3[0x18] + (int)psVar8[0x28] * (int)param_3[0x28];
      sVar7 = (short)iVar20 + (short)iVar10;
      sVar3 = (short)((uint)((iVar11 + iVar17) * 0x1d9) >> 8);
      sVar6 = (sVar3 - (short)((uint)(iVar17 * 0x29d) >> 8)) - sVar7;
      sVar4 = (short)((uint)((iVar10 - iVar20) * 0x16a) >> 8) - sVar6;
      sVar3 = (sVar3 - (short)((uint)(iVar11 * 0x115) >> 8)) - sVar4;
      *psVar8 = sVar14 + sVar7;
      psVar8[0x38] = sVar14 - sVar7;
      psVar8[8] = sVar5 + sVar6;
      psVar8[0x30] = sVar5 - sVar6;
      psVar8[0x10] = sVar15 + sVar4;
      psVar8[0x28] = sVar15 - sVar4;
      psVar8[0x18] = sVar21 + sVar3;
      psVar8[0x20] = sVar21 - sVar3;
    }
    param_3 = param_3 + 1;
    uVar13 = uVar13 + 1;
    param_1 = psVar8 + 1;
  } while (uVar13 < 8);
  psVar8 = psVar8 + -7;
  uVar13 = 0;
  do {
    iVar17 = _DAT_fffd0a8c;
    iVar10 = *psVar8 + 0x1000 + (int)psVar8[4];
    iVar16 = (*psVar8 + 0x1000) - (int)psVar8[4];
    iVar20 = (int)psVar8[6] + (int)psVar8[2];
    iVar11 = (((int)psVar8[2] - (int)psVar8[6]) * 0x16a >> 8) - iVar20;
    iVar18 = iVar10 + iVar20;
    iVar10 = iVar10 - iVar20;
    iVar19 = iVar11 + iVar16;
    iVar16 = iVar16 - iVar11;
    iVar12 = (int)psVar8[1] - (int)psVar8[7];
    iVar22 = (int)psVar8[1] + (int)psVar8[7];
    iVar23 = (int)psVar8[5] - (int)psVar8[3];
    iVar11 = (int)psVar8[3] + (int)psVar8[5];
    iVar20 = iVar11 + iVar22;
    iVar24 = (iVar12 + iVar23) * 0x1d9 >> 8;
    iVar23 = (iVar24 - (iVar23 * 0x29d >> 8)) - iVar20;
    iVar22 = ((iVar22 - iVar11) * 0x16a >> 8) - iVar23;
    iVar11 = (iVar24 - (iVar12 * 0x115 >> 8)) - iVar22;
    bVar1 = *(byte *)(((uint)(iVar18 - iVar20) >> 5 & 0x3ff) + _DAT_fffd0a8c);
    puVar9[7] = (ushort)*(byte *)(((uint)(iVar18 + iVar20) >> 5 & 0x3ff) + _DAT_fffd0a8c);
    bVar2 = *(byte *)(((uint)(iVar19 + iVar23) >> 5 & 0x3ff) + iVar17);
    *puVar9 = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar19 - iVar23) >> 5 & 0x3ff) + iVar17);
    puVar9[6] = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar16 + iVar22) >> 5 & 0x3ff) + iVar17);
    puVar9[1] = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar16 - iVar22) >> 5 & 0x3ff) + iVar17);
    puVar9[5] = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar10 + iVar11) >> 5 & 0x3ff) + iVar17);
    puVar9[2] = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar10 - iVar11) >> 5 & 0x3ff) + iVar17);
    puVar9[4] = (ushort)bVar2;
    psVar8 = psVar8 + 8;
    puVar9[3] = (ushort)bVar1;
    puVar9 = puVar9 + -param_5;
    uVar13 = uVar13 + 1;
  } while (uVar13 < 8);
  return;
}

/* ==================================================================
 * idct_block_8_8_270
 * Purpose: Xtensa integer inverse DCT producing a full 8x8 output block with 270-degree rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 00010b0c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_8_8_270(short *param_1,undefined4 param_2,short *param_3,int param_4,int param_5)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short sVar7;
  short *psVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  short sVar13;
  short sVar14;
  int iVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  int iVar19;
  short sVar20;
  int iVar21;
  int iVar22;
  int iVar23;
  int iVar24;

  param_4 = param_4 + param_5 * 0xe;
  uVar12 = 0;
  do {
    psVar8 = param_1;
    if ((((((psVar8[0x20] == 0 && psVar8[8] == 0) && psVar8[0x28] == 0) && psVar8[0x10] == 0) &&
         psVar8[0x30] == 0) && psVar8[0x18] == 0) && psVar8[0x38] == 0) {
      sVar13 = *psVar8 * *param_3;
      *psVar8 = sVar13;
      psVar8[0x38] = sVar13;
      psVar8[8] = sVar13;
      psVar8[0x30] = sVar13;
      psVar8[0x10] = sVar13;
      psVar8[0x28] = sVar13;
      psVar8[0x18] = sVar13;
      psVar8[0x20] = sVar13;
    }
    else {
      sVar14 = *psVar8 * *param_3;
      iVar23 = (int)psVar8[0x10] * (int)param_3[0x10];
      sVar13 = psVar8[0x20] * param_3[0x20];
      iVar18 = (int)psVar8[0x30] * (int)param_3[0x30];
      sVar20 = sVar14 + sVar13;
      sVar14 = sVar14 - sVar13;
      sVar5 = (short)iVar18 + (short)iVar23;
      sVar3 = (short)((uint)((iVar23 - iVar18) * 0x16a) >> 8) - sVar5;
      sVar13 = sVar20 + sVar5;
      sVar20 = sVar20 - sVar5;
      sVar5 = sVar3 + sVar14;
      sVar14 = sVar14 - sVar3;
      iVar9 = (int)psVar8[8] * (int)param_3[8] - (int)psVar8[0x38] * (int)param_3[0x38];
      iVar18 = (int)psVar8[8] * (int)param_3[8] + (int)psVar8[0x38] * (int)param_3[0x38];
      iVar23 = (int)psVar8[0x28] * (int)param_3[0x28] - (int)psVar8[0x18] * (int)param_3[0x18];
      iVar10 = (int)psVar8[0x18] * (int)param_3[0x18] + (int)psVar8[0x28] * (int)param_3[0x28];
      sVar7 = (short)iVar10 + (short)iVar18;
      sVar3 = (short)((uint)((iVar9 + iVar23) * 0x1d9) >> 8);
      sVar6 = (sVar3 - (short)((uint)(iVar23 * 0x29d) >> 8)) - sVar7;
      sVar4 = (short)((uint)((iVar18 - iVar10) * 0x16a) >> 8) - sVar6;
      sVar3 = (sVar3 - (short)((uint)(iVar9 * 0x115) >> 8)) - sVar4;
      *psVar8 = sVar13 + sVar7;
      psVar8[0x38] = sVar13 - sVar7;
      psVar8[8] = sVar5 + sVar6;
      psVar8[0x30] = sVar5 - sVar6;
      psVar8[0x10] = sVar14 + sVar4;
      psVar8[0x28] = sVar14 - sVar4;
      psVar8[0x18] = sVar20 + sVar3;
      psVar8[0x20] = sVar20 - sVar3;
    }
    param_3 = param_3 + 1;
    uVar12 = uVar12 + 1;
    param_1 = psVar8 + 1;
  } while (uVar12 < 8);
  psVar8 = psVar8 + -7;
  uVar12 = 0;
  iVar23 = 3;
  if (param_5 != 8) {
    iVar23 = 4;
  }
  iVar23 = 0x20 - iVar23;
  do {
    iVar18 = _DAT_fffd0ccc;
    iVar9 = *psVar8 + 0x1000 + (int)psVar8[4];
    iVar15 = (*psVar8 + 0x1000) - (int)psVar8[4];
    iVar19 = (int)psVar8[6] + (int)psVar8[2];
    iVar10 = (((int)psVar8[2] - (int)psVar8[6]) * 0x16a >> 8) - iVar19;
    iVar16 = iVar9 + iVar19;
    iVar9 = iVar9 - iVar19;
    iVar17 = iVar10 + iVar15;
    iVar15 = iVar15 - iVar10;
    iVar11 = (int)psVar8[1] - (int)psVar8[7];
    iVar21 = (int)psVar8[1] + (int)psVar8[7];
    iVar22 = (int)psVar8[5] - (int)psVar8[3];
    iVar10 = (int)psVar8[3] + (int)psVar8[5];
    iVar19 = iVar10 + iVar21;
    iVar24 = (iVar11 + iVar22) * 0x1d9 >> 8;
    iVar22 = (iVar24 - (iVar22 * 0x29d >> 8)) - iVar19;
    iVar21 = ((iVar21 - iVar10) * 0x16a >> 8) - iVar22;
    iVar10 = (iVar24 - (iVar11 * 0x115 >> 8)) - iVar21;
    bVar1 = *(byte *)(((uint)(iVar16 - iVar19) >> 5 & 0x3ff) + _DAT_fffd0ccc);
    *(ushort *)(param_4 - (0 << 0x20 - iVar23)) =
         (ushort)*(byte *)(((uint)(iVar16 + iVar19) >> 5 & 0x3ff) + _DAT_fffd0ccc);
    bVar2 = *(byte *)(((uint)(iVar17 + iVar22) >> 5 & 0x3ff) + iVar18);
    *(ushort *)(param_4 - (0xe << 0x20 - iVar23)) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar17 - iVar22) >> 5 & 0x3ff) + iVar18);
    *(ushort *)(param_4 - (2 << 0x20 - iVar23)) = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar15 + iVar21) >> 5 & 0x3ff) + iVar18);
    *(ushort *)(param_4 - (0xc << 0x20 - iVar23)) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar15 - iVar21) >> 5 & 0x3ff) + iVar18);
    *(ushort *)(param_4 - (4 << 0x20 - iVar23)) = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar9 + iVar10) >> 5 & 0x3ff) + iVar18);
    *(ushort *)(param_4 - (10 << 0x20 - iVar23)) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar9 - iVar10) >> 5 & 0x3ff) + iVar18);
    *(ushort *)(param_4 - (6 << 0x20 - iVar23)) = (ushort)bVar2;
    psVar8 = psVar8 + 8;
    *(ushort *)(param_4 - (8 << 0x20 - iVar23)) = (ushort)bVar1;
    param_4 = param_4 + 2;
    uVar12 = uVar12 + 1;
  } while (uVar12 < 8);
  return;
}

/* ==================================================================
 * idct_block_4_8
 * Purpose: Xtensa integer inverse DCT producing a horizontally reduced 4x8 output block without rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 00010d8c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_4_8(short *param_1,undefined4 param_2,short *param_3,ushort *param_4,int param_5)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short sVar7;
  short *psVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  short sVar13;
  short sVar14;
  int iVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  int iVar19;
  short sVar20;
  int iVar21;
  int iVar22;
  int iVar23;

  uVar12 = 0;
  do {
    psVar8 = param_1;
    if ((((((psVar8[0x20] == 0 && psVar8[8] == 0) && psVar8[0x28] == 0) && psVar8[0x10] == 0) &&
         psVar8[0x30] == 0) && psVar8[0x18] == 0) && psVar8[0x38] == 0) {
      sVar13 = *psVar8 * *param_3;
      *psVar8 = sVar13;
      psVar8[0x38] = sVar13;
      psVar8[8] = sVar13;
      psVar8[0x30] = sVar13;
      psVar8[0x10] = sVar13;
      psVar8[0x28] = sVar13;
      psVar8[0x18] = sVar13;
      psVar8[0x20] = sVar13;
    }
    else {
      sVar14 = *psVar8 * *param_3;
      iVar16 = (int)psVar8[0x10] * (int)param_3[0x10];
      sVar13 = psVar8[0x20] * param_3[0x20];
      iVar9 = (int)psVar8[0x30] * (int)param_3[0x30];
      sVar20 = sVar14 + sVar13;
      sVar14 = sVar14 - sVar13;
      sVar5 = (short)iVar9 + (short)iVar16;
      sVar3 = (short)((uint)((iVar16 - iVar9) * 0x16a) >> 8) - sVar5;
      sVar13 = sVar20 + sVar5;
      sVar20 = sVar20 - sVar5;
      sVar5 = sVar3 + sVar14;
      sVar14 = sVar14 - sVar3;
      iVar10 = (int)psVar8[8] * (int)param_3[8] - (int)psVar8[0x38] * (int)param_3[0x38];
      iVar9 = (int)psVar8[8] * (int)param_3[8] + (int)psVar8[0x38] * (int)param_3[0x38];
      iVar16 = (int)psVar8[0x28] * (int)param_3[0x28] - (int)psVar8[0x18] * (int)param_3[0x18];
      iVar19 = (int)psVar8[0x18] * (int)param_3[0x18] + (int)psVar8[0x28] * (int)param_3[0x28];
      sVar7 = (short)iVar19 + (short)iVar9;
      sVar3 = (short)((uint)((iVar10 + iVar16) * 0x1d9) >> 8);
      sVar6 = (sVar3 - (short)((uint)(iVar16 * 0x29d) >> 8)) - sVar7;
      sVar4 = (short)((uint)((iVar9 - iVar19) * 0x16a) >> 8) - sVar6;
      sVar3 = (sVar3 - (short)((uint)(iVar10 * 0x115) >> 8)) - sVar4;
      *psVar8 = sVar13 + sVar7;
      psVar8[0x38] = sVar13 - sVar7;
      psVar8[8] = sVar5 + sVar6;
      psVar8[0x30] = sVar5 - sVar6;
      psVar8[0x10] = sVar14 + sVar4;
      psVar8[0x28] = sVar14 - sVar4;
      psVar8[0x18] = sVar20 + sVar3;
      psVar8[0x20] = sVar20 - sVar3;
    }
    param_3 = param_3 + 1;
    uVar12 = uVar12 + 1;
    param_1 = psVar8 + 1;
  } while (uVar12 < 8);
  psVar8 = psVar8 + -7;
  uVar12 = 0;
  do {
    iVar16 = _DAT_fffd0f40;
    iVar9 = *psVar8 + 0x1000 + (int)psVar8[4];
    iVar15 = (*psVar8 + 0x1000) - (int)psVar8[4];
    iVar19 = (int)psVar8[6] + (int)psVar8[2];
    iVar10 = (((int)psVar8[2] - (int)psVar8[6]) * 0x16a >> 8) - iVar19;
    iVar17 = iVar9 + iVar19;
    iVar9 = iVar9 - iVar19;
    iVar18 = iVar10 + iVar15;
    iVar15 = iVar15 - iVar10;
    iVar11 = (int)psVar8[1] - (int)psVar8[7];
    iVar21 = (int)psVar8[1] + (int)psVar8[7];
    iVar22 = (int)psVar8[5] - (int)psVar8[3];
    iVar10 = (int)psVar8[3] + (int)psVar8[5];
    iVar19 = iVar10 + iVar21;
    iVar23 = (iVar11 + iVar22) * 0x1d9 >> 8;
    iVar22 = (iVar23 - (iVar22 * 0x29d >> 8)) - iVar19;
    iVar21 = ((iVar21 - iVar10) * 0x16a >> 8) - iVar22;
    iVar10 = (iVar23 - (iVar11 * 0x115 >> 8)) - iVar21;
    bVar1 = *(byte *)(((uint)(iVar17 - iVar19) >> 5 & 0x3ff) + _DAT_fffd0f40);
    *param_4 = (ushort)*(byte *)(((uint)(iVar17 + iVar19) >> 5 & 0x3ff) + _DAT_fffd0f40);
    bVar2 = *(byte *)(((uint)(iVar18 + iVar22) >> 5 & 0x3ff) + iVar16);
    param_4[7] = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar18 - iVar22) >> 5 & 0x3ff) + iVar16);
    param_4[1] = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar15 + iVar21) >> 5 & 0x3ff) + iVar16);
    param_4[6] = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar15 - iVar21) >> 5 & 0x3ff) + iVar16);
    param_4[2] = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar9 + iVar10) >> 5 & 0x3ff) + iVar16);
    param_4[5] = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar9 - iVar10) >> 5 & 0x3ff) + iVar16);
    param_4[3] = (ushort)bVar2;
    psVar8 = psVar8 + 0x10;
    param_4[4] = (ushort)bVar1;
    param_4 = param_4 + param_5;
    uVar12 = uVar12 + 2;
  } while (uVar12 < 8);
  return;
}

/* ==================================================================
 * idct_block_4_8_90
 * Purpose: Xtensa integer inverse DCT producing a horizontally reduced 4x8 output block with 90-degree rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 00010fbc
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_4_8_90(short *param_1,undefined4 param_2,short *param_3,int param_4,int param_5)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short sVar7;
  short *psVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  short sVar13;
  short sVar14;
  int iVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  int iVar19;
  short sVar20;
  int iVar21;
  int iVar22;
  int iVar23;
  int iVar24;

  param_4 = param_4 + 6;
  uVar12 = 0;
  do {
    psVar8 = param_1;
    if ((((((psVar8[0x20] == 0 && psVar8[8] == 0) && psVar8[0x28] == 0) && psVar8[0x10] == 0) &&
         psVar8[0x30] == 0) && psVar8[0x18] == 0) && psVar8[0x38] == 0) {
      sVar13 = *psVar8 * *param_3;
      *psVar8 = sVar13;
      psVar8[0x38] = sVar13;
      psVar8[8] = sVar13;
      psVar8[0x30] = sVar13;
      psVar8[0x10] = sVar13;
      psVar8[0x28] = sVar13;
      psVar8[0x18] = sVar13;
      psVar8[0x20] = sVar13;
    }
    else {
      sVar14 = *psVar8 * *param_3;
      iVar23 = (int)psVar8[0x10] * (int)param_3[0x10];
      sVar13 = psVar8[0x20] * param_3[0x20];
      iVar18 = (int)psVar8[0x30] * (int)param_3[0x30];
      sVar20 = sVar14 + sVar13;
      sVar14 = sVar14 - sVar13;
      sVar5 = (short)iVar18 + (short)iVar23;
      sVar3 = (short)((uint)((iVar23 - iVar18) * 0x16a) >> 8) - sVar5;
      sVar13 = sVar20 + sVar5;
      sVar20 = sVar20 - sVar5;
      sVar5 = sVar3 + sVar14;
      sVar14 = sVar14 - sVar3;
      iVar9 = (int)psVar8[8] * (int)param_3[8] - (int)psVar8[0x38] * (int)param_3[0x38];
      iVar18 = (int)psVar8[8] * (int)param_3[8] + (int)psVar8[0x38] * (int)param_3[0x38];
      iVar23 = (int)psVar8[0x28] * (int)param_3[0x28] - (int)psVar8[0x18] * (int)param_3[0x18];
      iVar10 = (int)psVar8[0x18] * (int)param_3[0x18] + (int)psVar8[0x28] * (int)param_3[0x28];
      sVar7 = (short)iVar10 + (short)iVar18;
      sVar3 = (short)((uint)((iVar9 + iVar23) * 0x1d9) >> 8);
      sVar6 = (sVar3 - (short)((uint)(iVar23 * 0x29d) >> 8)) - sVar7;
      sVar4 = (short)((uint)((iVar18 - iVar10) * 0x16a) >> 8) - sVar6;
      sVar3 = (sVar3 - (short)((uint)(iVar9 * 0x115) >> 8)) - sVar4;
      *psVar8 = sVar13 + sVar7;
      psVar8[0x38] = sVar13 - sVar7;
      psVar8[8] = sVar5 + sVar6;
      psVar8[0x30] = sVar5 - sVar6;
      psVar8[0x10] = sVar14 + sVar4;
      psVar8[0x28] = sVar14 - sVar4;
      psVar8[0x18] = sVar20 + sVar3;
      psVar8[0x20] = sVar20 - sVar3;
    }
    param_3 = param_3 + 1;
    uVar12 = uVar12 + 1;
    param_1 = psVar8 + 1;
  } while (uVar12 < 8);
  psVar8 = psVar8 + -7;
  uVar12 = 0;
  iVar23 = 3;
  if (param_5 != 8) {
    iVar23 = 4;
  }
  iVar23 = 0x20 - iVar23;
  do {
    iVar18 = _DAT_fffd1178;
    iVar9 = *psVar8 + 0x1000 + (int)psVar8[4];
    iVar15 = (*psVar8 + 0x1000) - (int)psVar8[4];
    iVar19 = (int)psVar8[6] + (int)psVar8[2];
    iVar10 = (((int)psVar8[2] - (int)psVar8[6]) * 0x16a >> 8) - iVar19;
    iVar16 = iVar9 + iVar19;
    iVar9 = iVar9 - iVar19;
    iVar17 = iVar10 + iVar15;
    iVar15 = iVar15 - iVar10;
    iVar11 = (int)psVar8[1] - (int)psVar8[7];
    iVar21 = (int)psVar8[1] + (int)psVar8[7];
    iVar22 = (int)psVar8[5] - (int)psVar8[3];
    iVar10 = (int)psVar8[3] + (int)psVar8[5];
    iVar19 = iVar10 + iVar21;
    iVar24 = (iVar11 + iVar22) * 0x1d9 >> 8;
    iVar22 = (iVar24 - (iVar22 * 0x29d >> 8)) - iVar19;
    iVar21 = ((iVar21 - iVar10) * 0x16a >> 8) - iVar22;
    iVar10 = (iVar24 - (iVar11 * 0x115 >> 8)) - iVar21;
    bVar1 = *(byte *)(((uint)(iVar16 - iVar19) >> 5 & 0x3ff) + _DAT_fffd1178);
    *(ushort *)((0 << 0x20 - iVar23) + param_4) =
         (ushort)*(byte *)(((uint)(iVar16 + iVar19) >> 5 & 0x3ff) + _DAT_fffd1178);
    bVar2 = *(byte *)(((uint)(iVar17 + iVar22) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((0xe << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar17 - iVar22) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((2 << 0x20 - iVar23) + param_4) = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar15 + iVar21) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((0xc << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar15 - iVar21) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((4 << 0x20 - iVar23) + param_4) = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar9 + iVar10) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((10 << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar9 - iVar10) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((6 << 0x20 - iVar23) + param_4) = (ushort)bVar2;
    psVar8 = psVar8 + 0x10;
    *(ushort *)((8 << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    param_4 = param_4 + -2;
    uVar12 = uVar12 + 2;
  } while (uVar12 < 8);
  return;
}

/* ==================================================================
 * idct_block_4_8_180
 * Purpose: Xtensa integer inverse DCT producing a horizontally reduced 4x8 output block with 180-degree rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 00011230
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_4_8_180(short *param_1,undefined4 param_2,short *param_3,int param_4,int param_5)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short sVar7;
  short *psVar8;
  ushort *puVar9;
  int iVar10;
  int iVar11;
  int iVar12;
  uint uVar13;
  short sVar14;
  short sVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  int iVar19;
  int iVar20;
  short sVar21;
  int iVar22;
  int iVar23;
  int iVar24;

  puVar9 = (ushort *)(param_4 + param_5 * 6);
  uVar13 = 0;
  do {
    psVar8 = param_1;
    if ((((((psVar8[0x20] == 0 && psVar8[8] == 0) && psVar8[0x28] == 0) && psVar8[0x10] == 0) &&
         psVar8[0x30] == 0) && psVar8[0x18] == 0) && psVar8[0x38] == 0) {
      sVar14 = *psVar8 * *param_3;
      *psVar8 = sVar14;
      psVar8[0x38] = sVar14;
      psVar8[8] = sVar14;
      psVar8[0x30] = sVar14;
      psVar8[0x10] = sVar14;
      psVar8[0x28] = sVar14;
      psVar8[0x18] = sVar14;
      psVar8[0x20] = sVar14;
    }
    else {
      sVar15 = *psVar8 * *param_3;
      iVar17 = (int)psVar8[0x10] * (int)param_3[0x10];
      sVar14 = psVar8[0x20] * param_3[0x20];
      iVar10 = (int)psVar8[0x30] * (int)param_3[0x30];
      sVar21 = sVar15 + sVar14;
      sVar15 = sVar15 - sVar14;
      sVar5 = (short)iVar10 + (short)iVar17;
      sVar3 = (short)((uint)((iVar17 - iVar10) * 0x16a) >> 8) - sVar5;
      sVar14 = sVar21 + sVar5;
      sVar21 = sVar21 - sVar5;
      sVar5 = sVar3 + sVar15;
      sVar15 = sVar15 - sVar3;
      iVar11 = (int)psVar8[8] * (int)param_3[8] - (int)psVar8[0x38] * (int)param_3[0x38];
      iVar10 = (int)psVar8[8] * (int)param_3[8] + (int)psVar8[0x38] * (int)param_3[0x38];
      iVar17 = (int)psVar8[0x28] * (int)param_3[0x28] - (int)psVar8[0x18] * (int)param_3[0x18];
      iVar20 = (int)psVar8[0x18] * (int)param_3[0x18] + (int)psVar8[0x28] * (int)param_3[0x28];
      sVar7 = (short)iVar20 + (short)iVar10;
      sVar3 = (short)((uint)((iVar11 + iVar17) * 0x1d9) >> 8);
      sVar6 = (sVar3 - (short)((uint)(iVar17 * 0x29d) >> 8)) - sVar7;
      sVar4 = (short)((uint)((iVar10 - iVar20) * 0x16a) >> 8) - sVar6;
      sVar3 = (sVar3 - (short)((uint)(iVar11 * 0x115) >> 8)) - sVar4;
      *psVar8 = sVar14 + sVar7;
      psVar8[0x38] = sVar14 - sVar7;
      psVar8[8] = sVar5 + sVar6;
      psVar8[0x30] = sVar5 - sVar6;
      psVar8[0x10] = sVar15 + sVar4;
      psVar8[0x28] = sVar15 - sVar4;
      psVar8[0x18] = sVar21 + sVar3;
      psVar8[0x20] = sVar21 - sVar3;
    }
    param_3 = param_3 + 1;
    uVar13 = uVar13 + 1;
    param_1 = psVar8 + 1;
  } while (uVar13 < 8);
  psVar8 = psVar8 + -7;
  uVar13 = 0;
  do {
    iVar17 = _DAT_fffd13e8;
    iVar10 = *psVar8 + 0x1000 + (int)psVar8[4];
    iVar16 = (*psVar8 + 0x1000) - (int)psVar8[4];
    iVar20 = (int)psVar8[6] + (int)psVar8[2];
    iVar11 = (((int)psVar8[2] - (int)psVar8[6]) * 0x16a >> 8) - iVar20;
    iVar18 = iVar10 + iVar20;
    iVar10 = iVar10 - iVar20;
    iVar19 = iVar11 + iVar16;
    iVar16 = iVar16 - iVar11;
    iVar12 = (int)psVar8[1] - (int)psVar8[7];
    iVar22 = (int)psVar8[1] + (int)psVar8[7];
    iVar23 = (int)psVar8[5] - (int)psVar8[3];
    iVar11 = (int)psVar8[3] + (int)psVar8[5];
    iVar20 = iVar11 + iVar22;
    iVar24 = (iVar12 + iVar23) * 0x1d9 >> 8;
    iVar23 = (iVar24 - (iVar23 * 0x29d >> 8)) - iVar20;
    iVar22 = ((iVar22 - iVar11) * 0x16a >> 8) - iVar23;
    iVar11 = (iVar24 - (iVar12 * 0x115 >> 8)) - iVar22;
    bVar1 = *(byte *)(((uint)(iVar18 - iVar20) >> 5 & 0x3ff) + _DAT_fffd13e8);
    puVar9[7] = (ushort)*(byte *)(((uint)(iVar18 + iVar20) >> 5 & 0x3ff) + _DAT_fffd13e8);
    bVar2 = *(byte *)(((uint)(iVar19 + iVar23) >> 5 & 0x3ff) + iVar17);
    *puVar9 = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar19 - iVar23) >> 5 & 0x3ff) + iVar17);
    puVar9[6] = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar16 + iVar22) >> 5 & 0x3ff) + iVar17);
    puVar9[1] = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar16 - iVar22) >> 5 & 0x3ff) + iVar17);
    puVar9[5] = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar10 + iVar11) >> 5 & 0x3ff) + iVar17);
    puVar9[2] = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar10 - iVar11) >> 5 & 0x3ff) + iVar17);
    puVar9[4] = (ushort)bVar2;
    psVar8 = psVar8 + 0x10;
    puVar9[3] = (ushort)bVar1;
    puVar9 = puVar9 + -param_5;
    uVar13 = uVar13 + 2;
  } while (uVar13 < 8);
  return;
}

/* ==================================================================
 * idct_block_4_8_270
 * Purpose: Xtensa integer inverse DCT producing a horizontally reduced 4x8 output block with 270-degree rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 00011468
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_4_8_270(short *param_1,undefined4 param_2,short *param_3,int param_4,int param_5)

{
  byte bVar1;
  byte bVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short sVar7;
  short *psVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  uint uVar12;
  short sVar13;
  short sVar14;
  int iVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  int iVar19;
  short sVar20;
  int iVar21;
  int iVar22;
  int iVar23;
  int iVar24;

  uVar12 = 0;
  do {
    psVar8 = param_1;
    if ((((((psVar8[0x20] == 0 && psVar8[8] == 0) && psVar8[0x28] == 0) && psVar8[0x10] == 0) &&
         psVar8[0x30] == 0) && psVar8[0x18] == 0) && psVar8[0x38] == 0) {
      sVar13 = *psVar8 * *param_3;
      *psVar8 = sVar13;
      psVar8[0x38] = sVar13;
      psVar8[8] = sVar13;
      psVar8[0x30] = sVar13;
      psVar8[0x10] = sVar13;
      psVar8[0x28] = sVar13;
      psVar8[0x18] = sVar13;
      psVar8[0x20] = sVar13;
    }
    else {
      sVar14 = *psVar8 * *param_3;
      iVar23 = (int)psVar8[0x10] * (int)param_3[0x10];
      sVar13 = psVar8[0x20] * param_3[0x20];
      iVar18 = (int)psVar8[0x30] * (int)param_3[0x30];
      sVar20 = sVar14 + sVar13;
      sVar14 = sVar14 - sVar13;
      sVar5 = (short)iVar18 + (short)iVar23;
      sVar3 = (short)((uint)((iVar23 - iVar18) * 0x16a) >> 8) - sVar5;
      sVar13 = sVar20 + sVar5;
      sVar20 = sVar20 - sVar5;
      sVar5 = sVar3 + sVar14;
      sVar14 = sVar14 - sVar3;
      iVar9 = (int)psVar8[8] * (int)param_3[8] - (int)psVar8[0x38] * (int)param_3[0x38];
      iVar18 = (int)psVar8[8] * (int)param_3[8] + (int)psVar8[0x38] * (int)param_3[0x38];
      iVar23 = (int)psVar8[0x28] * (int)param_3[0x28] - (int)psVar8[0x18] * (int)param_3[0x18];
      iVar10 = (int)psVar8[0x18] * (int)param_3[0x18] + (int)psVar8[0x28] * (int)param_3[0x28];
      sVar7 = (short)iVar10 + (short)iVar18;
      sVar3 = (short)((uint)((iVar9 + iVar23) * 0x1d9) >> 8);
      sVar6 = (sVar3 - (short)((uint)(iVar23 * 0x29d) >> 8)) - sVar7;
      sVar4 = (short)((uint)((iVar18 - iVar10) * 0x16a) >> 8) - sVar6;
      sVar3 = (sVar3 - (short)((uint)(iVar9 * 0x115) >> 8)) - sVar4;
      *psVar8 = sVar13 + sVar7;
      psVar8[0x38] = sVar13 - sVar7;
      psVar8[8] = sVar5 + sVar6;
      psVar8[0x30] = sVar5 - sVar6;
      psVar8[0x10] = sVar14 + sVar4;
      psVar8[0x28] = sVar14 - sVar4;
      psVar8[0x18] = sVar20 + sVar3;
      psVar8[0x20] = sVar20 - sVar3;
    }
    param_3 = param_3 + 1;
    uVar12 = uVar12 + 1;
    param_1 = psVar8 + 1;
  } while (uVar12 < 8);
  psVar8 = psVar8 + -7;
  uVar12 = 0;
  iVar23 = 3;
  if (param_5 != 8) {
    iVar23 = 4;
  }
  iVar23 = 0x20 - iVar23;
  do {
    iVar18 = _DAT_fffd1624;
    iVar9 = *psVar8 + 0x1000 + (int)psVar8[4];
    iVar15 = (*psVar8 + 0x1000) - (int)psVar8[4];
    iVar19 = (int)psVar8[6] + (int)psVar8[2];
    iVar10 = (((int)psVar8[2] - (int)psVar8[6]) * 0x16a >> 8) - iVar19;
    iVar16 = iVar9 + iVar19;
    iVar9 = iVar9 - iVar19;
    iVar17 = iVar10 + iVar15;
    iVar15 = iVar15 - iVar10;
    iVar11 = (int)psVar8[1] - (int)psVar8[7];
    iVar21 = (int)psVar8[1] + (int)psVar8[7];
    iVar22 = (int)psVar8[5] - (int)psVar8[3];
    iVar10 = (int)psVar8[3] + (int)psVar8[5];
    iVar19 = iVar10 + iVar21;
    iVar24 = (iVar11 + iVar22) * 0x1d9 >> 8;
    iVar22 = (iVar24 - (iVar22 * 0x29d >> 8)) - iVar19;
    iVar21 = ((iVar21 - iVar10) * 0x16a >> 8) - iVar22;
    iVar10 = (iVar24 - (iVar11 * 0x115 >> 8)) - iVar21;
    bVar1 = *(byte *)(((uint)(iVar16 - iVar19) >> 5 & 0x3ff) + _DAT_fffd1624);
    *(ushort *)((0xe << 0x20 - iVar23) + param_4) =
         (ushort)*(byte *)(((uint)(iVar16 + iVar19) >> 5 & 0x3ff) + _DAT_fffd1624);
    bVar2 = *(byte *)(((uint)(iVar17 + iVar22) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((0 << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar17 - iVar22) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((0xc << 0x20 - iVar23) + param_4) = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar15 + iVar21) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((2 << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar15 - iVar21) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((10 << 0x20 - iVar23) + param_4) = (ushort)bVar2;
    bVar2 = *(byte *)(((uint)(iVar9 + iVar10) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((4 << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    bVar1 = *(byte *)(((uint)(iVar9 - iVar10) >> 5 & 0x3ff) + iVar18);
    *(ushort *)((8 << 0x20 - iVar23) + param_4) = (ushort)bVar2;
    psVar8 = psVar8 + 0x10;
    *(ushort *)((6 << 0x20 - iVar23) + param_4) = (ushort)bVar1;
    param_4 = param_4 + 2;
    uVar12 = uVar12 + 2;
  } while (uVar12 < 8);
  return;
}

/* ==================================================================
 * idct_block_4_4
 * Purpose: Xtensa integer inverse DCT producing a half-resolution 4x4 output block without rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 000116dc
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_4_4(short *param_1,undefined4 param_2,short *param_3,ushort *param_4,int param_5)

{
  byte bVar1;
  short sVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short *psVar7;
  int iVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  int iVar12;
  uint uVar13;
  short sVar14;
  short sVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  short sVar19;
  int iVar20;
  int iVar21;
  int iVar22;

  uVar13 = 0;
  do {
    psVar7 = param_1;
    if ((((((psVar7[0x20] == 0 && psVar7[8] == 0) && psVar7[0x28] == 0) && psVar7[0x10] == 0) &&
         psVar7[0x30] == 0) && psVar7[0x18] == 0) && psVar7[0x38] == 0) {
      sVar14 = *psVar7 * *param_3;
      *psVar7 = sVar14;
      psVar7[0x38] = sVar14;
      psVar7[8] = sVar14;
      psVar7[0x30] = sVar14;
      psVar7[0x10] = sVar14;
      psVar7[0x28] = sVar14;
      psVar7[0x18] = sVar14;
      psVar7[0x20] = sVar14;
    }
    else {
      sVar15 = *psVar7 * *param_3;
      iVar17 = (int)psVar7[0x10] * (int)param_3[0x10];
      sVar14 = psVar7[0x20] * param_3[0x20];
      iVar8 = (int)psVar7[0x30] * (int)param_3[0x30];
      sVar19 = sVar15 + sVar14;
      sVar15 = sVar15 - sVar14;
      sVar4 = (short)iVar8 + (short)iVar17;
      sVar2 = (short)((uint)((iVar17 - iVar8) * 0x16a) >> 8) - sVar4;
      sVar14 = sVar19 + sVar4;
      sVar19 = sVar19 - sVar4;
      sVar4 = sVar2 + sVar15;
      sVar15 = sVar15 - sVar2;
      iVar9 = (int)psVar7[8] * (int)param_3[8] - (int)psVar7[0x38] * (int)param_3[0x38];
      iVar8 = (int)psVar7[8] * (int)param_3[8] + (int)psVar7[0x38] * (int)param_3[0x38];
      iVar17 = (int)psVar7[0x28] * (int)param_3[0x28] - (int)psVar7[0x18] * (int)param_3[0x18];
      iVar10 = (int)psVar7[0x18] * (int)param_3[0x18] + (int)psVar7[0x28] * (int)param_3[0x28];
      sVar6 = (short)iVar10 + (short)iVar8;
      sVar2 = (short)((uint)((iVar9 + iVar17) * 0x1d9) >> 8);
      sVar5 = (sVar2 - (short)((uint)(iVar17 * 0x29d) >> 8)) - sVar6;
      sVar3 = (short)((uint)((iVar8 - iVar10) * 0x16a) >> 8) - sVar5;
      sVar2 = (sVar2 - (short)((uint)(iVar9 * 0x115) >> 8)) - sVar3;
      *psVar7 = sVar14 + sVar6;
      psVar7[0x38] = sVar14 - sVar6;
      psVar7[8] = sVar4 + sVar5;
      psVar7[0x30] = sVar4 - sVar5;
      psVar7[0x10] = sVar15 + sVar3;
      psVar7[0x28] = sVar15 - sVar3;
      psVar7[0x18] = sVar19 + sVar2;
      psVar7[0x20] = sVar19 - sVar2;
    }
    param_3 = param_3 + 1;
    uVar13 = uVar13 + 1;
    param_1 = psVar7 + 1;
  } while (uVar13 < 8);
  psVar7 = psVar7 + -7;
  uVar13 = 0;
  do {
    iVar17 = _DAT_fffd1890;
    iVar8 = *psVar7 + 0x1000 + (int)psVar7[4];
    iVar16 = (*psVar7 + 0x1000) - (int)psVar7[4];
    iVar18 = (int)psVar7[6] + (int)psVar7[2];
    iVar11 = (((int)psVar7[2] - (int)psVar7[6]) * 0x16a >> 8) - iVar18;
    iVar12 = (int)psVar7[1] - (int)psVar7[7];
    iVar20 = (int)psVar7[1] + (int)psVar7[7];
    iVar21 = (int)psVar7[5] - (int)psVar7[3];
    iVar9 = (int)psVar7[3] + (int)psVar7[5];
    iVar10 = iVar9 + iVar20;
    iVar22 = (iVar12 + iVar21) * 0x1d9 >> 8;
    iVar21 = (iVar22 - (iVar21 * 0x29d >> 8)) - iVar10;
    iVar9 = ((iVar20 - iVar9) * 0x16a >> 8) - iVar21;
    *param_4 = (ushort)*(byte *)(((uint)(iVar8 + iVar18 + iVar10) >> 5 & 0x3ff) + _DAT_fffd1890);
    bVar1 = *(byte *)(((uint)((iVar16 - iVar11) + iVar9) >> 5 & 0x3ff) + iVar17);
    param_4[3] = (ushort)*(byte *)(((uint)((iVar11 + iVar16) - iVar21) >> 5 & 0x3ff) + iVar17);
    param_4[1] = (ushort)bVar1;
    psVar7 = psVar7 + 0x10;
    param_4[2] = (ushort)*(byte *)(((uint)((iVar8 - iVar18) -
                                          ((iVar22 - (iVar12 * 0x115 >> 8)) - iVar9)) >> 5 & 0x3ff)
                                  + iVar17);
    param_4 = param_4 + param_5;
    uVar13 = uVar13 + 2;
  } while (uVar13 < 8);
  return;
}

/* ==================================================================
 * idct_block_4_4_90
 * Purpose: Xtensa integer inverse DCT producing a half-resolution 4x4 output block with 90-degree rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 000118d8
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_4_4_90(short *param_1,undefined4 param_2,short *param_3,int param_4,int param_5)

{
  byte bVar1;
  short sVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short *psVar7;
  int iVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  int iVar12;
  uint uVar13;
  short sVar14;
  short sVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  short sVar19;
  int iVar20;
  int iVar21;
  int iVar22;
  int iVar23;

  param_4 = param_4 + 6;
  uVar13 = 0;
  do {
    psVar7 = param_1;
    if ((((((psVar7[0x20] == 0 && psVar7[8] == 0) && psVar7[0x28] == 0) && psVar7[0x10] == 0) &&
         psVar7[0x30] == 0) && psVar7[0x18] == 0) && psVar7[0x38] == 0) {
      sVar14 = *psVar7 * *param_3;
      *psVar7 = sVar14;
      psVar7[0x38] = sVar14;
      psVar7[8] = sVar14;
      psVar7[0x30] = sVar14;
      psVar7[0x10] = sVar14;
      psVar7[0x28] = sVar14;
      psVar7[0x18] = sVar14;
      psVar7[0x20] = sVar14;
    }
    else {
      sVar15 = *psVar7 * *param_3;
      iVar22 = (int)psVar7[0x10] * (int)param_3[0x10];
      sVar14 = psVar7[0x20] * param_3[0x20];
      iVar17 = (int)psVar7[0x30] * (int)param_3[0x30];
      sVar19 = sVar15 + sVar14;
      sVar15 = sVar15 - sVar14;
      sVar4 = (short)iVar17 + (short)iVar22;
      sVar2 = (short)((uint)((iVar22 - iVar17) * 0x16a) >> 8) - sVar4;
      sVar14 = sVar19 + sVar4;
      sVar19 = sVar19 - sVar4;
      sVar4 = sVar2 + sVar15;
      sVar15 = sVar15 - sVar2;
      iVar8 = (int)psVar7[8] * (int)param_3[8] - (int)psVar7[0x38] * (int)param_3[0x38];
      iVar17 = (int)psVar7[8] * (int)param_3[8] + (int)psVar7[0x38] * (int)param_3[0x38];
      iVar22 = (int)psVar7[0x28] * (int)param_3[0x28] - (int)psVar7[0x18] * (int)param_3[0x18];
      iVar9 = (int)psVar7[0x18] * (int)param_3[0x18] + (int)psVar7[0x28] * (int)param_3[0x28];
      sVar6 = (short)iVar9 + (short)iVar17;
      sVar2 = (short)((uint)((iVar8 + iVar22) * 0x1d9) >> 8);
      sVar5 = (sVar2 - (short)((uint)(iVar22 * 0x29d) >> 8)) - sVar6;
      sVar3 = (short)((uint)((iVar17 - iVar9) * 0x16a) >> 8) - sVar5;
      sVar2 = (sVar2 - (short)((uint)(iVar8 * 0x115) >> 8)) - sVar3;
      *psVar7 = sVar14 + sVar6;
      psVar7[0x38] = sVar14 - sVar6;
      psVar7[8] = sVar4 + sVar5;
      psVar7[0x30] = sVar4 - sVar5;
      psVar7[0x10] = sVar15 + sVar3;
      psVar7[0x28] = sVar15 - sVar3;
      psVar7[0x18] = sVar19 + sVar2;
      psVar7[0x20] = sVar19 - sVar2;
    }
    param_3 = param_3 + 1;
    uVar13 = uVar13 + 1;
    param_1 = psVar7 + 1;
  } while (uVar13 < 8);
  psVar7 = psVar7 + -7;
  uVar13 = 0;
  iVar22 = 3;
  if (param_5 != 8) {
    iVar22 = 4;
  }
  iVar22 = 0x20 - iVar22;
  do {
    iVar17 = _DAT_fffd1a94;
    iVar8 = *psVar7 + 0x1000 + (int)psVar7[4];
    iVar16 = (*psVar7 + 0x1000) - (int)psVar7[4];
    iVar18 = (int)psVar7[6] + (int)psVar7[2];
    iVar11 = (((int)psVar7[2] - (int)psVar7[6]) * 0x16a >> 8) - iVar18;
    iVar12 = (int)psVar7[1] - (int)psVar7[7];
    iVar20 = (int)psVar7[1] + (int)psVar7[7];
    iVar21 = (int)psVar7[5] - (int)psVar7[3];
    iVar9 = (int)psVar7[3] + (int)psVar7[5];
    iVar10 = iVar9 + iVar20;
    iVar23 = (iVar12 + iVar21) * 0x1d9 >> 8;
    iVar21 = (iVar23 - (iVar21 * 0x29d >> 8)) - iVar10;
    iVar9 = ((iVar20 - iVar9) * 0x16a >> 8) - iVar21;
    *(ushort *)((0 << 0x20 - iVar22) + param_4) =
         (ushort)*(byte *)(((uint)(iVar8 + iVar18 + iVar10) >> 5 & 0x3ff) + _DAT_fffd1a94);
    bVar1 = *(byte *)(((uint)((iVar16 - iVar11) + iVar9) >> 5 & 0x3ff) + iVar17);
    *(ushort *)((6 << 0x20 - iVar22) + param_4) =
         (ushort)*(byte *)(((uint)((iVar11 + iVar16) - iVar21) >> 5 & 0x3ff) + iVar17);
    *(ushort *)((2 << 0x20 - iVar22) + param_4) = (ushort)bVar1;
    psVar7 = psVar7 + 0x10;
    *(ushort *)((4 << 0x20 - iVar22) + param_4) =
         (ushort)*(byte *)(((uint)((iVar8 - iVar18) - ((iVar23 - (iVar12 * 0x115 >> 8)) - iVar9)) >>
                            5 & 0x3ff) + iVar17);
    param_4 = param_4 + -2;
    uVar13 = uVar13 + 2;
  } while (uVar13 < 8);
  return;
}

/* ==================================================================
 * idct_block_4_4_180
 * Purpose: Xtensa integer inverse DCT producing a half-resolution 4x4 output block with 180-degree rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 00011afc
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_4_4_180(short *param_1,undefined4 param_2,short *param_3,int param_4,int param_5)

{
  byte bVar1;
  short sVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short *psVar7;
  ushort *puVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  int iVar12;
  int iVar13;
  uint uVar14;
  short sVar15;
  short sVar16;
  int iVar17;
  int iVar18;
  int iVar19;
  short sVar20;
  int iVar21;
  int iVar22;
  int iVar23;

  puVar8 = (ushort *)(param_4 + param_5 * 6);
  uVar14 = 0;
  do {
    psVar7 = param_1;
    if ((((((psVar7[0x20] == 0 && psVar7[8] == 0) && psVar7[0x28] == 0) && psVar7[0x10] == 0) &&
         psVar7[0x30] == 0) && psVar7[0x18] == 0) && psVar7[0x38] == 0) {
      sVar15 = *psVar7 * *param_3;
      *psVar7 = sVar15;
      psVar7[0x38] = sVar15;
      psVar7[8] = sVar15;
      psVar7[0x30] = sVar15;
      psVar7[0x10] = sVar15;
      psVar7[0x28] = sVar15;
      psVar7[0x18] = sVar15;
      psVar7[0x20] = sVar15;
    }
    else {
      sVar16 = *psVar7 * *param_3;
      iVar18 = (int)psVar7[0x10] * (int)param_3[0x10];
      sVar15 = psVar7[0x20] * param_3[0x20];
      iVar9 = (int)psVar7[0x30] * (int)param_3[0x30];
      sVar20 = sVar16 + sVar15;
      sVar16 = sVar16 - sVar15;
      sVar4 = (short)iVar9 + (short)iVar18;
      sVar2 = (short)((uint)((iVar18 - iVar9) * 0x16a) >> 8) - sVar4;
      sVar15 = sVar20 + sVar4;
      sVar20 = sVar20 - sVar4;
      sVar4 = sVar2 + sVar16;
      sVar16 = sVar16 - sVar2;
      iVar10 = (int)psVar7[8] * (int)param_3[8] - (int)psVar7[0x38] * (int)param_3[0x38];
      iVar9 = (int)psVar7[8] * (int)param_3[8] + (int)psVar7[0x38] * (int)param_3[0x38];
      iVar18 = (int)psVar7[0x28] * (int)param_3[0x28] - (int)psVar7[0x18] * (int)param_3[0x18];
      iVar11 = (int)psVar7[0x18] * (int)param_3[0x18] + (int)psVar7[0x28] * (int)param_3[0x28];
      sVar6 = (short)iVar11 + (short)iVar9;
      sVar2 = (short)((uint)((iVar10 + iVar18) * 0x1d9) >> 8);
      sVar5 = (sVar2 - (short)((uint)(iVar18 * 0x29d) >> 8)) - sVar6;
      sVar3 = (short)((uint)((iVar9 - iVar11) * 0x16a) >> 8) - sVar5;
      sVar2 = (sVar2 - (short)((uint)(iVar10 * 0x115) >> 8)) - sVar3;
      *psVar7 = sVar15 + sVar6;
      psVar7[0x38] = sVar15 - sVar6;
      psVar7[8] = sVar4 + sVar5;
      psVar7[0x30] = sVar4 - sVar5;
      psVar7[0x10] = sVar16 + sVar3;
      psVar7[0x28] = sVar16 - sVar3;
      psVar7[0x18] = sVar20 + sVar2;
      psVar7[0x20] = sVar20 - sVar2;
    }
    param_3 = param_3 + 1;
    uVar14 = uVar14 + 1;
    param_1 = psVar7 + 1;
  } while (uVar14 < 8);
  psVar7 = psVar7 + -7;
  uVar14 = 0;
  do {
    iVar18 = _DAT_fffd1cb4;
    iVar9 = *psVar7 + 0x1000 + (int)psVar7[4];
    iVar17 = (*psVar7 + 0x1000) - (int)psVar7[4];
    iVar19 = (int)psVar7[6] + (int)psVar7[2];
    iVar12 = (((int)psVar7[2] - (int)psVar7[6]) * 0x16a >> 8) - iVar19;
    iVar13 = (int)psVar7[1] - (int)psVar7[7];
    iVar21 = (int)psVar7[1] + (int)psVar7[7];
    iVar22 = (int)psVar7[5] - (int)psVar7[3];
    iVar10 = (int)psVar7[3] + (int)psVar7[5];
    iVar11 = iVar10 + iVar21;
    iVar23 = (iVar13 + iVar22) * 0x1d9 >> 8;
    iVar22 = (iVar23 - (iVar22 * 0x29d >> 8)) - iVar11;
    iVar10 = ((iVar21 - iVar10) * 0x16a >> 8) - iVar22;
    puVar8[3] = (ushort)*(byte *)(((uint)(iVar9 + iVar19 + iVar11) >> 5 & 0x3ff) + _DAT_fffd1cb4);
    bVar1 = *(byte *)(((uint)((iVar17 - iVar12) + iVar10) >> 5 & 0x3ff) + iVar18);
    *puVar8 = (ushort)*(byte *)(((uint)((iVar12 + iVar17) - iVar22) >> 5 & 0x3ff) + iVar18);
    puVar8[2] = (ushort)bVar1;
    psVar7 = psVar7 + 0x10;
    puVar8[1] = (ushort)*(byte *)(((uint)((iVar9 - iVar19) -
                                         ((iVar23 - (iVar13 * 0x115 >> 8)) - iVar10)) >> 5 & 0x3ff)
                                 + iVar18);
    puVar8 = puVar8 + -param_5;
    uVar14 = uVar14 + 2;
  } while (uVar14 < 8);
  return;
}

/* ==================================================================
 * idct_block_4_4_270
 * Purpose: Xtensa integer inverse DCT producing a half-resolution 4x4 output block with 270-degree rotation; includes the all-zero-AC column shortcut and sample clipping.
 * Entry: 00011d00
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void idct_block_4_4_270(short *param_1,undefined4 param_2,short *param_3,int param_4,int param_5)

{
  byte bVar1;
  short sVar2;
  short sVar3;
  short sVar4;
  short sVar5;
  short sVar6;
  short *psVar7;
  int iVar8;
  int iVar9;
  int iVar10;
  int iVar11;
  int iVar12;
  uint uVar13;
  short sVar14;
  short sVar15;
  int iVar16;
  int iVar17;
  int iVar18;
  short sVar19;
  int iVar20;
  int iVar21;
  int iVar22;
  int iVar23;

  uVar13 = 0;
  do {
    psVar7 = param_1;
    if ((((((psVar7[0x20] == 0 && psVar7[8] == 0) && psVar7[0x28] == 0) && psVar7[0x10] == 0) &&
         psVar7[0x30] == 0) && psVar7[0x18] == 0) && psVar7[0x38] == 0) {
      sVar14 = *psVar7 * *param_3;
      *psVar7 = sVar14;
      psVar7[0x38] = sVar14;
      psVar7[8] = sVar14;
      psVar7[0x30] = sVar14;
      psVar7[0x10] = sVar14;
      psVar7[0x28] = sVar14;
      psVar7[0x18] = sVar14;
      psVar7[0x20] = sVar14;
    }
    else {
      sVar15 = *psVar7 * *param_3;
      iVar22 = (int)psVar7[0x10] * (int)param_3[0x10];
      sVar14 = psVar7[0x20] * param_3[0x20];
      iVar17 = (int)psVar7[0x30] * (int)param_3[0x30];
      sVar19 = sVar15 + sVar14;
      sVar15 = sVar15 - sVar14;
      sVar4 = (short)iVar17 + (short)iVar22;
      sVar2 = (short)((uint)((iVar22 - iVar17) * 0x16a) >> 8) - sVar4;
      sVar14 = sVar19 + sVar4;
      sVar19 = sVar19 - sVar4;
      sVar4 = sVar2 + sVar15;
      sVar15 = sVar15 - sVar2;
      iVar8 = (int)psVar7[8] * (int)param_3[8] - (int)psVar7[0x38] * (int)param_3[0x38];
      iVar17 = (int)psVar7[8] * (int)param_3[8] + (int)psVar7[0x38] * (int)param_3[0x38];
      iVar22 = (int)psVar7[0x28] * (int)param_3[0x28] - (int)psVar7[0x18] * (int)param_3[0x18];
      iVar9 = (int)psVar7[0x18] * (int)param_3[0x18] + (int)psVar7[0x28] * (int)param_3[0x28];
      sVar6 = (short)iVar9 + (short)iVar17;
      sVar2 = (short)((uint)((iVar8 + iVar22) * 0x1d9) >> 8);
      sVar5 = (sVar2 - (short)((uint)(iVar22 * 0x29d) >> 8)) - sVar6;
      sVar3 = (short)((uint)((iVar17 - iVar9) * 0x16a) >> 8) - sVar5;
      sVar2 = (sVar2 - (short)((uint)(iVar8 * 0x115) >> 8)) - sVar3;
      *psVar7 = sVar14 + sVar6;
      psVar7[0x38] = sVar14 - sVar6;
      psVar7[8] = sVar4 + sVar5;
      psVar7[0x30] = sVar4 - sVar5;
      psVar7[0x10] = sVar15 + sVar3;
      psVar7[0x28] = sVar15 - sVar3;
      psVar7[0x18] = sVar19 + sVar2;
      psVar7[0x20] = sVar19 - sVar2;
    }
    param_3 = param_3 + 1;
    uVar13 = uVar13 + 1;
    param_1 = psVar7 + 1;
  } while (uVar13 < 8);
  psVar7 = psVar7 + -7;
  uVar13 = 0;
  iVar22 = 3;
  if (param_5 != 8) {
    iVar22 = 4;
  }
  iVar22 = 0x20 - iVar22;
  do {
    iVar17 = _DAT_fffd1ebc;
    iVar8 = *psVar7 + 0x1000 + (int)psVar7[4];
    iVar16 = (*psVar7 + 0x1000) - (int)psVar7[4];
    iVar18 = (int)psVar7[6] + (int)psVar7[2];
    iVar11 = (((int)psVar7[2] - (int)psVar7[6]) * 0x16a >> 8) - iVar18;
    iVar12 = (int)psVar7[1] - (int)psVar7[7];
    iVar20 = (int)psVar7[1] + (int)psVar7[7];
    iVar21 = (int)psVar7[5] - (int)psVar7[3];
    iVar9 = (int)psVar7[3] + (int)psVar7[5];
    iVar10 = iVar9 + iVar20;
    iVar23 = (iVar12 + iVar21) * 0x1d9 >> 8;
    iVar21 = (iVar23 - (iVar21 * 0x29d >> 8)) - iVar10;
    iVar9 = ((iVar20 - iVar9) * 0x16a >> 8) - iVar21;
    *(ushort *)((6 << 0x20 - iVar22) + param_4) =
         (ushort)*(byte *)(((uint)(iVar8 + iVar18 + iVar10) >> 5 & 0x3ff) + _DAT_fffd1ebc);
    bVar1 = *(byte *)(((uint)((iVar16 - iVar11) + iVar9) >> 5 & 0x3ff) + iVar17);
    *(ushort *)((0 << 0x20 - iVar22) + param_4) =
         (ushort)*(byte *)(((uint)((iVar11 + iVar16) - iVar21) >> 5 & 0x3ff) + iVar17);
    *(ushort *)((4 << 0x20 - iVar22) + param_4) = (ushort)bVar1;
    psVar7 = psVar7 + 0x10;
    *(ushort *)((2 << 0x20 - iVar22) + param_4) =
         (ushort)*(byte *)(((uint)((iVar8 - iVar18) - ((iVar23 - (iVar12 * 0x115 >> 8)) - iVar9)) >>
                            5 & 0x3ff) + iVar17);
    param_4 = param_4 + 2;
    uVar13 = uVar13 + 2;
  } while (uVar13 < 8);
  return;
}
