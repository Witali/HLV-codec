/* ESP_NEW_JPEG 1.0.2 ESP32 decoder Ghidra pseudo-C.
 * Derived from the Espressif binary; see LICENSE.ESPRESSIF in the analysis directory.
 * Program: jpeg_dec_parse_header.c.obj
 * Language: Xtensa:LE:32:default
 * Types and names are reconstructed and must not be treated as the original source.
 */

/* ==================================================================
 * jpeg_dec_parse_soi
 * Purpose: Validates the JPEG Start Of Image marker and initializes marker scan state.
 * Entry: 00010024
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_parse_soi(uint8_t **inbuf,int *data_remain)

{
  jpeg_error_t jVar1;
  int iVar2;
  ushort *puVar3;
  int iVar4;
  ushort *puVar5;

                    /* Unresolved local var: int check_data_len@[???]
                       Unresolved local var: uint8_t * check_buf@[???]
                       Unresolved local var: int data_used@[???] */
  iVar4 = *data_remain;
  if (iVar4 < 2) {
    jVar1 = JPEG_ERR_NO_MORE_DATA;
  }
  else {
    puVar3 = (ushort *)*inbuf;
    if (*puVar3 == _DAT_fffd0030) {
      puVar5 = puVar3 + 1;
      iVar4 = iVar4 + -2;
LAB_0001007c:
      *inbuf = (uint8_t *)puVar5;
      *data_remain = iVar4;
      jVar1 = JPEG_ERR_OK;
    }
    else {
      if (iVar4 != 2) {
        iVar2 = (int)puVar3 + ((uint)puVar3 ^ 0xffffffff) + iVar4 + -1;
        puVar5 = puVar3;
        do {
          if (*(ushort *)((int)puVar5 + 1) == _DAT_fffd0030) {
            iVar4 = iVar4 - ((int)puVar5 - ((int)puVar3 + -3));
            puVar5 = (ushort *)((int)puVar5 + 3);
            goto LAB_0001007c;
          }
          iVar2 = iVar2 + -1;
          puVar5 = (ushort *)((int)puVar5 + 1);
        } while (iVar2 != 0);
      }
      jVar1 = JPEG_ERR_BAD_DATA;
    }
  }
  return jVar1;
}

/* ==================================================================
 * jpeg_dec_parse_sof0
 * Purpose: Parses baseline SOF0 dimensions, component sampling factors and quantization-table assignments.
 * Entry: 000100c8
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_parse_sof0(jpeg_decoder_t *jd,uint8_t *seg,uint16_t ndata)

{
  undefined4 uVar1;
  uint uVar2;
  ushort uVar3;
  uint uVar4;

                    /* Unresolved local var: uint8_t b@[???] */
  jd->height = *(ushort *)(seg + 1) << 8 | *(ushort *)(seg + 1) >> 8;
  jd->width = *(ushort *)(seg + 3) << 8 | *(ushort *)(seg + 3) >> 8;
  uVar4 = (uint)seg[5];
  jd->component_num = uVar4;
  if ((seg[5] & 0xfd) == 1) {
    uVar3 = 8;
    if (seg[5] != 1) {
      uVar3 = 0xe;
    }
    if (ndata <= uVar3) {
      return JPEG_ERR_NO_MORE_DATA;
    }
    jd->component_id[0] = seg[6];
    uVar2 = (uint)(seg[7] >> 4) | (seg[7] & 0xf) << 0x10;
    jd->msx = (short)uVar2;
    jd->msy = (short)(uVar2 >> 0x10);
    if (seg[8] < 4) {
      jd->qtid[0] = seg[8];
                    /* Unresolved local var: int i@[???] */
      if (1 < uVar4) {
        jd->component_id[1] = seg[seg[5] + 6];
        if (3 < seg[0xb]) {
          return JPEG_ERR_UNSUPPORT_STD;
        }
        jd->qtid[1] = seg[0xb];
        if (uVar4 != 2) {
          jd->component_id[2] = seg[(uint)seg[5] * 2 + 6];
          if (3 < seg[0xe]) {
            return JPEG_ERR_UNSUPPORT_STD;
          }
          jd->qtid[2] = seg[0xe];
        }
      }
      return JPEG_ERR_OK;
    }
  }
  else {
    uVar1 = (*_DAT_fffd0110)();
    (*_DAT_fffd0124)(1,_DAT_fffd0114,_DAT_fffd0118,uVar1,_DAT_fffd0114);
  }
  return JPEG_ERR_UNSUPPORT_STD;
}

