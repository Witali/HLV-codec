/* ESP_NEW_JPEG 1.0.2 ESP32 decoder Ghidra pseudo-C.
 * Derived from the Espressif binary; see LICENSE.ESPRESSIF in the analysis directory.
 * Program: esp_jpeg_version.c.obj
 * Language: Xtensa:LE:32:default
 * Types and names are reconstructed and must not be treated as the original source.
 */

/* ==================================================================
 * esp_jpeg_get_version
 * Purpose: Returns the compile-time ESP_NEW_JPEG version string; it is not part of the image decode path.
 * Entry: 0001000c
 * ================================================================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* WARNING: Unknown calling convention -- yet parameter storage is locked */

char * esp_jpeg_get_version(void)

{
  return _DAT_fffd0010;
}
