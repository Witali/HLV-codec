#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class CydDisplay {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 240;
    static constexpr int kRowsPerTransfer = 16;

    esp_err_t init();
    esp_err_t clear(uint16_t rgb565);

    // acquireBuffer waits only when both DMA buffers are still in flight.
    // The caller fills no more than kWidth*kRowsPerTransfer pixels and submits
    // that exact same pointer with drawBitmap().
    uint16_t *acquireBuffer();
    esp_err_t drawBitmap(int x, int y, int width, int height,
                         const uint16_t *pixels);
    esp_err_t flush();

private:
    static bool onColorTransferDone(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *event_data,
                                    void *user_context);

    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    SemaphoreHandle_t transfer_done_ = nullptr;
    size_t transfers_in_flight_ = 0;
    size_t next_buffer_ = 0;

    alignas(4) uint16_t dma_buffers_[2][kWidth * kRowsPerTransfer]{};
};
