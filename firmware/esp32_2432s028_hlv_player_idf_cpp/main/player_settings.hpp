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

    // Keep audio continuous for a short delay by omitting at most
    // kMaxConsecutiveVideoSkips display transfers. If video is still late,
    // cyclically replay the DAC DMA ring until the decoded picture catches up.
    kDropThenLoopAudio,
};

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

// Decode frame N on CPU1 while CPU0 converts and queues frame N-1 to the
// display. Predictive decoding itself remains ordered between frames.
constexpr bool kUseDualCorePipeline = true;

// Play the HLV PCM_U8 mono track through the ESP32 DAC on GPIO26 and use its
// sample counter as the video clock. false (and files without audio) use the
// monotonic ESP timer instead.
constexpr bool kEnableAudio = true;

// Retain the 4 KiB static queue, but wait for this many file-defined frame
// intervals of PCM before playback and after an underrun. Four intervals cover
// the 139 ms SD stall measured with the current 24 fps test file.
constexpr unsigned kAudioPrerollFrames = 4;

// In hybrid mode every predictive frame remains decoded, but at most this many
// consecutive late frames omit their display transfer before audio is held.
constexpr unsigned kMaxConsecutiveVideoSkips = 2;

// Keep playback tied to fps_num/fps_den from the HLV header. The hybrid mode
// bounds visible frame skips while preserving every source audio sample.
constexpr AvSyncMode kAvSyncMode = AvSyncMode::kDropThenLoopAudio;

// Emit one compact CSV record per decoded frame. Timestamps are captured
// before UART output so formatting/transmission is excluded from the reported
// values. Normal ESP-IDF logs are restricted to errors in sdkconfig.defaults
// while this measurement mode is enabled.
constexpr bool kLogFrameTimings = true;

constexpr char kVideoDirectory[] = "/sdcard/HLV";
// play.txt contains one base filename from the same directory. Playback never
// guesses or falls back to another file when this selection is absent.
constexpr char kVideoSelectionPath[] = "/sdcard/HLV/play.txt";
constexpr int kSdClockKhz =
    CONFIG_HLV_PLAYER_SD_SPI_CLOCK_MHZ * 1000;
constexpr int kDisplayClockHz =
    CONFIG_HLV_PLAYER_DISPLAY_SPI_CLOCK_MHZ * 1000000;

}  // namespace player_settings
