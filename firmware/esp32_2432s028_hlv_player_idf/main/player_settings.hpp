#pragma once

#include "sdkconfig.h"

namespace player_settings {

enum class AvSyncMode {
    // Keep audio continuous and omit only the display transfer of video frames
    // that arrive more than one frame late. Predictive decoding still runs.
    kDropLateVideoFrames,

    // Present every video frame. When video falls behind, stop consuming new
    // PCM and cyclically replay the six DAC DMA descriptors until it catches up.
    kLoopAudioForLateVideo,
};

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

// Play the HLV PCM_U8 mono track through the ESP32 DAC on GPIO26 and use its
// sample counter as the video clock. false (and files without audio) use the
// monotonic ESP timer instead.
constexpr bool kEnableAudio = true;

// Preserve every frame in the current test build. This may make a short audio
// fragment repeat while a slow frame is decoded or transferred to the display.
constexpr AvSyncMode kAvSyncMode = AvSyncMode::kLoopAudioForLateVideo;

constexpr char kVideoPath[] = "/sdcard/video.hlv";
constexpr int kSdClockKhz =
    CONFIG_HLV_PLAYER_SD_SPI_CLOCK_MHZ * 1000;
constexpr int kDisplayClockHz =
    CONFIG_HLV_PLAYER_DISPLAY_SPI_CLOCK_MHZ * 1000000;

}  // namespace player_settings
