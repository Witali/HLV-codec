/* ESP_NEW_JPEG 1.0.2 ESP32 decoder Ghidra pseudo-C.
 * Derived from the Espressif binary; see LICENSE.ESPRESSIF in the analysis directory.
 * Program: esp_jpeg_dec.c.obj
 * Language: Xtensa:LE:32:default
 * Types and names are reconstructed and must not be treated as the original source.
 */

/* ==================================================================
 * jpeg_dec_parse_header
 * Purpose: Scans baseline-JPEG markers, parses tables and frame geometry, then prepares MCU and output state for decoding.
 * Entry: 00010724
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t
jpeg_dec_parse_header(jpeg_dec_handle_t jpeg_dec,jpeg_dec_io_t *io,jpeg_dec_header_info_t *out_info)

{
  undefined2 uVar1;
  undefined2 uVar2;
  undefined2 uVar3;
  undefined2 uVar4;
  ushort *puVar5;
  undefined4 uVar6;
  undefined4 uVar7;
  int *piVar8;
  int iVar9;
  uint uVar10;
  jpeg_error_t jVar11;
  int iVar12;
  undefined2 *puVar13;
  int *piVar14;
  ushort uVar15;
  uint uVar16;
  undefined4 *puVar17;
  short sVar18;
  ushort uVar19;
  uint uVar20;
  undefined2 *puVar21;
  short sVar22;
  undefined4 uVar23;
  uint uVar24;
  int iVar25;
  ushort uVar26;
  int iVar27;
  int iStack_40;
  ushort *apuStack_3c [3];
  jpeg_dec_header_info_t *pjStack_30;

                    /* Unresolved local var: jpeg_decoder_t * jd@[???]
                       Unresolved local var: uint16_t marker@[???]
                       Unresolved local var: uint16_t len@[???]
                       Unresolved local var: uint8_t * seg@[???]
                       Unresolved local var: int data_remain@[???]
                       Unresolved local var: int rc@[???]
                       Unresolved local var: int ret@[???] */
  pjStack_30 = out_info;
  if ((io == (jpeg_dec_io_t *)0x0 || out_info == (jpeg_dec_header_info_t *)0x0) ||
     (jpeg_dec == (jpeg_dec_handle_t)0x0 ||
      (io == (jpeg_dec_io_t *)0x0 || out_info == (jpeg_dec_header_info_t *)0x0))) {
    uVar6 = (*_DAT_fffd0744)();
    (*_DAT_fffd0760)(1,_DAT_fffd074c,_DAT_fffd0750,uVar6,_DAT_fffd074c,jpeg_dec,io,pjStack_30);
    jVar11 = JPEG_ERR_INVALID_PARAM;
  }
  else {
    apuStack_3c[0] = (ushort *)io->inbuf;
    iStack_40 = io->inbuf_len;
                    /* Unresolved local var: int i@[???] */
    iVar9 = (int)jpeg_dec + 8;
    do {
      iVar12 = iVar9 + -8;
      do {
                    /* Unresolved local var: int j@[???] */
        if (*(int *)(iVar12 + 0x9c) != 0) {
          (*_DAT_fffd0780)();
          *(undefined4 *)(iVar12 + 0x9c) = 0;
        }
        (*_DAT_fffd0790)(*(undefined4 *)(iVar12 + 0x2ac),0,0x100);
        puVar17 = (undefined4 *)(iVar12 + 700);
        iVar12 = iVar12 + 4;
        (*_DAT_fffd07a4)(*puVar17,0,0x100);
      } while (iVar9 != iVar12);
      iVar9 = iVar9 + 8;
    } while ((int)jpeg_dec + 0x18 != iVar9);
    if (*(int *)((int)jpeg_dec + 0xbe0) != 0) {
      (*_DAT_fffd07c4)();
      *(undefined4 *)((int)jpeg_dec + 0xbe0) = 0;
    }
    if (*(int *)((int)jpeg_dec + 0xbe4) != 0) {
      (*_DAT_fffd07d4)();
      *(undefined4 *)((int)jpeg_dec + 0xbe4) = 0;
    }
    if (*(int *)((int)jpeg_dec + 0xbe8) != 0) {
      (*_DAT_fffd07e4)();
      *(undefined4 *)((int)jpeg_dec + 0xbe8) = 0;
    }
    if (*(int *)((int)jpeg_dec + 0xbec) != 0) {
      (*_DAT_fffd07f4)();
      *(undefined4 *)((int)jpeg_dec + 0xbec) = 0;
    }
    if (*(int *)((int)jpeg_dec + 0x58) != 0) {
      (*_DAT_fffd0804)();
      *(undefined4 *)((int)jpeg_dec + 0x58) = 0;
    }
    *(undefined4 *)((int)jpeg_dec + 0x1c) = 0;
    *(undefined2 *)((int)jpeg_dec + 0x48) = 0;
    *(undefined2 *)((int)jpeg_dec + 0x46) = 0;
    *(undefined4 *)((int)jpeg_dec + 0x14) = 0;
    *(undefined4 *)((int)jpeg_dec + 0x18) = 0;
    *(undefined4 *)((int)jpeg_dec + 0x20) = 0;
    if ((*(char *)((int)jpeg_dec + 0xb14) == '\0') && (*(char *)((int)jpeg_dec + 0xb15) == '\0')) {
      *(undefined4 *)((int)jpeg_dec + 0xb0c) = 0;
    }
    *(undefined2 *)((int)jpeg_dec + 0xbf4) = 0;
    io->out_size = 0;
    *(undefined1 *)((int)jpeg_dec + 0xbf1) = 1;
    *(undefined1 *)((int)jpeg_dec + 0xbf2) = 1;
    jVar11 = (*_DAT_fffd0844)(apuStack_3c,&iStack_40);
    iVar9 = iStack_40;
    puVar5 = apuStack_3c[0];
    if (jVar11 == JPEG_ERR_OK) {
      while (iStack_40 = iVar9, apuStack_3c[0] = puVar5, 3 < iVar9) {
        uVar19 = puVar5[1] << 8 | puVar5[1] >> 8;
        if (uVar19 < 3) {
LAB_000108d5:
          uVar6 = (*_DAT_fffd08d8)();
          (*_DAT_fffd08e8)(1,_DAT_fffd08dc,_DAT_fffd08e0,uVar6,_DAT_fffd08dc);
          return JPEG_ERR_BAD_DATA;
        }
        uVar26 = *puVar5;
        uVar20 = (uint)(uVar26 >> 8);
        if ((uVar26 & 0xff) != 0xff) goto LAB_000108d5;
        apuStack_3c[0] = puVar5 + 2;
        iStack_40 = iVar9 + -4;
        uVar10 = (uint)(ushort)(uVar19 - 2);
        if (iStack_40 < (int)uVar10) {
          uVar6 = (*_DAT_fffd0908)();
          (*_DAT_fffd091c)(1,_DAT_fffd090c,_DAT_fffd0910,uVar6,_DAT_fffd090c,0x231);
          goto LAB_00010921;
        }
        if (uVar20 == 0xda) {
                    /* Unresolved local var: jpeg_error_t ret_value@[???] */
          sVar18 = *(short *)((int)jpeg_dec + 0x36);
          sVar22 = *(short *)((int)jpeg_dec + 0x34);
          *(ushort **)((int)jpeg_dec + 0x10) = puVar5;
          *(int *)((int)jpeg_dec + 0xc) = iVar9;
          io->inbuf_remain = iVar9;
          if (sVar22 < sVar18) {
            iStack_40 = iVar9;
            apuStack_3c[0] = puVar5;
            uVar6 = (*_DAT_fffd09e4)();
            (*_DAT_fffd09f8)(1,_DAT_fffd09e8,_DAT_fffd09ec,uVar6,_DAT_fffd09e8);
LAB_00010f10:
            jVar11 = JPEG_ERR_UNSUPPORT_FMT;
            goto LAB_00010f12;
          }
          uVar19 = *(ushort *)((int)jpeg_dec + 0xb0c);
          uVar20 = (uint)uVar19;
          uVar15 = *(ushort *)((int)jpeg_dec + 0xb0e);
          uVar26 = *(ushort *)((int)jpeg_dec + 0x38);
          uVar10 = (uint)uVar26;
          if (uVar20 == 0) {
            *(ushort *)((int)jpeg_dec + 0xb0c) = uVar26;
            uVar19 = uVar26;
            if (uVar15 == 0) {
              uVar15 = *(ushort *)((int)jpeg_dec + 0x3a);
              *(ushort *)((int)jpeg_dec + 0xb0e) = uVar15;
              uVar26 = uVar15;
            }
            else {
LAB_00010a44:
              uVar26 = *(ushort *)((int)jpeg_dec + 0x3a);
              if (*(ushort *)((int)jpeg_dec + 0x3a) < uVar15) goto LAB_00010a24;
            }
LAB_00010a4a:
            uVar20 = *(uint *)((int)jpeg_dec + 4);
            if ((uVar20 & 0xfffffffd) == 0) {
              pjStack_30->width = uVar19;
              pjStack_30->height = uVar15;
            }
            else if ((uVar20 & 0xfffffffd) == 1) {
              pjStack_30->width = uVar15;
              pjStack_30->height = *(uint16_t *)((int)jpeg_dec + 0xb0c);
            }
            if (*(char *)((int)jpeg_dec + 0xbf3) == '\0') {
              if (*(char *)((int)jpeg_dec + 0xb14) != '\0') {
                    /* Unresolved local var: int h_tmp@[???]
                       Unresolved local var: int16_t * tmp@[???] */
                if ((uVar10 < *(ushort *)((int)jpeg_dec + 0xb10)) ||
                   (uVar26 < *(ushort *)((int)jpeg_dec + 0xb12))) {
                  iStack_40 = iVar9;
                  apuStack_3c[0] = puVar5;
                  uVar7 = (*_DAT_fffd0ac8)();
                  uVar1 = *(undefined2 *)((int)jpeg_dec + 0x38);
                  uVar2 = *(undefined2 *)((int)jpeg_dec + 0x3a);
                  uVar3 = *(undefined2 *)((int)jpeg_dec + 0xb10);
                  uVar4 = *(undefined2 *)((int)jpeg_dec + 0xb12);
                  uVar6 = _DAT_fffd0acc;
                  uVar23 = _DAT_fffd0adc;
                  goto LAB_00010ade;
                }
                uVar19 = *(ushort *)((int)jpeg_dec + 0xb0c);
                if (((uVar19 & 7) != 0) || ((*(ushort *)((int)jpeg_dec + 0xb0e) & 7) != 0))
                goto LAB_00010b04;
                iStack_40 = iVar9;
                apuStack_3c[0] = puVar5;
                if (*(char *)((int)jpeg_dec + 0xbf0) == '\0') goto LAB_00010cec;
                puVar13 = *(undefined2 **)((int)jpeg_dec + 0xb1c);
                if (puVar13 == (undefined2 *)0x0) {
                  puVar13 = (undefined2 *)(*_DAT_fffd0b30)((uint)(ushort)(uVar19 + 1) << 2,0x10);
                  *(undefined2 **)((int)jpeg_dec + 0xb1c) = puVar13;
                  if (puVar13 == (undefined2 *)0x0) {
                    uVar6 = (*_DAT_fffd0b48)();
                    (*_DAT_fffd0b5c)(1,_DAT_fffd0b4c,_DAT_fffd0b50,uVar6,_DAT_fffd0b4c,0x81);
                    goto LAB_00010bba;
                  }
                    /* Unresolved local var: size_t i@[???] */
                  uVar20 = 0;
                  puVar21 = puVar13;
                  if (*(short *)((int)jpeg_dec + 0xb0c) == 0) {
                    uVar10 = 0;
                  }
                  else {
                    do {
                      *puVar21 = (short)(uVar20 % (uint)((int)*(short *)((int)jpeg_dec + 0x34) << 3)
                                        );
                      uVar10 = (uint)*(ushort *)((int)jpeg_dec + 0xb0c);
                      uVar20 = uVar20 + 1;
                      puVar21 = puVar21 + 1;
                    } while (uVar20 < uVar10);
                  }
                  puVar13[uVar10] = 0xffff;
                }
                else {
                  if ((uint)*(ushort *)((int)jpeg_dec + 0xb10) < (uint)((int)(uVar10 + 7) >> 3)) {
                    uVar7 = (*_DAT_fffd0b98)();
                    uVar19 = *(ushort *)((int)jpeg_dec + 0x38);
                    uVar1 = *(undefined2 *)((int)jpeg_dec + 0xb10);
                    uVar6 = _DAT_fffd0ba0;
                    uVar23 = _DAT_fffd0ba8;
                    goto LAB_00010bac;
                  }
                    /* Unresolved local var: size_t x_i@[???] */
                  uVar20 = 0;
                  if (uVar19 != 0) {
                    *puVar13 = 0;
                    uVar20 = (uint)*(ushort *)((int)jpeg_dec + 0xb0c);
                    if (1 < uVar20) {
                      uVar10 = 1;
                      puVar21 = puVar13;
                      do {
                        puVar21 = puVar21 + 1;
                        *puVar21 = (short)(((((uVar10 & 0xffff) << 0xe) /
                                            (uint)*(ushort *)((int)jpeg_dec + 0xb10)) *
                                            (uint)*(ushort *)((int)jpeg_dec + 0x38) + 0x2000 >> 0xe)
                                          % (uint)((int)*(short *)((int)jpeg_dec + 0x34) << 3));
                        uVar20 = (uint)*(ushort *)((int)jpeg_dec + 0xb0c);
                        uVar10 = uVar10 + 1;
                      } while (uVar10 < uVar20);
                    }
                  }
                  puVar13[uVar20] = 0xffff;
                }
                puVar13 = *(undefined2 **)((int)jpeg_dec + 0xb18);
                if (puVar13 == (undefined2 *)0x0) {
                  puVar13 = (undefined2 *)
                            (*_DAT_fffd0c18)((*(ushort *)((int)jpeg_dec + 0xb0e) + 1) * 4,0x10);
                  *(undefined2 **)((int)jpeg_dec + 0xb18) = puVar13;
                  if (puVar13 == (undefined2 *)0x0) {
                    uVar6 = (*_DAT_fffd0c30)();
                    (*_DAT_fffd0c44)(1,_DAT_fffd0c34,_DAT_fffd0c38,uVar6,_DAT_fffd0c34,0x9b);
                  }
                  else {
                    /* Unresolved local var: size_t i@[???] */
                    uVar20 = 0;
                    puVar21 = puVar13;
                    if (*(short *)((int)jpeg_dec + 0xb0e) == 0) {
                      uVar10 = 0;
                    }
                    else {
                      do {
                        *puVar21 = (short)(uVar20 % (uint)((int)*(short *)((int)jpeg_dec + 0x36) <<
                                                          3));
                        uVar10 = (uint)*(ushort *)((int)jpeg_dec + 0xb0e);
                        uVar20 = uVar20 + 1;
                        puVar21 = puVar21 + 1;
                      } while (uVar20 < uVar10);
                    }
                    puVar13[uVar10] = 0xffff;
LAB_00010ce4:
                    sVar18 = *(short *)((int)jpeg_dec + 0x36);
                    *(undefined1 *)((int)jpeg_dec + 0xbf0) = 0;
LAB_00010cec:
                    /* Unresolved local var: size_t i@[???] */
                    if (sVar18 * 8 == 0) {
LAB_00010d2d:
                      uVar20 = 0x10;
                    }
                    else {
                      uVar23 = *(undefined4 *)((int)jpeg_dec + 0x6c);
                      uVar6 = *(undefined4 *)((int)jpeg_dec + 0x70);
                      puVar17 = (undefined4 *)((int)jpeg_dec + _DAT_fffd0cfc);
                      *puVar17 = *(undefined4 *)((int)jpeg_dec + 0x68);
                      puVar17[0x10] = uVar23;
                      puVar17[0x20] = uVar6;
                      if ((*(ushort *)((int)jpeg_dec + 0xb12) == 0) ||
                         ((int)(sVar18 * 8 * (uint)*(ushort *)((int)jpeg_dec + 0xb12)) /
                          (int)(uint)*(ushort *)((int)jpeg_dec + 0x3a) < 9)) goto LAB_00010d2d;
                      uVar20 = 0x20;
                    }
                    iVar9 = (*_DAT_fffd0d50)(*(ushort *)((int)jpeg_dec + 0xb0c) * uVar20 * 6,0x10);
                    *(int *)((int)jpeg_dec + 0xbe0) = iVar9;
                    if (iVar9 == 0) {
                      uVar6 = (*_DAT_fffd0d5c)();
                      (*_DAT_fffd0d70)(1,_DAT_fffd0d60,_DAT_fffd0d64,uVar6,_DAT_fffd0d60,0xc0);
                    }
                    else {
                      iVar9 = (*_DAT_fffd0d80)((uint)*(ushort *)((int)jpeg_dec + 0xb0e) << 2);
                      *(int *)((int)jpeg_dec + 0xbe4) = iVar9;
                      if (iVar9 == 0) {
                        uVar6 = (*_DAT_fffd0d8c)();
                        (*_DAT_fffd0da0)(1,_DAT_fffd0d90,_DAT_fffd0d94,uVar6,_DAT_fffd0d90,0xc6);
                      }
                      else {
                        iVar9 = (*_DAT_fffd0db0)((uint)*(ushort *)((int)jpeg_dec + 0xb0e) << 2);
                        *(int *)((int)jpeg_dec + 0xbe8) = iVar9;
                        if (iVar9 == 0) {
                          uVar6 = (*_DAT_fffd0dbc)();
                          (*_DAT_fffd0dd0)(1,_DAT_fffd0dc0,_DAT_fffd0dc4,uVar6,_DAT_fffd0dc0,0xcc);
                        }
                        else {
                          piVar14 = (int *)(*_DAT_fffd0de0)((uint)*(ushort *)((int)jpeg_dec + 0xb0e)
                                                            << 2);
                          *(int **)((int)jpeg_dec + 0xbec) = piVar14;
                          if (piVar14 != (int *)0x0) {
                    /* Unresolved local var: size_t i@[???] */
                            if (*(short *)((int)jpeg_dec + 0xb0e) == 0) {
                              return JPEG_ERR_OK;
                            }
                            iVar25 = *(int *)((int)jpeg_dec + 0xbe0);
                            iVar27 = *(ushort *)((int)jpeg_dec + 0xb0c) * uVar20;
                            piVar8 = *(int **)((int)jpeg_dec + 0xbe8);
                            iVar12 = (0U % uVar20) * (uint)*(ushort *)((int)jpeg_dec + 0xb0c);
                            iVar9 = iVar12 + iVar27;
                            **(int **)((int)jpeg_dec + 0xbe4) = iVar12 * 2 + iVar25;
                            *piVar8 = iVar9 * 2 + iVar25;
                            *piVar14 = (iVar27 + iVar9) * 2 + iVar25;
                            return JPEG_ERR_OK;
                          }
                          uVar6 = (*_DAT_fffd0dec)();
                          (*_DAT_fffd0e00)(1,_DAT_fffd0df0,_DAT_fffd0df4,uVar6,_DAT_fffd0df0,0xd2);
                        }
                      }
                    }
                  }
                }
                else {
                  if ((uint)((int)(*(ushort *)((int)jpeg_dec + 0x3a) + 7) >> 3) <=
                      (uint)*(ushort *)((int)jpeg_dec + 0xb12)) {
                    /* Unresolved local var: size_t x_i@[???] */
                    if (*(short *)((int)jpeg_dec + 0xb0e) == 0) {
                      uVar20 = 0;
                    }
                    else {
                      *puVar13 = 0;
                      uVar20 = (uint)*(ushort *)((int)jpeg_dec + 0xb0e);
                      if (1 < uVar20) {
                        uVar10 = 1;
                        puVar21 = puVar13;
                        do {
                          puVar21 = puVar21 + 1;
                          *puVar21 = (short)(((((uVar10 & 0xffff) << 0xe) /
                                              (uint)*(ushort *)((int)jpeg_dec + 0xb12)) *
                                              (uint)*(ushort *)((int)jpeg_dec + 0x3a) + 0x2000 >>
                                             0xe) % (uint)((int)*(short *)((int)jpeg_dec + 0x36) <<
                                                          3));
                          uVar20 = (uint)*(ushort *)((int)jpeg_dec + 0xb0e);
                          uVar10 = uVar10 + 1;
                        } while (uVar10 < uVar20);
                      }
                    }
                    puVar13[uVar20] = 0xffff;
                    goto LAB_00010ce4;
                  }
                  uVar7 = (*_DAT_fffd0c84)();
                  uVar19 = *(ushort *)((int)jpeg_dec + 0x3a);
                  uVar1 = *(undefined2 *)((int)jpeg_dec + 0xb12);
                  uVar6 = _DAT_fffd0c90;
                  uVar23 = _DAT_fffd0c98;
LAB_00010bac:
                  (*_DAT_fffd0bb4)(1,uVar6,uVar23,uVar7,uVar6,uVar1,(int)(uVar19 + 7) >> 3);
                }
LAB_00010bba:
                jVar11 = JPEG_ERR_NO_MEM;
LAB_00010f12:
                    /* Unresolved local var: int i@[???] */
                piVar14 = (int *)((int)jpeg_dec + 0x9c);
                    /* Unresolved local var: int j@[???] */
                do {
                  if (*piVar14 != 0) {
                    (*_DAT_fffd0f24)();
                    *piVar14 = 0;
                  }
                  if (piVar14[1] != 0) {
                    (*_DAT_fffd0f30)();
                    piVar14[1] = 0;
                  }
                  piVar14 = piVar14 + 2;
                } while (piVar14 != (int *)((int)jpeg_dec + 0xac));
                if (*(char *)((int)jpeg_dec + 0xb14) != '\0') {
                  if (*(int *)((int)jpeg_dec + 0xb1c) != 0) {
                    (*_DAT_fffd0f48)();
                    *(undefined4 *)((int)jpeg_dec + 0xb1c) = 0;
                  }
                  if (*(int *)((int)jpeg_dec + 0xb18) != 0) {
                    (*_DAT_fffd0f58)();
                    *(undefined4 *)((int)jpeg_dec + 0xb18) = 0;
                  }
                  if (*(int *)((int)jpeg_dec + 0xbe0) != 0) {
                    (*_DAT_fffd0f68)();
                    *(undefined4 *)((int)jpeg_dec + 0xbe0) = 0;
                  }
                  if (*(int *)((int)jpeg_dec + 0xbe4) != 0) {
                    (*_DAT_fffd0f78)();
                    *(undefined4 *)((int)jpeg_dec + 0xbe4) = 0;
                  }
                  if (*(int *)((int)jpeg_dec + 0xbe8) != 0) {
                    (*_DAT_fffd0f88)();
                    *(undefined4 *)((int)jpeg_dec + 0xbe8) = 0;
                  }
                  if (*(int *)((int)jpeg_dec + 0xbec) != 0) {
                    (*_DAT_fffd0f98)();
                    *(undefined4 *)((int)jpeg_dec + 0xbec) = 0;
                  }
                }
                if (*(int *)((int)jpeg_dec + 0x58) == 0) {
                  return jVar11;
                }
                (*_DAT_fffd0fa8)();
                *(undefined4 *)((int)jpeg_dec + 0x58) = 0;
                return jVar11;
              }
              uVar19 = *(ushort *)((int)jpeg_dec + 0xb0c);
              if (*(char *)((int)jpeg_dec + 0xb15) == '\0') {
                if ((uVar19 & 7) == 0) {
                  if ((*(ushort *)((int)jpeg_dec + 0xb0e) & 7) == 0) {
                    return JPEG_ERR_OK;
                  }
                  iStack_40 = iVar9;
                  apuStack_3c[0] = puVar5;
                  if (uVar20 == 0) {
LAB_00010eaf:
                    iVar9 = (*_DAT_fffd0ec8)(sVar22 * 8 * sVar18 * 8 *
                                             (uint)*(byte *)((int)jpeg_dec + 0x52),0x10);
                    *(int *)((int)jpeg_dec + 0x58) = iVar9;
                    if (iVar9 != 0) {
                      return JPEG_ERR_OK;
                    }
                    uVar6 = (*_DAT_fffd0ed4)();
                    (*_DAT_fffd0ee8)(1,_DAT_fffd0ed8,_DAT_fffd0edc,uVar6,_DAT_fffd0ed8,0xf2);
                    goto LAB_00010bba;
                  }
                }
                else if (uVar20 == 0) {
                  iStack_40 = iVar9;
                  apuStack_3c[0] = puVar5;
                  if ((*(char *)((int)jpeg_dec + 0x53) == '\x03') && ((uVar19 & 1) != 0)) {
                    *(ushort *)((int)jpeg_dec + 0xb0c) = uVar19 + 1;
                    pjStack_30->width = pjStack_30->width + 1;
                    uVar6 = (*_DAT_fffd0e90)();
                    (*_DAT_fffd0ea4)(3,_DAT_fffd0e98,_DAT_fffd0e98,uVar6,_DAT_fffd0e98);
                    sVar18 = *(short *)((int)jpeg_dec + 0x36);
                    sVar22 = *(short *)((int)jpeg_dec + 0x34);
                  }
                  goto LAB_00010eaf;
                }
                iStack_40 = iVar9;
                apuStack_3c[0] = puVar5;
                uVar6 = (*_DAT_fffd0ef0)();
                (*_DAT_fffd0f0c)(1,_DAT_fffd0ef8,_DAT_fffd0f00,uVar6,_DAT_fffd0ef8,
                                 *(undefined2 *)((int)jpeg_dec + 0xb0e),
                                 *(undefined2 *)((int)jpeg_dec + 0xb0c));
                goto LAB_00010f10;
              }
              if (((uVar19 & 7) == 0) && ((*(ushort *)((int)jpeg_dec + 0xb0e) & 7) == 0)) {
                return JPEG_ERR_OK;
              }
LAB_00010b04:
              iStack_40 = iVar9;
              apuStack_3c[0] = puVar5;
              uVar7 = (*_DAT_fffd0b04)();
              uVar1 = *(undefined2 *)((int)jpeg_dec + 0xb0c);
              uVar2 = *(undefined2 *)((int)jpeg_dec + 0xb0e);
              uVar6 = _DAT_fffd0b0c;
              uVar23 = _DAT_fffd0b14;
            }
            else {
              if (((*(ushort *)((int)jpeg_dec + 0xb0c) & 7) == 0) &&
                 (uVar20 = *(ushort *)((int)jpeg_dec + 0xb0e) & 7,
                 (*(ushort *)((int)jpeg_dec + 0xb0e) & 7) == 0)) {
                *(short *)((int)jpeg_dec + 0xbf4) = (short)uVar20;
                io->out_size = uVar20;
                return JPEG_ERR_OK;
              }
              iStack_40 = iVar9;
              apuStack_3c[0] = puVar5;
              uVar7 = (*_DAT_fffd0a88)();
              uVar1 = *(undefined2 *)((int)jpeg_dec + 0xb0c);
              uVar2 = *(undefined2 *)((int)jpeg_dec + 0xb0e);
              uVar6 = _DAT_fffd0a8c;
              uVar23 = _DAT_fffd0a94;
            }
            (*_DAT_fffd0aa0)(1,uVar6,uVar23,uVar7,uVar6,uVar2,uVar1);
          }
          else {
            if (uVar15 == 0) {
              uVar15 = *(ushort *)((int)jpeg_dec + 0x3a);
              *(ushort *)((int)jpeg_dec + 0xb0e) = uVar15;
              uVar26 = uVar15;
              if (uVar20 <= uVar10) goto LAB_00010a4a;
            }
            else if (uVar20 <= uVar10) goto LAB_00010a44;
LAB_00010a24:
            iStack_40 = iVar9;
            apuStack_3c[0] = puVar5;
            uVar7 = (*_DAT_fffd0a24)();
            uVar1 = *(undefined2 *)((int)jpeg_dec + 0x38);
            uVar2 = *(undefined2 *)((int)jpeg_dec + 0x3a);
            uVar3 = *(undefined2 *)((int)jpeg_dec + 0xb0c);
            uVar4 = *(undefined2 *)((int)jpeg_dec + 0xb0e);
            uVar6 = _DAT_fffd0a2c;
            uVar23 = _DAT_fffd0a3c;
LAB_00010ade:
            (*_DAT_fffd0aec)(1,uVar6,uVar23,uVar7,uVar6,uVar4,uVar3,uVar2,uVar1);
          }
          jVar11 = JPEG_ERR_INVALID_PARAM;
          goto LAB_00010f12;
        }
        if (uVar20 < 0xdb) {
          uVar16 = uVar20 - 0xc0 & 0xffff;
          if (uVar16 < 0x1a) {
            uVar24 = 1 << 0x20 - (0x20 - (uVar20 - 0xc0 & 0x1f));
            if ((uVar24 & _DAT_fffd0948) != 0) {
              uVar6 = (*_DAT_fffd0fb8)();
              (*_DAT_fffd0fc8)(1,_DAT_fffd0fbc,_DAT_fffd0fc0,uVar6,_DAT_fffd0fbc);
              return JPEG_ERR_UNSUPPORT_STD;
            }
            if (uVar16 == 4) {
              jVar11 = (*_DAT_fffd09a8)(jpeg_dec,apuStack_3c[0],uVar10);
            }
            else {
              if ((uVar24 & 1) == 0) goto LAB_00011001;
              jVar11 = (*_DAT_fffd0978)(jpeg_dec,apuStack_3c[0],uVar10);
            }
LAB_00010fd2:
            if (jVar11 != JPEG_ERR_OK) {
              pjStack_30 = (jpeg_dec_header_info_t *)jVar11;
              uVar6 = (*_DAT_fffd0fe0)();
              (*_DAT_fffd0ff8)(1,_DAT_fffd0fe4,_DAT_fffd0fe8,uVar6,_DAT_fffd0fe4,
                               (uVar26 & 0xff) << 8 | uVar20,pjStack_30);
              return (jpeg_error_t)pjStack_30;
            }
          }
        }
        else {
          if (uVar20 == 0xdb) {
            jVar11 = (*_DAT_fffd09bc)(jpeg_dec,apuStack_3c[0],uVar10);
            goto LAB_00010fd2;
          }
          if (uVar20 == 0xdd) {
            *(ushort *)((int)jpeg_dec + 0x1c) = puVar5[2] << 8 | puVar5[2] >> 8;
          }
        }
LAB_00011001:
        puVar5 = (ushort *)((int)apuStack_3c[0] + uVar10);
        iVar9 = iStack_40 - uVar10;
      }
      uVar6 = (*_DAT_fffd0880)();
      (*_DAT_fffd0894)(1,_DAT_fffd0884,_DAT_fffd0888,uVar6,_DAT_fffd0884,0x224);
LAB_00010921:
      jVar11 = JPEG_ERR_NO_MORE_DATA;
    }
    else {
      uVar6 = (*_DAT_fffd0850)();
      (*_DAT_fffd0864)(1,_DAT_fffd0854,_DAT_fffd0858,uVar6,_DAT_fffd0854);
    }
  }
  return jVar11;
}

