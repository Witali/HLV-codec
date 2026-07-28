#ifndef HLV_PLAYER_SETTINGS_H
#define HLV_PLAYER_SETTINGS_H

#include "sdkconfig.h"

typedef enum {
    /* Keep audio continuous and omit only display transfers that arrive more
       than one frame late. Predictive decoding still runs. */
    PLAYER_AV_SYNC_DROP_LATE_VIDEO_FRAMES,

    /* Present every video frame. When video falls behind, cyclically replay
       the DAC DMA ring until it catches up. */
    PLAYER_AV_SYNC_LOOP_AUDIO_FOR_LATE_VIDEO,

    /* Omit at most PLAYER_MAX_CONSECUTIVE_VIDEO_SKIPS late transfers, then
       replay the DAC DMA ring until video catches up. */
    PLAYER_AV_SYNC_DROP_THEN_LOOP_AUDIO,
} player_av_sync_mode_t;

/* H.263 CIF ignores this setting and copies its central 320x240 coded area
   pixel-for-pixel. */
#define PLAYER_SCALE_VIDEO_TO_DISPLAY 0

/* Store HLV v14 references as compact Y7/U6/V6 planes with per-plane Q4
   local-average corrections. */
#define PLAYER_USE_COMPACT_HLV_REFERENCE 1

/* Decode frame N on CPU1 while CPU0 renders frame N-1. */
#define PLAYER_USE_DUAL_CORE_PIPELINE 1

/* Play supported mono audio through the ESP32 DAC on GPIO26. */
#define PLAYER_ENABLE_AUDIO 1

#define PLAYER_AUDIO_PREROLL_FRAMES 4U
#define PLAYER_MAX_CONSECUTIVE_VIDEO_SKIPS 2U
#define PLAYER_AV_SYNC_MODE PLAYER_AV_SYNC_DROP_THEN_LOOP_AUDIO

/* Emit one compact CSV record per decoded frame. */
#define PLAYER_LOG_FRAME_TIMINGS 1

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
