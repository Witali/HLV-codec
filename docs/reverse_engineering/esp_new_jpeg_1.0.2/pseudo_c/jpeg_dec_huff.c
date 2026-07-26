/* ESP_NEW_JPEG 1.0.2 ESP32 decoder Ghidra pseudo-C.
 * Derived from the Espressif binary; see LICENSE.ESPRESSIF in the analysis directory.
 * Program: jpeg_dec_huff.c.obj
 * Language: Xtensa:LE:32:default
 * Types and names are reconstructed and must not be treated as the original source.
 */

/* ==================================================================
 * jpeg_dec_create_huffman_tbl
 * Purpose: Builds canonical JPEG Huffman decode metadata and fast lookup tables from DHT code-length/value arrays.
 * Entry: 00010018
 * ================================================================== */

/* WARNING: Type propagation algorithm not settling */
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_create_huffman_tbl(jpeg_decoder_t *jd,uint8_t *data,uint16_t ndata)

{
  byte bVar1;
  byte bVar2;
  byte *pbVar3;
  ushort *puVar4;
  uint8_t (*pauVar5) [16];
  uint uVar6;
  uint8_t *puVar7;
  int iVar8;
  uint uVar9;
  uint8_t *puVar10;
  uint uVar11;
  ushort *puVar12;
  uint uVar13;
  int32_t (*paiVar14) [16];
  int iVar15;
  uint uVar16;
  int iVar17;
  ushort uVar18;
  uint16_t (*pauVar19) [256];
  uint8_t *puVar20;
  int iVar21;
  uint8_t (*pauVar22) [16];

                    /* Unresolved local var: uint16_t i@[???]
                       Unresolved local var: uint16_t j@[???]
                       Unresolved local var: uint16_t b@[???]
                       Unresolved local var: uint16_t np@[???]
                       Unresolved local var: uint16_t cls@[???]
                       Unresolved local var: uint16_t num@[???]
                       Unresolved local var: uint8_t d@[???]
                       Unresolved local var: uint8_t * pb@[???]
                       Unresolved local var: uint8_t * pd@[???]
                       Unresolved local var: uint8_t * psym@[???]
                       Unresolved local var: uint8_t * pnbit@[???]
                       Unresolved local var: uint16_t hc@[???]
                       Unresolved local var: uint16_t * ph@[???] */
  uVar6 = (uint)ndata;
  if (ndata != 0) {
    do {
                    /* Unresolved local var: int p@[???]
                       Unresolved local var: int l@[???]
                       Unresolved local var: int ctr@[???]
                       Unresolved local var: int lookbits@[???] */
      if (uVar6 < 0x11) {
        return JPEG_ERR_BAD_DATA;
      }
      bVar1 = *data;
      if ((bVar1 & 0xee) != 0) {
        return JPEG_ERR_BAD_DATA;
      }
      uVar9 = (uint)(bVar1 >> 4);
      uVar16 = bVar1 & 0xf;
      pauVar5 = jd->huffbits[uVar16] + uVar9;
      bVar2 = data[1];
      (*pauVar5)[0] = bVar2;
      uVar11 = (uint)bVar2 + (bVar1 & 0xffffffee);
      puVar4 = (ushort *)(*_DAT_fffd008c)(uVar11 * 2,*pauVar5 + 1);
      if (puVar4 == (ushort *)0x0) {
        return JPEG_ERR_NO_MEM;
      }
      uVar18 = 0;
      uVar13 = uVar6 - 0x11 & 0xffff;
      jd->huffcode[uVar16][uVar9] = puVar4;
      uVar6 = 0;
      pauVar22 = pauVar5;
      do {
        bVar1 = (*pauVar22)[0];
        if (bVar1 != 0) {
          puVar4[uVar6] = uVar18;
          uVar6 = uVar6 + 1 & 0xffff;
          uVar18 = bVar1 + uVar18;
        }
        iVar17 = _DAT_fffd0100;
        pauVar22 = (uint8_t (*) [16])(*pauVar22 + 1);
        uVar18 = uVar18 * 2;
      } while (pauVar22 != pauVar5 + 1);
      paiVar14 = jd->huffmaxcode[uVar16] + uVar9;
      uVar6 = 0xffffffff;
      if ((*pauVar5)[0] != '\0') {
        *(uint *)(paiVar14 + 4) = -(uint)*puVar4;
        uVar6 = (uint)puVar4[(uint)(*pauVar5)[0] + iVar17];
      }
      (*paiVar14)[0] = uVar6;
      if (uVar13 < uVar11) {
        return JPEG_ERR_BAD_DATA;
      }
      pauVar19 = jd->huffdata[uVar16] + uVar9;
      pbVar3 = data + 0x11;
      if (uVar11 != 0) {
        bVar1 = data[0x11];
        if ((0xb < bVar1) && (((uVar9 ^ 0xffffffff) & 1) != 0)) {
          return JPEG_ERR_BAD_DATA;
        }
        *(byte *)*pauVar19 = bVar1;
        pbVar3 = data + 0x12;
      }
      data = pbVar3;
      puVar10 = jd->huff_look_nbits[uVar16][uVar9];
      uVar6 = uVar13 - uVar11 & 0xffff;
      puVar7 = jd->huff_look_sym[uVar16][uVar9];
      iVar17 = 1;
      iVar21 = 0;
      do {
        if ((*pauVar5)[0] != '\0') {
          puVar12 = puVar4 + iVar21;
          iVar8 = iVar21 + -1;
          puVar20 = (uint8_t *)((int)*pauVar19 + iVar21);
          do {
            iVar15 = (uint)*puVar12 << 0x20 - (0x20 - (-(iVar17 + -8) & 0x1fU));
            puVar10[iVar15] = (uint8_t)iVar17;
            puVar7[iVar15] = *puVar20;
            iVar21 = iVar21 + 1;
            puVar12 = puVar12 + 1;
            puVar20 = puVar20 + 1;
          } while ((iVar21 - iVar8 & 0xffffU) <= (uint)(*pauVar5)[0]);
        }
        iVar17 = iVar17 + 1;
        pauVar5 = (uint8_t (*) [16])(*pauVar5 + 1);
      } while (iVar17 != 9);
    } while (uVar6 != 0);
  }
  return JPEG_ERR_OK;
}