/* ==================================================================
 * jpeg_dec_process
 * Purpose: Public decode entry that dispatches the prepared image to the selected rotation, scale, clip and block-processing kernel.
 * Entry: 00011084
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_process(jpeg_dec_handle_t jpeg_dec,jpeg_dec_io_t *io)

{
  undefined4 uVar1;
  jpeg_error_t jVar2;
  int iVar3;

                    /* Unresolved local var: jpeg_decoder_t * jd@[???]
                       Unresolved local var: jpeg_error_t ret@[???] */
  if ((jpeg_dec == (jpeg_dec_handle_t)0x0) ||
     (io == (jpeg_dec_io_t *)0x0 || jpeg_dec == (jpeg_dec_handle_t)0x0)) {
    uVar1 = (*_DAT_fffd1098)();
    (*_DAT_fffd10b0)(1,_DAT_fffd10a0,_DAT_fffd10a4,uVar1,_DAT_fffd10a0,jpeg_dec,io);
    jVar2 = JPEG_ERR_INVALID_PARAM;
  }
  else {
    if (*(char *)((int)jpeg_dec + 0xbf2) != '\0') {
      *(bool *)((int)jpeg_dec + 0xbf2) =
           io == (jpeg_dec_io_t *)0x0 || jpeg_dec == (jpeg_dec_handle_t)0x0;
      jVar2 = (*_DAT_fffd10cc)(jpeg_dec,io);
      if (jVar2 < JPEG_ERR_OK) {
        uVar1 = (*_DAT_fffd10d8)();
        (*_DAT_fffd10e8)(1,_DAT_fffd10dc,_DAT_fffd10e0,uVar1,_DAT_fffd10dc);
        return jVar2;
      }
    }
    jVar2 = (**(code **)((int)jpeg_dec + 0x90))(jpeg_dec,io);
    iVar3 = *(int *)((int)jpeg_dec + 0xc);
    if ((1 < iVar3) && (**(ushort **)((int)jpeg_dec + 0x10) == _DAT_fffd1108)) {
      iVar3 = iVar3 + -2;
      *(int *)((int)jpeg_dec + 0xc) = iVar3;
      *(ushort **)((int)jpeg_dec + 0x10) = *(ushort **)((int)jpeg_dec + 0x10) + 1;
    }
    io->inbuf_remain = iVar3;
  }
  return jVar2;
}

