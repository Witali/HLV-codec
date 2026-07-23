#pragma once

#include "sdkconfig.h"

namespace player_settings {

// false: draw at native resolution in the centre with black borders.
// true: stretch every frame to the complete 320x240 display.
constexpr bool kScaleVideoToDisplay = false;

// Store both predictive YUV420 frames as packed Y6/U5/V5 planes. This saves
// 45 KiB at 320x180 (including the decoder's row working area) at the
// cost of reduced colour precision and some additional unpacking work.
constexpr bool kUseCompactY6U5V5 = true;

// Decode frame N on CPU1 while CPU0 converts and queues frame N-1 to the
// display. Predictive decoding itself remains ordered between frames.
constexpr bool kUseDualCorePipeline = true;

// Keep this false while isolating the playback reset caused by DAC DMA.
// Audio packets remain in the HLV file but are skipped by the player.
constexpr bool kEnableAudio = false;

constexpr char kVideoPath[] = "/sdcard/video.hlv";
constexpr int kSdClockKhz =
    CONFIG_HLV_PLAYER_SD_SPI_CLOCK_MHZ * 1000;
constexpr int kDisplayClockHz =
    CONFIG_HLV_PLAYER_DISPLAY_SPI_CLOCK_MHZ * 1000000;

}  // namespace player_settings