/* ==================================================================
 * jpeg_dec_huffman
 * Purpose: Decodes one entropy-coded 8x8 coefficient block: obtains the DC difference, expands AC run/size codes, handles EOB/ZRL and updates restart/DC-predictor state.
 * Entry: 0001020c
 * ================================================================== */

/* WARNING: Type propagation algorithm not settling */
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_huffman(jpeg_decoder_t *jd,int mcu_num,uint8_t cmp_idx,int16_t *tmp)

{
  byte bVar1;
  byte bVar2;
  byte bVar3;
  uint uVar4;
  uint uVar5;
  int iVar6;
  int iVar7;
  uint8_t *puVar8;
  byte *pbVar9;
  uint uVar10;
  int iVar11;
  int iVar12;
  byte *pbVar13;
  uint uVar14;
  jpeg_error_t jVar15;
  short sVar16;
  uint uVar17;
  jpeg_error_t jVar18;
  uint8_t *puVar19;
  short *psStack_48;
  int iStack_34;

  pbVar9 = jd->dptr;
  iVar6 = jd->dctr;
  uVar5 = jd->bits_left;
  uVar4 = jd->get_buffer;
  if (mcu_num == 0) {
LAB_000106d4:
    jd->dptr = pbVar9;
    jd->dctr = iVar6;
    jd->get_buffer = uVar4;
    jd->bits_left = uVar5;
    return JPEG_ERR_OK;
  }
  iStack_34 = 0;
                    /* Unresolved local var: int b@[???]
                       Unresolved local var: int d@[???]
                       Unresolved local var: int e@[???]
                       Unresolved local var: uint32_t blk@[???]
                       Unresolved local var: uint32_t i@[???]
                       Unresolved local var: uint32_t z@[???]
                       Unresolved local var: uint8_t dcid@[???]
                       Unresolved local var: uint8_t acid@[???]
                       Unresolved local var: uint8_t * dp@[???]
                       Unresolved local var: int dc@[???]
                       Unresolved local var: int get_buffer@[???]
                       Unresolved local var: int bits_left@[???]
                       Unresolved local var: int mcu_block_num@[???] */
  psStack_48 = tmp;
LAB_0001023d:
  uVar10 = (uint)jd->huffdcid[cmp_idx];
  bVar1 = jd->huffacid[cmp_idx];
  (*_DAT_fffd0250)(psStack_48,0,0x80);
  puVar8 = jd->huff_look_nbits[uVar10][0];
                    /* Unresolved local var: int nb@[???]
                       Unresolved local var: int val@[???]
                       Unresolved local var: int result@[DW_OP_reg8(a8)] */
  if ((int)uVar5 < 8) {
    do {
      uVar14 = uVar5;
      if (iVar6 == 0) {
LAB_000102ac:
        iVar11 = 1;
        uVar5 = uVar14;
        if ((int)uVar14 < 8) goto LAB_000102d4;
        goto LAB_000102b1;
      }
      bVar2 = *pbVar9;
      iVar6 = iVar6 + -1;
      pbVar9 = pbVar9 + 1;
      if (bVar2 == 0xff) {
        do {
          pbVar13 = pbVar9;
          iVar11 = iVar6;
          bVar3 = *pbVar13;
          pbVar9 = pbVar13 + 1;
          iVar6 = iVar11 + -1;
        } while (bVar3 == 0xff);
        if (bVar3 != 0) {
          if (bVar3 != 0xd9) {
            iVar6 = iVar11 + 1;
            pbVar9 = pbVar13 + -1;
          }
          goto LAB_000102ac;
        }
      }
      uVar5 = uVar14 + 8;
      uVar4 = uVar4 << 8 | (uint)bVar2;
    } while ((int)uVar5 < 0x19);
    uVar14 = (int)uVar4 >> (uVar14 & 0x1f) & 0xff;
    uVar17 = (uint)puVar8[uVar14];
    if (uVar17 == 0) {
      iVar11 = 9;
      goto LAB_00010326;
    }
  }
  else {
LAB_000102b1:
    uVar14 = (int)uVar4 >> (uVar5 - 8 & 0x1f) & 0xff;
    uVar17 = (uint)puVar8[uVar14];
    if (uVar17 == 0) {
      iVar11 = 9;
LAB_000102d4:
                    /* Unresolved local var: int code@[???] */
      if ((int)uVar5 < iVar11) {
        do {
          if (iVar6 == 0) {
LAB_00010320:
            if ((int)uVar5 < iVar11) {
              return JPEG_ERR_NO_MORE_DATA;
            }
            break;
          }
          bVar2 = *pbVar9;
          iVar6 = iVar6 + -1;
          pbVar9 = pbVar9 + 1;
          if (bVar2 == 0xff) {
            do {
              pbVar13 = pbVar9;
              iVar7 = iVar6;
              bVar3 = *pbVar13;
              pbVar9 = pbVar13 + 1;
              iVar6 = iVar7 + -1;
            } while (bVar3 == 0xff);
            if (bVar3 != 0) {
              if (bVar3 != 0xd9) {
                iVar6 = iVar7 + 1;
                pbVar9 = pbVar13 + -1;
              }
              goto LAB_00010320;
            }
          }
          uVar5 = uVar5 + 8;
          uVar4 = uVar4 << 8 | (uint)bVar2;
        } while ((int)uVar5 < 0x19);
      }
LAB_00010326:
      iVar12 = iVar11 * 4 + -4;
      uVar5 = uVar5 - iVar11;
      uVar14 = (int)uVar4 >> (uVar5 & 0x1f) & *(uint *)(iVar11 * 4 + _DAT_fffd0334);
      iVar7 = iVar11 * 4;
      if (jd->huffmaxcode[uVar10 - 1][1][iVar11 + 0xf] < (int)uVar14) {
        do {
          iVar12 = iVar7;
          if ((int)uVar5 < 1) {
            do {
              if (iVar6 == 0) {
LAB_000103a8:
                if ((int)uVar5 < 1) {
                  return JPEG_ERR_NO_MORE_DATA;
                }
                break;
              }
              bVar2 = *pbVar9;
              iVar6 = iVar6 + -1;
              pbVar9 = pbVar9 + 1;
              if (bVar2 == 0xff) {
                do {
                  pbVar13 = pbVar9;
                  iVar7 = iVar6;
                  bVar3 = *pbVar13;
                  pbVar9 = pbVar13 + 1;
                  iVar6 = iVar7 + -1;
                } while (bVar3 == 0xff);
                if (bVar3 != 0) {
                  if (bVar3 != 0xd9) {
                    iVar6 = iVar7 + 1;
                    pbVar9 = pbVar13 + -1;
                  }
                  goto LAB_000103a8;
                }
              }
              uVar5 = uVar5 + 8;
              uVar4 = uVar4 << 8 | (uint)bVar2;
            } while ((int)uVar5 < 0x19);
          }
          uVar5 = uVar5 - 1;
          uVar14 = (int)uVar4 >> (uVar5 & 0x1f) & 1U | uVar14 * 2;
          iVar11 = iVar11 + 1;
          iVar7 = iVar12 + 4;
        } while (*(int *)((int)jd->huffmaxcode[uVar10][0] + iVar12) < (int)uVar14);
        if (0x10 < iVar11) {
          return JPEG_ERR_BAD_DATA;
        }
      }
      bVar2 = *(byte *)((int)jd->huffdata[uVar10][0] +
                       *(int *)((int)jd->huffdata_offset[uVar10][0] + iVar12) + uVar14);
      goto LAB_000103f6;
    }
  }
  uVar5 = uVar5 - uVar17;
  bVar2 = jd->huff_look_sym[uVar10][0][uVar14];
LAB_000103f6:
  uVar10 = (uint)bVar2;
  sVar16 = jd->dcv[cmp_idx];
  if (uVar10 != 0) {
    if ((int)uVar5 < (int)uVar10) {
                    /* Unresolved local var: int c@[???] */
      if (0x18 < (int)uVar5) {
        return JPEG_ERR_NO_MORE_DATA;
      }
      while (iVar6 != 0) {
        bVar2 = *pbVar9;
        iVar6 = iVar6 + -1;
        pbVar9 = pbVar9 + 1;
        if (bVar2 == 0xff) {
          do {
            pbVar13 = pbVar9;
            iVar11 = iVar6;
            bVar3 = *pbVar13;
            pbVar9 = pbVar13 + 1;
            iVar6 = iVar11 + -1;
          } while (bVar3 == 0xff);
          if (bVar3 != 0) {
            if (bVar3 != 0xd9) {
              iVar6 = iVar11 + 1;
              pbVar9 = pbVar13 + -1;
            }
            break;
          }
        }
        uVar5 = uVar5 + 8;
        uVar4 = uVar4 << 8 | (uint)bVar2;
        if (0x18 < (int)uVar5) break;
      }
      if ((int)uVar5 < (int)uVar10) {
        return JPEG_ERR_NO_MORE_DATA;
      }
    }
    uVar5 = uVar5 - uVar10;
    jVar15 = *(jpeg_error_t *)(uVar10 * 4 + _DAT_fffd0450);
    jVar18 = (int)uVar4 >> (uVar5 & 0x1f) & jVar15;
    if (jVar18 < JPEG_ERR_OK) {
      return jVar18;
    }
    if (jVar18 <= *(jpeg_error_t *)((uVar10 - 1) * 4 + _DAT_fffd0450)) {
      jVar18 = jVar18 - jVar15;
    }
    sVar16 = (short)jVar18 + sVar16;
    jd->dcv[cmp_idx] = sVar16;
  }
  puVar19 = jd->huff_look_nbits[bVar1][1];
  puVar8 = jd->huff_look_sym[bVar1][1];
  *psStack_48 = sVar16;
  uVar10 = 1;
                    /* Unresolved local var: int c@[???] */
LAB_000104b8:
                    /* Unresolved local var: int nb@[???]
                       Unresolved local var: int val@[???]
                       Unresolved local var: int result@[???] */
  if (7 < (int)uVar5) {
LAB_000104fd:
    uVar14 = (int)uVar4 >> (uVar5 - 8 & 0x1f) & 0xff;
    uVar17 = (uint)puVar19[uVar14];
    if (uVar17 != 0) goto LAB_00010510;
    iVar11 = 9;
LAB_00010528:
                    /* Unresolved local var: int code@[???] */
    if ((int)uVar5 < iVar11) {
      do {
                    /* Unresolved local var: int c@[???] */
        if (iVar6 == 0) {
LAB_00010564:
          if ((int)uVar5 < iVar11) goto LAB_000106ec;
          break;
        }
        bVar2 = *pbVar9;
        iVar6 = iVar6 + -1;
        pbVar9 = pbVar9 + 1;
        if (bVar2 == 0xff) {
          do {
            pbVar13 = pbVar9;
            iVar7 = iVar6;
            bVar3 = *pbVar13;
            pbVar9 = pbVar13 + 1;
            iVar6 = iVar7 + -1;
          } while (bVar3 == 0xff);
          if (bVar3 != 0) {
            if (bVar3 != 0xd9) {
              iVar6 = iVar7 + 1;
              pbVar9 = pbVar13 + -1;
            }
            goto LAB_00010564;
          }
        }
        uVar5 = uVar5 + 8;
        uVar4 = uVar4 << 8 | (uint)bVar2;
      } while ((int)uVar5 < 0x19);
    }
LAB_0001056c:
    iVar12 = iVar11 * 4 + -4;
    uVar5 = uVar5 - iVar11;
    uVar14 = (int)uVar4 >> (uVar5 & 0x1f) & *(uint *)(iVar11 * 4 + _DAT_fffd056c);
    iVar7 = iVar11 * 4;
    if ((int)uVar14 <= jd->huffmaxcode[bVar1][0][iVar11 + 0xf]) {
LAB_000105fe:
      bVar2 = *(byte *)((int)jd->huffdata[bVar1][1] +
                       *(int *)((int)jd->huffdata_offset[bVar1][1] + iVar12) + uVar14);
      goto LAB_0001061b;
    }
    do {
      iVar12 = iVar7;
      if ((int)uVar5 < 1) {
        do {
                    /* Unresolved local var: int c@[???] */
          if (iVar6 == 0) {
LAB_000105d4:
            if ((int)uVar5 < 1) goto LAB_000106ec;
            break;
          }
          bVar2 = *pbVar9;
          iVar6 = iVar6 + -1;
          pbVar9 = pbVar9 + 1;
          if (bVar2 == 0xff) {
            do {
              pbVar13 = pbVar9;
              iVar7 = iVar6;
              bVar3 = *pbVar13;
              pbVar9 = pbVar13 + 1;
              iVar6 = iVar7 + -1;
            } while (bVar3 == 0xff);
            if (bVar3 != 0) {
              if (bVar3 != 0xd9) {
                iVar6 = iVar7 + 1;
                pbVar9 = pbVar13 + -1;
              }
              goto LAB_000105d4;
            }
          }
          uVar5 = uVar5 + 8;
          uVar4 = uVar4 << 8 | (uint)bVar2;
        } while ((int)uVar5 < 0x19);
      }
      uVar5 = uVar5 - 1;
      uVar14 = (int)uVar4 >> (uVar5 & 0x1f) & 1U | uVar14 * 2;
      iVar11 = iVar11 + 1;
      iVar7 = iVar12 + 4;
    } while (*(int *)((int)jd->huffmaxcode[bVar1][1] + iVar12) < (int)uVar14);
    if (iVar11 < 0x11) goto LAB_000105fe;
    uVar14 = 0xb;
    uVar17 = _DAT_fffd0624;
LAB_00010626:
    iVar11 = uVar10 + uVar17;
    if ((int)uVar5 < (int)uVar14) goto LAB_0001062c;
    goto LAB_0001066d;
  }
  do {
    uVar14 = uVar5;
    if (iVar6 == 0) {
LAB_000104f8:
      iVar11 = 1;
      uVar5 = uVar14;
      if ((int)uVar14 < 8) goto LAB_00010528;
      goto LAB_000104fd;
    }
    bVar2 = *pbVar9;
    iVar6 = iVar6 + -1;
    pbVar9 = pbVar9 + 1;
    if (bVar2 == 0xff) {
      do {
        pbVar13 = pbVar9;
        iVar11 = iVar6;
        bVar3 = *pbVar13;
        pbVar9 = pbVar13 + 1;
        iVar6 = iVar11 + -1;
      } while (bVar3 == 0xff);
      if (bVar3 != 0) {
        if (bVar3 != 0xd9) {
          iVar6 = iVar11 + 1;
          pbVar9 = pbVar13 + -1;
        }
        goto LAB_000104f8;
      }
    }
    uVar5 = uVar14 + 8;
    uVar4 = uVar4 << 8 | (uint)bVar2;
  } while ((int)uVar5 < 0x19);
  uVar14 = (int)uVar4 >> (uVar14 & 0x1f) & 0xff;
  uVar17 = (uint)puVar19[uVar14];
  if (uVar17 == 0) {
    iVar11 = 9;
    goto LAB_0001056c;
  }
LAB_00010510:
  uVar5 = uVar5 - uVar17;
  bVar2 = puVar8[uVar14];
LAB_0001061b:
  uVar14 = bVar2 & 0xf;
  uVar17 = (uint)(bVar2 >> 4);
  if ((bVar2 & 0xf) != 0) goto LAB_00010626;
  if (uVar17 == 0xf) {
    iVar11 = uVar10 + 0xf;
    goto LAB_000106b0;
  }
  goto LAB_000106ba;
LAB_000106ec:
  uVar14 = 0xd;
  iVar11 = uVar10 + _DAT_fffd06ec;
LAB_0001062c:
  do {
    if (iVar6 == 0) {
LAB_00010665:
      if ((int)uVar5 < (int)uVar14) {
        return JPEG_ERR_NO_MORE_DATA;
      }
      break;
    }
    bVar2 = *pbVar9;
    iVar6 = iVar6 + -1;
    pbVar9 = pbVar9 + 1;
    if (bVar2 == 0xff) {
      do {
        pbVar13 = pbVar9;
        iVar7 = iVar6;
        bVar3 = *pbVar13;
        pbVar9 = pbVar13 + 1;
        iVar6 = iVar7 + -1;
      } while (bVar3 == 0xff);
      if (bVar3 != 0) {
        if (bVar3 != 0xd9) {
          iVar6 = iVar7 + 1;
          pbVar9 = pbVar13 + -1;
        }
        goto LAB_00010665;
      }
    }
    uVar5 = uVar5 + 8;
    uVar4 = uVar4 << 8 | (uint)bVar2;
  } while ((int)uVar5 < 0x19);
LAB_0001066d:
  uVar5 = uVar5 - uVar14;
  jVar18 = *(jpeg_error_t *)(uVar14 * 4 + _DAT_fffd0670);
  jVar15 = (int)uVar4 >> (uVar5 & 0x1f) & jVar18;
  if (jVar15 < JPEG_ERR_OK) {
    return jVar15;
  }
  if (jVar15 <= *(jpeg_error_t *)((uVar14 - 1) * 4 + _DAT_fffd0688)) {
    jVar15 = jVar15 - jVar18;
  }
  psStack_48[*(byte *)(_DAT_fffd0690 + iVar11)] = (short)jVar15;
LAB_000106b0:
  uVar10 = iVar11 + 1;
  if (0x3f < uVar10) goto LAB_000106ba;
  goto LAB_000104b8;
LAB_000106ba:
  iStack_34 = iStack_34 + 1;
  psStack_48 = psStack_48 + 0x40;
  if (iStack_34 == mcu_num) goto LAB_000106d4;
  goto LAB_0001023d;
}