/* ==================================================================
 * jpeg_dec_close
 * Purpose: Releases the decoder state and every persistent work allocation.
 * Entry: 00011154
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_close(jpeg_dec_handle_t jpeg_dec)

{
  undefined4 uVar1;
  jpeg_error_t jVar2;
  int *piVar3;

                    /* Unresolved local var: jpeg_decoder_t * jd@[???] */
  if (jpeg_dec == (jpeg_dec_handle_t)0x0) {
    uVar1 = (*_DAT_fffd115c)();
    (*_DAT_fffd1170)(1,_DAT_fffd1160,_DAT_fffd1164,uVar1,_DAT_fffd1160,0);
    jVar2 = JPEG_ERR_INVALID_PARAM;
  }
  else {
    if (*(int *)((int)jpeg_dec + 0x5c) != 0) {
      (*_DAT_fffd1180)();
    }
    if (*(int *)((int)jpeg_dec + 0x68) != 0) {
      (*_DAT_fffd118c)();
    }
    if (*(int *)((int)jpeg_dec + 0x24) != 0) {
      (*_DAT_fffd1194)();
    }
    if (*(int *)((int)jpeg_dec + 0x58) != 0) {
      (*_DAT_fffd11a0)();
    }
    if (*(int *)((int)jpeg_dec + 0x2ac) != 0) {
      (*_DAT_fffd11ac)();
    }
    if (*(int *)((int)jpeg_dec + 700) != 0) {
      (*_DAT_fffd11b8)();
    }
                    /* Unresolved local var: int i@[???] */
    piVar3 = (int *)((int)jpeg_dec + 0x9c);
    do {
                    /* Unresolved local var: int j@[???] */
      if (*piVar3 != 0) {
        (*_DAT_fffd11cc)();
      }
      if (piVar3[1] != 0) {
        (*_DAT_fffd11d4)();
      }
      piVar3 = piVar3 + 2;
    } while (piVar3 != (int *)((int)jpeg_dec + 0xac));
    if (*(char *)((int)jpeg_dec + 0xb14) != '\0') {
      if (*(int *)((int)jpeg_dec + 0xb1c) != 0) {
        (*_DAT_fffd11ec)();
      }
      if (*(int *)((int)jpeg_dec + 0xb18) != 0) {
        (*_DAT_fffd11f8)();
      }
      if (*(int *)((int)jpeg_dec + 0xbe0) != 0) {
        (*_DAT_fffd1204)();
      }
      if (*(int *)((int)jpeg_dec + 0xbe4) != 0) {
        (*_DAT_fffd1210)();
      }
      if (*(int *)((int)jpeg_dec + 0xbe8) != 0) {
        (*_DAT_fffd121c)();
      }
      if (*(int *)((int)jpeg_dec + 0xbec) != 0) {
        (*_DAT_fffd1228)();
      }
    }
    (*_DAT_fffd1230)(jpeg_dec);
    jVar2 = JPEG_ERR_OK;
  }
  return jVar2;
}

