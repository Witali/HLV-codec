#pragma once

#include <cstdint>

#include "sdkconfig.h"

namespace player_settings {

// false: draw at native resolution in the centre with black borders.
// true: stretch every frame to the complete 320x240 display.
// H.263 CIF always ignores this setting and copies the 320x240 area at (16,16)
// pixel-for-pixel.
constexpr bool kScaleVideoToDisplay = false;

// Store both predictive YUV420 frames as packed Y7/U6/V6 planes with a
// separate signed Q4 local-average correction for every 8x8 block in Y, U and
// V. This prevents coherent luma/chroma prediction drift while retaining the
// compact v14 reference representation.
constexpr bool kUseCompactHlvReference = true;

#if defined(HLV_PLAYER_BARE_METAL_STYLE)
// ESP-IDF's FreeRTOS scheduler acts as a small microkernel. app_main owns
// control, I/O and rendering on CPU0; one ordered decoder worker owns CPU1.
constexpr bool kUseDualCorePipeline = true;
constexpr bool kEnableAudio = false;
constexpr bool kEnableUartControl = false;
constexpr bool kUseBootButtonTask = false;
constexpr bool kEnableBpvV7StreamingTask = false;
constexpr bool kLogFrameTimings = false;
#else
// Decode frame N on CPU1 while CPU0 converts and queues frame N-1 to the
// display. Predictive decoding itself remains ordered between frames.
constexpr bool kUseDualCorePipeline = true;

// Play supported mono audio through I2S0 PCM-to-PDM on GPIO26 and use its
// sample counter as the video clock. false (and files without audio) use the
// monotonic ESP timer instead.
constexpr bool kEnableAudio = true;
constexpr bool kEnableUartControl = true;
constexpr bool kUseBootButtonTask = true;
constexpr bool kEnableBpvV7StreamingTask = true;
constexpr bool kLogFrameTimings = true;
#endif

// After this many consecutive late presentations, skip future compressed
// predictive packets before decode until the next independently decodable
// frame. Already decoded frames remain eligible for display.
constexpr unsigned kKeyframeCatchupLateFrames = 3;

constexpr uint32_t kBootButtonPollMs = 10;
constexpr uint32_t kBootButtonDebounceMs = 30;
constexpr uint32_t kBootButtonLongPressMs = 800;

constexpr char kVideoDirectory[] = "/sdcard/HLV";
// play.txt contains one base filename from the same directory. Playback never
// guesses or falls back to another file when this selection is absent.
constexpr char kVideoSelectionPath[] = "/sdcard/HLV/play.txt";
constexpr int kSdClockKhz =
    CONFIG_HLV_PLAYER_SD_SPI_CLOCK_MHZ * 1000;
constexpr int kDisplayClockHz =
    CONFIG_HLV_PLAYER_DISPLAY_SPI_CLOCK_MHZ * 1000000;

}  // namespace player_settings