/* ==================================================================
 * jpeg_dec_parse_sos
 * Purpose: Parses scan component selectors and DC/AC Huffman table assignments, then positions input at entropy data.
 * Entry: 0001019c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_parse_sos(jpeg_decoder_t *jd,jpeg_dec_io_t *io)

{
  bool bVar1;
  ushort *puVar2;
  uint uVar3;
  code *pcVar4;
  uint uVar5;
  jpeg_rotate_t jVar6;
  int iVar7;
  ushort *puVar8;
  uint8_t *puVar9;
  int iVar10;

                    /* Unresolved local var: int check_data_len@[???]
                       Unresolved local var: uint8_t * check_buf@[???]
                       Unresolved local var: int remain_data@[???]
                       Unresolved local var: uint16_t len@[???]
                       Unresolved local var: int data_used@[???] */
  iVar7 = io->inbuf_remain;
  if (1 < iVar7) {
    puVar2 = (ushort *)jd->dptr;
    if (*puVar2 != _DAT_fffd01b0) {
      if (iVar7 != 2) {
        iVar7 = (int)puVar2 + ((uint)puVar2 ^ 0xffffffff) + iVar7 + -1;
        puVar8 = puVar2;
        do {
          if (*(ushort *)((int)puVar8 + 1) == _DAT_fffd01b0) {
            iVar7 = jd->dctr - ((int)puVar8 - ((int)puVar2 + -3));
            puVar8 = (ushort *)((int)puVar8 + 3);
            goto LAB_0001020c;
          }
          iVar7 = iVar7 + -1;
          puVar8 = (ushort *)((int)puVar8 + 1);
        } while (iVar7 != 0);
      }
      return JPEG_ERR_BAD_DATA;
    }
    puVar8 = puVar2 + 1;
    iVar7 = jd->dctr + -2;
LAB_0001020c:
    uVar3 = (*puVar8 & 0xff) << 8 | (uint)(*puVar8 >> 8);
    if ((2 < uVar3) && ((int)uVar3 <= iVar7)) {
      uVar5 = (uint)(byte)puVar8[1];
      if ((uVar5 & 0xfffffffd) == 1) {
        iVar10 = 0;
        puVar2 = puVar8 + 2;
        puVar9 = jd->qtid;
                    /* Unresolved local var: int i@[???]
                       Unresolved local var: int b@[???] */
        while( true ) {
          bVar1 = iVar10 != 0;
          if ((int)uVar5 <= iVar10) {
            jVar6 = jd->rotate;
            jd->dctr = (iVar7 + -2) - (uVar3 - 2 & 0xffff);
            iVar7 = _DAT_fffd02bc;
            jd->dptr = (uint8_t *)((int)puVar8 + uVar3);
            jd->rsc = 0;
            jd->rst = 0;
            jd->get_buffer = 0;
            jd->bits_left = 0;
            jd->dcv[0] = 0;
            jd->dcv[1] = 0;
            jd->dcv[2] = 0;
            jd->block_output_line = 0;
            pcVar4 = *(code **)(jVar6 * 4 + iVar7);
            io->out_size = 0;
            (*pcVar4)(jd,io);
            return JPEG_ERR_OK;
          }
          uVar5 = (uint)(byte)*puVar2;
          if ((uVar5 != 0) && (uVar5 != 0x11)) break;
          puVar9[0x51] = (byte)((int)uVar5 >> 4);
          puVar9[0x54] = (byte)(uVar5 & 0xf);
          iVar10 = iVar10 + 1;
          if (jd->qttbl[*puVar9] == (int16_t *)0x0) {
            return JPEG_ERR_BAD_DATA;
          }
          puVar9 = puVar9 + 1;
          if (jd->huffcode[bVar1][uVar5 & 0xf] == (uint16_t *)0x0) {
            return JPEG_ERR_BAD_DATA;
          }
          puVar2 = puVar2 + 1;
          if (jd->huffcode[bVar1][(int)uVar5 >> 4] == (uint16_t *)0x0) {
            return JPEG_ERR_BAD_DATA;
          }
          uVar5 = (uint)(byte)puVar8[1];
        }
      }
      return JPEG_ERR_UNSUPPORT_STD;
    }
  }
  return JPEG_ERR_NO_MORE_DATA;
}

/* ==================================================================
 * jpeg_dec_parse_dqt
 * Purpose: Parses an 8-bit baseline quantization table and stores it in the decoder's coefficient order.
 * Entry: 000102f0
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

jpeg_error_t jpeg_dec_parse_dqt(jpeg_decoder_t *jd,uint8_t *data,uint16_t ndata)

{
  int iVar1;
  byte *pbVar2;
  int16_t *piVar3;

                    /* Unresolved local var: uint16_t i@[???]
                       Unresolved local var: uint8_t d@[???]
                       Unresolved local var: int16_t * pb@[???] */
  iVar1 = _DAT_fffd0300;
  while( true ) {
    if (ndata == 0) {
      return JPEG_ERR_OK;
    }
    if ((ndata < 0x41) || (0xf < *data)) break;
    piVar3 = jd->qttbl[*data & 3];
    ndata = ndata - 0x41;
    pbVar2 = (byte *)(_DAT_fffd0324 + 1);
    *piVar3 = (ushort)data[1] << 2;
    piVar3[*pbVar2] =
         (int16_t)((int)((uint)data[2] * (uint)*(ushort *)((uint)*pbVar2 * 2 + iVar1) + 0x800) >>
                  0xc);
    data = data + 0x41;
  }
  return JPEG_ERR_BAD_DATA;
}