/* ==================================================================
 * jpeg_dec_open
 * Purpose: Validates decoder configuration, allocates persistent state and selects output, scale, clip and rotation dispatch tables.
 * Entry: 00011450
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_open(jpeg_dec_config_t *config,jpeg_dec_handle_t *jpeg_dec)

{
  _Bool _Var1;
  uint16_t uVar2;
  jpeg_pixel_format_t jVar3;
  jpeg_pixel_format_t *pjVar4;
  undefined4 uVar5;
  undefined4 *puVar6;
  undefined4 uVar7;
  int iVar8;
  undefined1 uVar9;
  uint16_t uVar10;
  jpeg_pixel_format_t jVar11;
  uint uVar12;
  undefined1 uVar13;
  ushort uVar14;
  jpeg_rotate_t jVar15;
  uint uVar16;
  jpeg_resolution_t jVar17;
  undefined4 uVar18;

                    /* Unresolved local var: jpeg_decoder_t * jd@[???]
                       Unresolved local var: int16_t * workbuf_pool@[???]
                       Unresolved local var: int16_t * yuv_pool@[???]
                       Unresolved local var: int16_t * pb_pool@[???]
                       Unresolved local var: uint8_t * huff_look_nbits_pool@[???]
                       Unresolved local var: uint8_t * huff_look_sym_pool@[???] */
  if (jpeg_dec == (jpeg_dec_handle_t *)0x0) {
    uVar5 = (*_DAT_fffd1458)();
    (*_DAT_fffd146c)(1,_DAT_fffd1460,_DAT_fffd1460,uVar5,_DAT_fffd1460,0);
    return JPEG_ERR_INVALID_PARAM;
  }
  *jpeg_dec = (jpeg_dec_handle_t)0x0;
  if (config == (jpeg_dec_config_t *)0x0) {
    uVar5 = (*_DAT_fffd147c)();
    (*_DAT_fffd1490)(1,_DAT_fffd1480,_DAT_fffd1484,uVar5,_DAT_fffd1480,0);
    return JPEG_ERR_INVALID_PARAM;
  }
  puVar6 = (undefined4 *)(*_DAT_fffd149c)(_DAT_fffd149c);
  uVar9 = 0;
  if (puVar6 == (undefined4 *)0x0) {
    uVar5 = (*_DAT_fffd14a8)();
    (*_DAT_fffd14bc)(1,_DAT_fffd14ac,_DAT_fffd14b0,uVar5,_DAT_fffd14ac,0x16b);
    *jpeg_dec = (jpeg_dec_handle_t)0x0;
    return JPEG_ERR_NO_MEM;
  }
  jVar15 = config->rotate;
  *puVar6 = 1;
  jVar3 = _DAT_fffd14f4;
  if (JPEG_ROTATE_270D < jVar15) {
    uVar5 = (*_DAT_fffd14d4)();
    (*_DAT_fffd14e8)(1,_DAT_fffd14d8,_DAT_fffd14dc,uVar5,_DAT_fffd14d8);
    goto LAB_0001183c;
  }
  jVar11 = config->output_type;
  puVar6[1] = jVar15;
  if (jVar11 == jVar3) {
    uVar13 = 2;
    uVar9 = 1;
  }
  else {
    if (jVar3 < jVar11) {
      uVar9 = 3;
      jVar3 = _DAT_fffd1510;
    }
    else {
      if (jVar11 == _DAT_fffd1500) {
        uVar13 = 3;
        goto LAB_00011550;
      }
      uVar9 = 2;
      jVar3 = _DAT_fffd1504;
    }
    uVar13 = 2;
    if (jVar11 != jVar3) {
      uVar7 = (*_DAT_fffd151c)();
      pjVar4 = _DAT_fffd1520;
      *_DAT_fffd1520 = 0;
                    /* Unresolved local var: int i@[???] */
      *(undefined1 *)(pjVar4 + 1) = 0;
      uVar18 = _DAT_fffd1530;
      uVar5 = _DAT_fffd152c;
      *pjVar4 = config->output_type;
      (*_DAT_fffd153c)(1,uVar5,uVar18,uVar7,uVar5);
      goto LAB_0001183c;
    }
  }
