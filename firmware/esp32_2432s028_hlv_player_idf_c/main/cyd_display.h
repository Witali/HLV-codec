#ifndef HLV_CYD_DISPLAY_H
#define HLV_CYD_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define CYD_DISPLAY_WIDTH 320
#define CYD_DISPLAY_HEIGHT 240
#define CYD_DISPLAY_ROWS_PER_TRANSFER 16

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    SemaphoreHandle_t transfer_done;
    size_t transfers_in_flight;
    size_t next_buffer;
    size_t dma_buffer_count;
    int rows_per_transfer;
    uint16_t primary_dma_buffer[
        CYD_DISPLAY_WIDTH * CYD_DISPLAY_ROWS_PER_TRANSFER]
        __attribute__((aligned(16)));
    uint16_t *secondary_dma_buffer;
} cyd_display_t;

esp_err_t cyd_display_init(cyd_display_t *display);
esp_err_t cyd_display_clear(cyd_display_t *display, uint16_t rgb565);
esp_err_t cyd_display_set_double_buffered(
    cyd_display_t *display, bool enabled);
uint16_t *cyd_display_acquire_buffer(cyd_display_t *display);
esp_err_t cyd_display_draw_bitmap(
    cyd_display_t *display, int x, int y, int width, int height,
    const uint16_t *pixels);
esp_err_t cyd_display_flush(cyd_display_t *display);

static inline int cyd_display_rows_per_transfer(
    const cyd_display_t *display) {
    return display->rows_per_transfer;
}

#ifdef __cplusplus
}
#endif

#endif
