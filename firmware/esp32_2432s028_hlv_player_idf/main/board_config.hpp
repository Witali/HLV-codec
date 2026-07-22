#pragma once

#include "driver/gpio.h"

namespace board {

constexpr gpio_num_t kTftSck = GPIO_NUM_14;
constexpr gpio_num_t kTftMosi = GPIO_NUM_13;
constexpr gpio_num_t kTftMiso = GPIO_NUM_12;
constexpr gpio_num_t kTftCs = GPIO_NUM_15;
constexpr gpio_num_t kTftDc = GPIO_NUM_2;
constexpr gpio_num_t kTftBacklight = GPIO_NUM_21;

constexpr gpio_num_t kSdSck = GPIO_NUM_18;
constexpr gpio_num_t kSdMosi = GPIO_NUM_23;
constexpr gpio_num_t kSdMiso = GPIO_NUM_19;
constexpr gpio_num_t kSdCs = GPIO_NUM_5;

constexpr gpio_num_t kAudioDac = GPIO_NUM_26;

}  // namespace board