LAB_00011550:
  *(undefined1 *)((int)puVar6 + 0x53) = uVar9;
  *(undefined1 *)((int)puVar6 + 0x52) = uVar13;
  *(undefined1 *)(puVar6 + 0x2fc) = 1;
  iVar8 = (*_DAT_fffd1564)(0x600,0x10);
  if (iVar8 == 0) {
    uVar5 = (*_DAT_fffd156c)();
    (*_DAT_fffd1584)(1,_DAT_fffd1574,_DAT_fffd1574,uVar5,_DAT_fffd1574,400);
    goto LAB_0001183c;
  }
  puVar6[0x17] = iVar8;
  puVar6[0x18] = iVar8 + 0x200;
  puVar6[0x19] = iVar8 + 0x280;
  iVar8 = (*_DAT_fffd15a4)(0x600,0x10);
  if (iVar8 == 0) {
    uVar5 = (*_DAT_fffd15b0)();
    (*_DAT_fffd15c4)(1,_DAT_fffd15b4,_DAT_fffd15b8,uVar5,_DAT_fffd15b4,0x199);
    goto LAB_0001183c;
  }
  puVar6[0x1a] = iVar8;
  puVar6[0x1b] = iVar8 + 0x200;
  puVar6[0x1c] = iVar8 + 0x280;
  iVar8 = (*_DAT_fffd15e0)(0x200,0x10);
  if (iVar8 == 0) {
    uVar5 = (*_DAT_fffd15e8)();
    (*_DAT_fffd1600)(1,_DAT_fffd15f0,_DAT_fffd15f0,uVar5,_DAT_fffd15f0,0x1a2);
    goto LAB_0001183c;
  }
  puVar6[9] = iVar8;
  puVar6[10] = iVar8 + 0x80;
  puVar6[0xb] = iVar8 + 0x100;
  puVar6[0xc] = iVar8 + 0x180;
  iVar8 = (*_DAT_fffd1620)(0x400);
  if (iVar8 == 0) {
    uVar5 = (*_DAT_fffd1650)();
    (*_DAT_fffd1664)(1,_DAT_fffd1654,_DAT_fffd1658,uVar5,_DAT_fffd1654,0x1ac);
    goto LAB_0001183c;
  }
                    /* Unresolved local var: int i@[???]
                       Unresolved local var: int j@[???] */
  puVar6[0xab] = iVar8;
  puVar6[0xac] = iVar8 + 0x100;
  puVar6[0xad] = iVar8 + 0x200;
  puVar6[0xae] = iVar8 + 0x300;
  iVar8 = (*_DAT_fffd1640)(0x400);
  if (iVar8 == 0) {
    uVar5 = (*_DAT_fffd1694)();
    (*_DAT_fffd16a8)(1,_DAT_fffd1698,_DAT_fffd169c,uVar5,_DAT_fffd1698,0x1b7);
    goto LAB_0001183c;
  }
                    /* Unresolved local var: int i@[???]
                       Unresolved local var: int j@[???] */
  puVar6[0xaf] = iVar8;
  puVar6[0xb1] = iVar8 + 0x200;
  puVar6[0xb2] = iVar8 + 0x300;
  puVar6[0xb0] = iVar8 + 0x100;
  uVar12 = (uint)(config->clipper).width;
  *(undefined2 *)(puVar6 + 0x2c5) = 0;
  uVar16 = (uint)(config->scale).width;
  if (uVar12 != 0) {
    if (uVar16 == 0) goto LAB_000116d4;
    if (uVar12 <= uVar16) goto LAB_0001185e;
    uVar7 = (*_DAT_fffd16bc)();
    uVar10 = (config->scale).width;
    uVar2 = (config->clipper).width;
    uVar5 = _DAT_fffd16c0;
    uVar18 = _DAT_fffd16cc;
LAB_00011730:
    (*_DAT_fffd173c)(1,uVar5,uVar18,uVar7,uVar5,uVar2,uVar10);
    goto LAB_0001183c;
  }
  uVar12 = uVar16;
  if (uVar16 == 0) {
LAB_000116d4:
    uVar12 = (uint)*(ushort *)(puVar6 + 0x2c3);
    if (uVar12 != 0) goto LAB_000116d9;
  }
  else {
LAB_0001185e:
    *(short *)(puVar6 + 0x2c3) = (short)uVar12;
    *(undefined1 *)(puVar6 + 0x2c5) = 1;
LAB_000116d9:
    iVar8 = (*_DAT_fffd16e0)((uVar12 + 1) * 4,0x10);
    puVar6[0x2c7] = iVar8;
    if (iVar8 == 0) {
      uVar5 = (*_DAT_fffd16ec)();
      (*_DAT_fffd1700)(1,_DAT_fffd16f0,_DAT_fffd16f4,uVar5,_DAT_fffd16f0,0x1d0);
      goto LAB_0001183c;
    }
  }
  jVar17 = config->scale;
  *(jpeg_resolution_t *)(puVar6 + 0x2c4) = jVar17;
  uVar14 = (config->clipper).height;
  uVar16 = (uint)uVar14;
  uVar12 = (uint)jVar17 >> 0x10;
  if (uVar16 == 0) {
    uVar16 = uVar12;
    if (uVar12 != 0) goto LAB_00011850;
    uVar12 = (uint)*(ushort *)((int)puVar6 + 0xb0e);
    if (uVar12 != 0) goto LAB_0001176c;
    uVar10 = (config->clipper).width;
    uVar16 = 0;
    if (uVar10 != 0) goto LAB_000117ae;
    uVar10 = *(uint16_t *)(puVar6 + 0x2c3);
  }
  else {
    if (uVar12 == 0) {
      uVar12 = (uint)*(ushort *)((int)puVar6 + 0xb0e);
      if (uVar12 != 0) goto LAB_0001176c;
      uVar10 = (config->clipper).width;
      if (uVar10 != 0) goto LAB_000117ae;
      uVar10 = *(uint16_t *)(puVar6 + 0x2c3);
    }
    else {
      if (uVar12 < uVar16) {
        uVar7 = (*_DAT_fffd1720)();
        uVar10 = (config->scale).height;
        uVar2 = (config->clipper).height;
        uVar5 = _DAT_fffd1724;
        uVar18 = _DAT_fffd1730;
        goto LAB_00011730;
      }
LAB_00011850:
      *(short *)((int)puVar6 + 0xb0e) = (short)uVar16;
      *(undefined1 *)(puVar6 + 0x2c5) = 1;
      uVar12 = uVar16;
LAB_0001176c:
      iVar8 = (*_DAT_fffd1774)((uVar12 + 1) * 4,0x10);
      puVar6[0x2c6] = iVar8;
      if (iVar8 == 0) {
        uVar5 = (*_DAT_fffd178c)();
        (*_DAT_fffd17a0)(1,_DAT_fffd1790,_DAT_fffd1794,uVar5,_DAT_fffd1790,0x1e5);
        goto LAB_0001183c;
      }
      uVar10 = (config->clipper).width;
      uVar16 = (uint)(config->clipper).height;
      if (uVar10 == 0) {
        uVar10 = *(uint16_t *)(puVar6 + 0x2c3);
      }
      else {
LAB_000117ae:
        *(undefined1 *)((int)puVar6 + 0xb15) = 1;
        *(uint16_t *)(puVar6 + 0x2c3) = uVar10;
      }
      uVar14 = (ushort)uVar16;
      if (uVar16 == 0) goto LAB_000117c1;
    }
    *(undefined1 *)((int)puVar6 + 0xb15) = 1;
    *(ushort *)((int)puVar6 + 0xb0e) = uVar14;
  }
