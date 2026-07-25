#include "cyd_display.hpp"

#include <algorithm>
#include <cstring>

#include "board_config.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "hal/lcd_types.h"
#include "player_settings.hpp"

namespace {

constexpr char kTag[] = "display";
constexpr size_t kDmaBufferCount = 2;

}  // namespace

bool CydDisplay::onColorTransferDone(
    esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *,
    void *user_context) {
    auto *display = static_cast<CydDisplay *>(user_context);
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR(display->transfer_done_, &task_woken);
    return task_woken == pdTRUE;
}

esp_err_t CydDisplay::init() {
    transfer_done_ = xSemaphoreCreateCounting(kDmaBufferCount, 0);
    ESP_RETURN_ON_FALSE(transfer_done_, ESP_ERR_NO_MEM, kTag,
                        "LCD completion semaphore allocation failed");
    ESP_RETURN_ON_ERROR(setDoubleBuffered(true), kTag,
                        "LCD secondary DMA buffer allocation failed");

    spi_bus_config_t bus{};
    bus.mosi_io_num = board::kTftMosi;
    bus.miso_io_num = board::kTftMiso;
    bus.sclk_io_num = board::kTftSck;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.data4_io_num = GPIO_NUM_NC;
    bus.data5_io_num = GPIO_NUM_NC;
    bus.data6_io_num = GPIO_NUM_NC;
    bus.data7_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = sizeof primary_dma_buffer_;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO),
                        kTag, "LCD SPI2 DMA initialization failed");

    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num = board::kTftCs;
    io_config.dc_gpio_num = board::kTftDc;
    io_config.spi_mode = 0;
    io_config.pclk_hz = player_settings::kDisplayClockHz;
    io_config.trans_queue_depth = kDmaBufferCount;
    io_config.on_color_trans_done = onColorTransferDone;
    io_config.user_ctx = this;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi(
            static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config,
            &io_),
        kTag, "ST7789 SPI panel IO creation failed");

    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    // The conversion buffers contain conventional RGB565.  This CYD2USB
    // panel expects the controller's RGB element order; BGR swaps red/blue.
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    // rgb565 values are produced in the ESP32's native little-endian order.
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
    panel_config.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7789(io_, &panel_config, &panel_), kTag,
        "ST7789 panel creation failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_), kTag,
                        "ST7789 reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_), kTag,
                        "ST7789 initialization failed");

    // Equivalent to LovyanGFX rotation=1 for the 240x320 panel.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel_, true), kTag,
                        "ST7789 axis swap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel_, true, false), kTag,
                        "ST7789 mirror setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_, false), kTag,
                        "ST7789 color mode setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_, true), kTag,
                        "ST7789 display enable failed");

    gpio_config_t backlight{};
    backlight.pin_bit_mask = 1ULL << board::kTftBacklight;
    backlight.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), kTag,
                        "Backlight GPIO setup failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(board::kTftBacklight, 1), kTag,
                        "Backlight enable failed");

    ESP_LOGI(kTag, "ST7789: SPI2 DMA, %d Hz, double %dx%d-row buffers",
             player_settings::kDisplayClockHz, kWidth, kRowsPerTransfer);
    return clear(0x0000);
}

uint16_t *CydDisplay::acquireBuffer() {
    if (transfers_in_flight_ == dma_buffer_count_) {
        if (xSemaphoreTake(transfer_done_, portMAX_DELAY) != pdTRUE) {
            return nullptr;
        }
        --transfers_in_flight_;
    }
    uint16_t *buffer = primary_dma_buffer_;
    if (next_buffer_ != 0) {
        buffer = secondary_dma_buffer_
                     ? secondary_dma_buffer_
                     : primary_dma_buffer_ +
                           kWidth * (kRowsPerTransfer / 2);
    }
    next_buffer_ = (next_buffer_ + 1) % dma_buffer_count_;
    return buffer;
}

esp_err_t CydDisplay::setDoubleBuffered(bool enabled) {
    ESP_RETURN_ON_ERROR(flush(), kTag,
                        "LCD DMA flush before buffer change failed");
    if (enabled && !secondary_dma_buffer_) {
        secondary_dma_buffer_ = static_cast<uint16_t *>(heap_caps_malloc(
            sizeof primary_dma_buffer_, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        ESP_RETURN_ON_FALSE(secondary_dma_buffer_, ESP_ERR_NO_MEM, kTag,
                            "LCD secondary DMA buffer unavailable");
    } else if (!enabled && secondary_dma_buffer_) {
        heap_caps_free(secondary_dma_buffer_);
        secondary_dma_buffer_ = nullptr;
    }
    dma_buffer_count_ = 2;
    rows_per_transfer_ =
        secondary_dma_buffer_ ? kRowsPerTransfer
                              : kRowsPerTransfer / 2;
    next_buffer_ = 0;
    return ESP_OK;
}

esp_err_t CydDisplay::drawBitmap(int x, int y, int width, int height,
                                 const uint16_t *pixels) {
    ESP_RETURN_ON_FALSE(panel_ && pixels && width > 0 && height > 0,
                        ESP_ERR_INVALID_ARG, kTag,
                        "Invalid LCD bitmap submission");
    const esp_err_t result = esp_lcd_panel_draw_bitmap(
        panel_, x, y, x + width, y + height, pixels);
    if (result == ESP_OK) ++transfers_in_flight_;
    return result;
}

esp_err_t CydDisplay::flush() {
    while (transfers_in_flight_) {
        ESP_RETURN_ON_FALSE(
            xSemaphoreTake(transfer_done_, pdMS_TO_TICKS(1000)) == pdTRUE,
            ESP_ERR_TIMEOUT, kTag, "LCD DMA completion timed out");
        --transfers_in_flight_;
    }
    return ESP_OK;
}

esp_err_t CydDisplay::clear(uint16_t rgb565) {
    for (int y = 0; y < kHeight; y += rows_per_transfer_) {
        const int rows = std::min(rows_per_transfer_, kHeight - y);
        uint16_t *buffer = acquireBuffer();
        ESP_RETURN_ON_FALSE(buffer, ESP_ERR_NO_MEM, kTag,
                            "LCD DMA buffer unavailable");
        std::fill_n(buffer, kWidth * rows, rgb565);
        ESP_RETURN_ON_ERROR(drawBitmap(0, y, kWidth, rows, buffer), kTag,
                            "LCD clear transfer failed");
    }
    return flush();
}
