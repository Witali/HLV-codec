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
    esp_err_t setDoubleBuffered(bool enabled);
    int rowsPerTransfer() const { return rows_per_transfer_; }

    // acquireBuffer waits when all currently enabled DMA buffers are in flight.
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
    size_t dma_buffer_count_ = 2;
    int rows_per_transfer_ = kRowsPerTransfer;

    alignas(16) uint16_t primary_dma_buffer_[
        kWidth * kRowsPerTransfer]{};
    uint16_t *secondary_dma_buffer_ = nullptr;
};