LAB_000117c1:
  if ((uVar10 & 7) == 0) {
    _Var1 = config->block_enable;
    *(_Bool *)((int)puVar6 + 0xbf3) = _Var1;
    if (_Var1 == false) {
LAB_00011834:
      *jpeg_dec = puVar6;
      return JPEG_ERR_OK;
    }
    if ((*(char *)(puVar6 + 0x2c5) == '\0') && (*(char *)((int)puVar6 + 0xb15) == '\0')) {
      if (puVar6[1] == 0) goto LAB_00011834;
      uVar5 = (*_DAT_fffd1818)();
      (*_DAT_fffd182c)(1,_DAT_fffd1820,_DAT_fffd1820,uVar5,_DAT_fffd1820);
    }
    else {
      uVar5 = (*_DAT_fffd17f8)();
      (*_DAT_fffd180c)(1,_DAT_fffd1800,_DAT_fffd1804,uVar5,_DAT_fffd1800);
    }
  }
  else {
    uVar5 = (*_DAT_fffd17c8)();
    (*_DAT_fffd17dc)(1,_DAT_fffd17cc,_DAT_fffd17d4,uVar5,_DAT_fffd17cc,
                     *(undefined2 *)(puVar6 + 0x2c3));
  }
LAB_0001183c:
  (*_DAT_fffd1840)(puVar6);
  *jpeg_dec = (jpeg_dec_handle_t)0x0;
  return JPEG_ERR_FAIL;
}

