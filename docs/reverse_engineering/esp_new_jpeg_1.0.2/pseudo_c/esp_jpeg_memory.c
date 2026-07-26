/* ESP_NEW_JPEG 1.0.2 ESP32 decoder Ghidra pseudo-C.
 * Derived from the Espressif binary; see LICENSE.ESPRESSIF in the analysis directory.
 * Program: esp_jpeg_memory.c.obj
 * Language: Xtensa:LE:32:default
 * Types and names are reconstructed and must not be treated as the original source.
 */

/* ==================================================================
 * jpeg_calloc
 * Purpose: Allocates zero-initialized JPEG work memory using the component's default capability policy.
 * Entry: 00010030
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void * jpeg_calloc(size_t num,size_t size)

{
  void *pvVar1;

                    /* Unresolved local var: void * ptr@[???] */
  pvVar1 = (void *)(*_DAT_fffd0040)(num,size,2,0x404,_DAT_fffd0034);
  return pvVar1;
}

/* ==================================================================
 * jpeg_calloc_inner
 * Purpose: Allocates zero-initialized internal-memory JPEG work storage, preferring capability combinations suitable for decoder tables and state.
 * Entry: 0001004c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void * jpeg_calloc_inner(size_t size)

{
  void *pvVar1;

                    /* Unresolved local var: void * ptr@[???] */
  pvVar1 = (void *)(*_DAT_fffd005c)(1,size,2,_DAT_fffd0050,0x404);
  return pvVar1;
}

/* ==================================================================
 * jpeg_free
 * Purpose: Releases an ordinary JPEG component allocation.
 * Entry: 00010068
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void jpeg_free(void *buf)

{
  (*_DAT_fffd0070)(buf);
  return;
}

/* ==================================================================
 * jpeg_calloc_align
 * Purpose: Allocates zero-initialized JPEG work memory with the requested alignment using the component's default capability policy.
 * Entry: 00010078
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void * jpeg_calloc_align(size_t size,int aligned)

{
  void *pvVar1;

                    /* Unresolved local var: void * ptr@[???] */
  pvVar1 = (void *)(*_DAT_fffd0088)(aligned,1,size,0x404);
  if (pvVar1 == (void *)0x0) {
    pvVar1 = (void *)(*_DAT_fffd009c)(aligned,1,size,_DAT_fffd0094);
  }
  return pvVar1;
}

/* ==================================================================
 * jpeg_calloc_align_inner
 * Purpose: Allocates aligned, zero-initialized internal-memory JPEG work storage with capability-aware heap selection.
 * Entry: 000100a8
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void * jpeg_calloc_align_inner(size_t size,int aligned)

{
  void *pvVar1;

                    /* Unresolved local var: void * ptr@[???] */
  pvVar1 = (void *)(*_DAT_fffd00b8)(aligned,1,size,_DAT_fffd00ac);
  if (pvVar1 == (void *)0x0) {
    pvVar1 = (void *)(*_DAT_fffd00cc)(aligned,1,size,0x404);
  }
  return pvVar1;
}

/* ==================================================================
 * jpeg_free_align
 * Purpose: Releases an aligned JPEG component allocation.
 * Entry: 000100d8
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void jpeg_free_align(undefined4 param_1)

{
  (*_DAT_fffd00e0)(param_1);
  return;
}
