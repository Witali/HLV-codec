#include "cyd_display.h"

#include <string.h>

#include "board_config.h"
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
#include "player_settings.h"

#define CYD_DISPLAY_DMA_BUFFER_COUNT 2U

static const char *const k_tag = "display";

static bool cyd_display_on_color_transfer_done(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *event_data,
    void *user_context) {
    cyd_display_t *display = (cyd_display_t *)user_context;
    BaseType_t task_woken = pdFALSE;
    (void)panel_io;
    (void)event_data;
    xSemaphoreGiveFromISR(display->transfer_done, &task_woken);
    return task_woken == pdTRUE;
}

esp_err_t cyd_display_init(cyd_display_t *display) {
    spi_bus_config_t bus = {0};
    esp_lcd_panel_io_spi_config_t io_config = {0};
    esp_lcd_panel_dev_config_t panel_config = {0};
    gpio_config_t backlight = {0};

    ESP_RETURN_ON_FALSE(display, ESP_ERR_INVALID_ARG, k_tag,
                        "Display context is required");
    display->transfer_done =
        xSemaphoreCreateCounting(CYD_DISPLAY_DMA_BUFFER_COUNT, 0);
    ESP_RETURN_ON_FALSE(display->transfer_done, ESP_ERR_NO_MEM, k_tag,
                        "LCD completion semaphore allocation failed");
    ESP_RETURN_ON_ERROR(cyd_display_set_double_buffered(display, true), k_tag,
                        "LCD secondary DMA buffer allocation failed");

    bus.mosi_io_num = BOARD_TFT_MOSI;
    bus.miso_io_num = BOARD_TFT_MISO;
    bus.sclk_io_num = BOARD_TFT_SCK;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.data4_io_num = GPIO_NUM_NC;
    bus.data5_io_num = GPIO_NUM_NC;
    bus.data6_io_num = GPIO_NUM_NC;
    bus.data7_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = sizeof(display->primary_dma_buffer);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(
                            SPI2_HOST, &bus, SPI_DMA_CH_AUTO),
                        k_tag, "LCD SPI2 DMA initialization failed");

    io_config.cs_gpio_num = BOARD_TFT_CS;
    io_config.dc_gpio_num = BOARD_TFT_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = PLAYER_DISPLAY_CLOCK_HZ;
    io_config.trans_queue_depth = CYD_DISPLAY_DMA_BUFFER_COUNT;
    io_config.on_color_trans_done = cyd_display_on_color_transfer_done;
    io_config.user_ctx = display;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &display->io),
        k_tag, "ST7789 SPI panel IO creation failed");

    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
    panel_config.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7789(
            display->io, &panel_config, &display->panel),
        k_tag, "ST7789 panel creation failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(display->panel), k_tag,
                        "ST7789 reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(display->panel), k_tag,
                        "ST7789 initialization failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(display->panel, true), k_tag,
                        "ST7789 axis swap failed");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_mirror(display->panel, true, false), k_tag,
        "ST7789 mirror setup failed");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_invert_color(display->panel, false), k_tag,
        "ST7789 color mode setup failed");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_disp_on_off(display->panel, true), k_tag,
        "ST7789 display enable failed");

    backlight.pin_bit_mask = 1ULL << BOARD_TFT_BACKLIGHT;
    backlight.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), k_tag,
                        "Backlight GPIO setup failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_TFT_BACKLIGHT, 1), k_tag,
                        "Backlight enable failed");

    ESP_LOGI(k_tag, "ST7789: SPI2 DMA, %d Hz, double %dx%d-row buffers",
             PLAYER_DISPLAY_CLOCK_HZ, CYD_DISPLAY_WIDTH,
             CYD_DISPLAY_ROWS_PER_TRANSFER);
    return cyd_display_clear(display, 0x0000);
}