/* ==================================================================
 * jpeg_dec_get_outbuf_len
 * Purpose: Calculates the required output-buffer size for current dimensions and selected pixel format.
 * Entry: 000118d0
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_get_outbuf_len(jpeg_dec_handle_t jpeg_dec,int *outbuf_len)

{
  byte bVar1;
  char cVar2;
  ushort uVar3;
  undefined4 uVar4;
  jpeg_error_t jVar5;

                    /* Unresolved local var: jpeg_decoder_t * jd@[???] */
  if ((jpeg_dec == (jpeg_dec_handle_t)0x0) ||
     (outbuf_len == (int *)0x0 || jpeg_dec == (jpeg_dec_handle_t)0x0)) {
    uVar4 = (*_DAT_fffd18e4)();
    (*_DAT_fffd18fc)(1,_DAT_fffd18ec,_DAT_fffd18f0,uVar4,_DAT_fffd18ec,jpeg_dec,outbuf_len);
    jVar5 = JPEG_ERR_INVALID_PARAM;
  }
  else if (*(char *)((int)jpeg_dec + 0xbf1) == '\0') {
    uVar4 = (*_DAT_fffd1910)();
    (*_DAT_fffd1920)(1,_DAT_fffd1914,_DAT_fffd1918,uVar4,_DAT_fffd1914);
    jVar5 = JPEG_ERR_FAIL;
  }
  else {
    uVar3 = *(ushort *)((int)jpeg_dec + 0xb0c);
    bVar1 = *(byte *)((int)jpeg_dec + 0x52);
    cVar2 = *(char *)((int)jpeg_dec + 0xbf3);
    *outbuf_len = (uint)*(ushort *)((int)jpeg_dec + 0xb0e) * (uint)uVar3 * (uint)bVar1;
    if (cVar2 != '\0') {
      *outbuf_len = *(short *)((int)jpeg_dec + 0x36) * 8 * (uint)uVar3 * (uint)bVar1;
    }
    jVar5 = JPEG_ERR_OK;
  }
  return jVar5;
}

