#ifndef HLV_PLAYER_SETTINGS_H
#define HLV_PLAYER_SETTINGS_H

#include "sdkconfig.h"

/* H.263 CIF ignores this setting and copies the 320x240 coded area at (16,16)
   pixel-for-pixel. */
#define PLAYER_SCALE_VIDEO_TO_DISPLAY 0

/* Store HLV v14/v15 references as compact Y7/U6/V6 planes with per-plane Q4
   local-average corrections. */
#define PLAYER_USE_COMPACT_HLV_REFERENCE 1

/* The bare-metal-style build keeps only the minimal two-core task topology.
   ESP-IDF's FreeRTOS scheduler acts as a microkernel: app_main owns control,
   I/O and rendering on CPU0, while one ordered decoder worker runs on CPU1. */
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
#define PLAYER_USE_DUAL_CORE_PIPELINE 1
#define PLAYER_ENABLE_AUDIO 0
#define PLAYER_ENABLE_UART_CONTROL 0
#define PLAYER_USE_BOOT_BUTTON_TASK 0
#define PLAYER_ENABLE_BPV_V7_STREAMING_TASK 0
#define PLAYER_LOG_FRAME_TIMINGS 0
#else
/* Decode frame N on CPU1 while CPU0 renders frame N-1. */
#define PLAYER_USE_DUAL_CORE_PIPELINE 1

/* Play supported mono audio through I2S0 PCM-to-PDM on GPIO26. */
#define PLAYER_ENABLE_AUDIO 1
#define PLAYER_ENABLE_UART_CONTROL 1
#define PLAYER_USE_BOOT_BUTTON_TASK 1
#define PLAYER_ENABLE_BPV_V7_STREAMING_TASK 1
#define PLAYER_LOG_FRAME_TIMINGS 1
#endif

/* After this many consecutive late presentation intervals, suppress
   predictive display transfers until the next independently decodable frame. */
#define PLAYER_KEYFRAME_CATCHUP_SKIPS 3U

#define PLAYER_BOOT_BUTTON_POLL_MS 10U
#define PLAYER_BOOT_BUTTON_DEBOUNCE_MS 30U
#define PLAYER_BOOT_BUTTON_LONG_PRESS_MS 800U

#define PLAYER_VIDEO_DIRECTORY "/sdcard/HLV"
#define PLAYER_VIDEO_SELECTION_PATH "/sdcard/HLV/play.txt"
#define PLAYER_SD_CLOCK_KHZ \
    (CONFIG_HLV_PLAYER_SD_SPI_CLOCK_MHZ * 1000)
#define PLAYER_DISPLAY_CLOCK_HZ \
    (CONFIG_HLV_PLAYER_DISPLAY_SPI_CLOCK_MHZ * 1000000)

#endif