uint16_t *cyd_display_acquire_buffer(cyd_display_t *display) {
    uint16_t *buffer;
    if (!display) return NULL;
    if (display->transfers_in_flight == display->dma_buffer_count) {
        if (xSemaphoreTake(display->transfer_done, portMAX_DELAY) != pdTRUE) {
            return NULL;
        }
        --display->transfers_in_flight;
    }
    buffer = display->primary_dma_buffer;
    if (display->next_buffer != 0) {
        buffer = display->secondary_dma_buffer
                     ? display->secondary_dma_buffer
                     : display->primary_dma_buffer +
                           CYD_DISPLAY_WIDTH *
                               (CYD_DISPLAY_ROWS_PER_TRANSFER / 2);
    }
    display->next_buffer =
        (display->next_buffer + 1) % display->dma_buffer_count;
    return buffer;
}

esp_err_t cyd_display_set_double_buffered(
    cyd_display_t *display, bool enabled) {
    ESP_RETURN_ON_FALSE(display, ESP_ERR_INVALID_ARG, k_tag,
                        "Display context is required");
    ESP_RETURN_ON_ERROR(cyd_display_flush(display), k_tag,
                        "LCD DMA flush before buffer change failed");
    if (enabled && !display->secondary_dma_buffer) {
        display->secondary_dma_buffer = (uint16_t *)heap_caps_aligned_alloc(
            16, sizeof(display->primary_dma_buffer),
            MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        ESP_RETURN_ON_FALSE(display->secondary_dma_buffer, ESP_ERR_NO_MEM,
                            k_tag, "LCD secondary DMA buffer unavailable");
    } else if (!enabled && display->secondary_dma_buffer) {
        heap_caps_free(display->secondary_dma_buffer);
        display->secondary_dma_buffer = NULL;
    }
    display->dma_buffer_count = CYD_DISPLAY_DMA_BUFFER_COUNT;
    display->rows_per_transfer =
        display->secondary_dma_buffer
            ? CYD_DISPLAY_ROWS_PER_TRANSFER
            : CYD_DISPLAY_ROWS_PER_TRANSFER / 2;
    display->next_buffer = 0;
    return ESP_OK;
}

esp_err_t cyd_display_draw_bitmap(
    cyd_display_t *display, int x, int y, int width, int height,
    const uint16_t *pixels) {
    esp_err_t result;
    ESP_RETURN_ON_FALSE(
        display && display->panel && pixels && width > 0 && height > 0,
        ESP_ERR_INVALID_ARG, k_tag, "Invalid LCD bitmap submission");
    result = esp_lcd_panel_draw_bitmap(
        display->panel, x, y, x + width, y + height, pixels);
    if (result == ESP_OK) ++display->transfers_in_flight;
    return result;
}

esp_err_t cyd_display_flush(cyd_display_t *display) {
    ESP_RETURN_ON_FALSE(display, ESP_ERR_INVALID_ARG, k_tag,
                        "Display context is required");
    while (display->transfers_in_flight) {
        ESP_RETURN_ON_FALSE(
            xSemaphoreTake(
                display->transfer_done, pdMS_TO_TICKS(1000)) == pdTRUE,
            ESP_ERR_TIMEOUT, k_tag, "LCD DMA completion timed out");
        --display->transfers_in_flight;
    }
    return ESP_OK;
}

esp_err_t cyd_display_clear(cyd_display_t *display, uint16_t rgb565) {
    int y;
    ESP_RETURN_ON_FALSE(display, ESP_ERR_INVALID_ARG, k_tag,
                        "Display context is required");
    for (y = 0; y < CYD_DISPLAY_HEIGHT; y += display->rows_per_transfer) {
        int rows = display->rows_per_transfer;
        uint16_t *buffer;
        size_t pixel_count;
        size_t pixel;
        if (rows > CYD_DISPLAY_HEIGHT - y) {
            rows = CYD_DISPLAY_HEIGHT - y;
        }
        buffer = cyd_display_acquire_buffer(display);
        ESP_RETURN_ON_FALSE(buffer, ESP_ERR_NO_MEM, k_tag,
                            "LCD DMA buffer unavailable");
        pixel_count = (size_t)CYD_DISPLAY_WIDTH * (size_t)rows;
        for (pixel = 0; pixel < pixel_count; ++pixel) {
            buffer[pixel] = rgb565;
        }
        ESP_RETURN_ON_ERROR(cyd_display_draw_bitmap(
                                display, 0, y, CYD_DISPLAY_WIDTH, rows,
                                buffer),
                            k_tag, "LCD clear transfer failed");
    }
    return cyd_display_flush(display);
}