/* ==================================================================
 * jpeg_dec_get_process_count
 * Purpose: Returns the number of block callbacks/process iterations needed for the prepared image.
 * Entry: 0001199c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_get_process_count(jpeg_dec_handle_t jpeg_dec,int *process_count)

{
  char cVar1;
  undefined4 uVar2;
  jpeg_error_t jVar3;
  int iVar4;

                    /* Unresolved local var: jpeg_decoder_t * jd@[???] */
  if ((jpeg_dec == (jpeg_dec_handle_t)0x0) ||
     (process_count == (int *)0x0 || jpeg_dec == (jpeg_dec_handle_t)0x0)) {
    uVar2 = (*_DAT_fffd19b0)();
    (*_DAT_fffd19c8)(1,_DAT_fffd19b8,_DAT_fffd19bc,uVar2,_DAT_fffd19b8,jpeg_dec,process_count);
    jVar3 = JPEG_ERR_INVALID_PARAM;
  }
  else if (*(char *)((int)jpeg_dec + 0xbf1) == '\0') {
    uVar2 = (*_DAT_fffd19dc)();
    (*_DAT_fffd19ec)(1,_DAT_fffd19e0,_DAT_fffd19e4,uVar2,_DAT_fffd19e0);
    jVar3 = JPEG_ERR_FAIL;
  }
  else {
    cVar1 = *(char *)((int)jpeg_dec + 0xbf3);
    *process_count = 1;
    if (cVar1 != '\0') {
      (*_DAT_fffd1a1c)((float)*(ushort *)((int)jpeg_dec + 0xb0e) / 1.0,
                       (float)((int)*(short *)((int)jpeg_dec + 0x36) << 3) / 1.0);
      (*_DAT_fffd1a20)();
      (*_DAT_fffd1a28)();
      iVar4 = (*_DAT_fffd1a2c)();
      *process_count = iVar4;
    }
    jVar3 = JPEG_ERR_OK;
  }
  return jVar3;
}
