#pragma once

namespace player_settings {

// false: draw at native resolution in the centre with black borders.
// true: stretch every frame to the complete 320x240 display.
constexpr bool kScaleVideoToDisplay = false;

// Store both predictive YUV420 frames as packed Y6/U5/V5 planes. This saves
// 45 KiB at 320x180 (including the decoder's row working area) at the
// cost of reduced colour precision and some additional unpacking work.
constexpr bool kUseCompactY6U5V5 = true;

constexpr char kVideoPath[] = "/sdcard/video.hlv";
constexpr int kSdClockKhz = 20000;

// 40 MHz leaves enough bandwidth for 320x240x16 at 15 fps and is more robust
// on the long PCB traces than the former LovyanGFX 80 MHz setting.
constexpr int kDisplayClockHz = 40000000;

}  // namespace player_settings
