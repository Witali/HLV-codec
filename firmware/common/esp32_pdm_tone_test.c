#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    kPdmDataGpio = GPIO_NUM_26,
    kToneSampleRate = 32000,
    kToneFrequency = 1000,
    kToneSamplesPerCycle = kToneSampleRate / kToneFrequency,
    kToneDmaSamples = 2016,
    kToneDmaDescriptors = 6,
    kToneRampSeconds = 20,
    kToneRampSamples = kToneSampleRate * kToneRampSeconds,
    kGainOneQ24 = 1 << 24,
    kGainStepQ24 = kGainOneQ24 / kToneRampSamples,
    kGainRemainderQ24 = kGainOneQ24 % kToneRampSamples,
};

_Static_assert(kToneSamplesPerCycle == 32,
               "The sine table must contain one exact 1 kHz period");
_Static_assert(kToneDmaSamples % kToneSamplesPerCycle == 0,
               "Every DMA descriptor must end on a sine-cycle boundary");
_Static_assert(sizeof(int16_t) * kToneDmaSamples <= 4092,
               "The PCM chunk must fit one ESP32 DMA descriptor");

static const int16_t kSineQ15[kToneSamplesPerCycle] = {
         0,   6393,  12539,  18204,  23170,  27245,  30273,  32137,
     32767,  32137,  30273,  27245,  23170,  18204,  12539,   6393,
         0,  -6393, -12539, -18204, -23170, -27245, -30273, -32137,
    -32767, -32137, -30273, -27245, -23170, -18204, -12539,  -6393,
};

static __attribute__((aligned(4))) int16_t
    tone_pcm[kToneDmaSamples];

static uint32_t tone_phase;
static uint32_t tone_gain_q24;
static uint32_t tone_gain_remainder;
static uint32_t tone_ramp_samples;
static bool tone_ramp_generated;

static void fillRampPcm(void) {
    for (size_t index = 0; index < kToneDmaSamples; ++index) {
        tone_pcm[index] = (int16_t)(
            ((int64_t)kSineQ15[tone_phase] * tone_gain_q24) >> 24);
        tone_phase = (tone_phase + 1U) & (kToneSamplesPerCycle - 1U);

        if (tone_ramp_samples < kToneRampSamples) {
            ++tone_ramp_samples;
            tone_gain_q24 += kGainStepQ24;
            tone_gain_remainder += kGainRemainderQ24;
            if (tone_gain_remainder >= kToneRampSamples) {
                tone_gain_remainder -= kToneRampSamples;
                ++tone_gain_q24;
            }
            if (tone_ramp_samples == kToneRampSamples) {
                tone_gain_q24 = kGainOneQ24;
                tone_ramp_generated = true;
            }
        }
    }
}

static void fillFullScalePcm(void) {
    for (size_t index = 0; index < kToneDmaSamples; ++index) {
        tone_pcm[index] =
            kSineQ15[index & (kToneSamplesPerCycle - 1U)];
    }
}

static void writePcm(i2s_chan_handle_t tx) {
    size_t written = 0;
    ESP_ERROR_CHECK(i2s_channel_write(
        tx, tone_pcm, sizeof tone_pcm, &written, 1000));
    ESP_ERROR_CHECK(written == sizeof tone_pcm ? ESP_OK : ESP_FAIL);
}

void app_main(void) {
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = kToneDmaDescriptors;
    channel_config.dma_frame_num = kToneDmaSamples;
    channel_config.auto_clear_after_cb = false;
    channel_config.auto_clear_before_cb = false;

    i2s_chan_handle_t tx = NULL;
    ESP_ERROR_CHECK(i2s_new_channel(&channel_config, &tx, NULL));

    /* Espressif's analog-output clock profile keeps the 6.144 MHz carrier
       while using the lower-noise BCLK divider (13 instead of codec mode 8). */
    i2s_pdm_tx_clk_config_t clock_config =
        I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(kToneSampleRate);

    i2s_pdm_tx_config_t pdm_config = {
        .clk_cfg = clock_config,
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = I2S_GPIO_UNUSED,
            .dout = kPdmDataGpio,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_tx_mode(tx, &pdm_config));

    i2s_chan_info_t channel_info = {0};
    ESP_ERROR_CHECK(i2s_channel_get_info(tx, &channel_info));
    esp_rom_printf(
        "PDM TONE 2 READY: SD/display/SPI/DAC disabled, GPIO26 data, "
        "external clock unpinned, %u Hz sine, %u s ramp, PDM clock %u Hz, "
        "analog high-SNR divider 13\n",
        (unsigned)kToneFrequency, (unsigned)kToneRampSeconds,
        (unsigned)channel_info.bclk_hz);

    for (size_t descriptor = 0;
         descriptor < kToneDmaDescriptors;
         ++descriptor) {
        fillRampPcm();
        size_t loaded = 0;
        ESP_ERROR_CHECK(i2s_channel_preload_data(
            tx, tone_pcm, sizeof tone_pcm, &loaded));
        ESP_ERROR_CHECK(loaded == sizeof tone_pcm ? ESP_OK : ESP_FAIL);
    }

    /* Let the one startup UART message finish before PDM activity begins. */
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));

    while (!tone_ramp_generated) {
        fillRampPcm();
        writePcm(tx);
    }

    /* Replace every descriptor with the same exact-cycle full-scale PCM. */
    fillFullScalePcm();
    for (size_t descriptor = 0;
         descriptor < kToneDmaDescriptors;
         ++descriptor) {
        writePcm(tx);
    }

    /* DMA now repeats the unchanged buffers without task-side refills. */
    vTaskDelete(NULL);
}
