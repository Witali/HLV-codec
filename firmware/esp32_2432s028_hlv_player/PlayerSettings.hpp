#pragma once

namespace player_settings {

// false: draw at native resolution in the centre with black borders.
// true:  stretch every frame to the complete 320x240 display.
constexpr bool kScaleVideoToDisplay = false;

// The CYD2USB microSD socket is connected to the otherwise unused VSPI bus.
constexpr char kVideoPath[] = "/sdcard/video.hlv";
constexpr int kSdClockKhz = 20000;

}  // namespace player_settings
