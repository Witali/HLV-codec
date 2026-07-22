#pragma once

#include <LovyanGFX.hpp>

/*
 * Display configuration for the two-USB ESP32-2432S028 (CYD2USB).
 * This board revision uses an ST7789 on HSPI. VSPI remains independent and is
 * used by the player for DMA reads from the microSD socket.
 */
class LGFX_CYD2USB : public lgfx::LGFX_Device {
    lgfx::Bus_SPI bus_;
    lgfx::Panel_ST7789 panel_;
    lgfx::Light_PWM backlight_;

public:
    LGFX_CYD2USB() {
        {
            auto cfg = bus_.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 80000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = CYD_TFT_SCK;
            cfg.pin_mosi = CYD_TFT_MOSI;
            cfg.pin_miso = CYD_TFT_MISO;
            cfg.pin_dc = CYD_TFT_DC;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }

        {
            auto cfg = panel_.config();
            cfg.pin_cs = CYD_TFT_CS;
            cfg.pin_rst = -1;
            cfg.pin_busy = -1;
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 16;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            panel_.config(cfg);
        }

        {
            auto cfg = backlight_.config();
            cfg.pin_bl = CYD_TFT_BL;
            cfg.invert = false;
            cfg.freq = 12000;
            cfg.pwm_channel = 7;
            backlight_.config(cfg);
            panel_.setLight(&backlight_);
        }

        setPanel(&panel_);
    }
};
