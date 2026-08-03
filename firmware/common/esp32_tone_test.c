#include <stdint.h>

#include "driver/dac_continuous.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

enum {
    kToneSampleRate = 32000,
    kToneFrequency = 1000,
    kToneSamplesPerCycle = kToneSampleRate / kToneFrequency,
    kToneBufferSamples = 256,
    kToneRampSeconds = 20,
    kToneRampSamples = kToneSampleRate * kToneRampSeconds,
    kGainOneQ24 = 1 << 24,
    kGainStepQ24 = kGainOneQ24 / kToneRampSamples,
    kGainRemainderQ24 = kGainOneQ24 % kToneRampSamples,
};

_Static_assert(kToneSamplesPerCycle == 32,
               "The sine table must contain one exact 1 kHz period");
_Static_assert(CONFIG_DAC_DMA_AUTO_16BIT_ALIGN,
               "The tone buffer contains unsigned 8-bit DAC samples");

static const int8_t kSineQ7[kToneSamplesPerCycle] = {
    0,    25,   49,   71,   90,   106,  117,  125,
    127,  125,  117,  106,  90,   71,   49,   25,
    0,   -25,  -49,  -71,  -90,  -106, -117, -125,
   -127, -125, -117, -106, -90,  -71,  -49,  -25,
};

static __attribute__((aligned(4))) uint8_t
    tone_buffer[kToneBufferSamples];

void app_main(void) {
    esp_rom_printf(
        "TONE 1 READY: SD/display/SPI disabled, GPIO26 DAC, "
        "%u Hz sine, %u s linear ramp to full scale\n",
        (unsigned)kToneFrequency, (unsigned)kToneRampSeconds);

    dac_continuous_config_t config = {0};
    config.chan_mask = DAC_CHANNEL_MASK_CH1;
    config.desc_num = 4;
    config.buf_size = kToneBufferSamples * 2U;
    config.freq_hz = kToneSampleRate;
    config.offset = 0;
    config.clk_src = DAC_DIGI_CLK_SRC_APLL;
    config.chan_mode = DAC_CHANNEL_MODE_SIMUL;

    dac_continuous_handle_t dac = NULL;
    ESP_ERROR_CHECK(dac_continuous_new_channels(&config, &dac));
    ESP_ERROR_CHECK(dac_continuous_enable(dac));

    /* Let the one startup UART message finish while the output is silent. */
    vTaskDelay(pdMS_TO_TICKS(250));

    uint32_t phase = 0;
    uint32_t gain_q24 = 0;
    uint32_t gain_remainder = 0;
    uint32_t ramp_samples = 0;

    for (;;) {
        for (size_t index = 0; index < sizeof tone_buffer; ++index) {
            const int32_t scaled =
                ((int32_t)kSineQ7[phase] * (int32_t)gain_q24) >> 24;
            tone_buffer[index] = (uint8_t)(128 + scaled);
            phase = (phase + 1U) & (kToneSamplesPerCycle - 1U);

            if (ramp_samples < kToneRampSamples) {
                ++ramp_samples;
                gain_q24 += kGainStepQ24;
                gain_remainder += kGainRemainderQ24;
                if (gain_remainder >= kToneRampSamples) {
                    gain_remainder -= kToneRampSamples;
                    ++gain_q24;
                }
                if (ramp_samples == kToneRampSamples) {
                    gain_q24 = kGainOneQ24;
                }
            }
        }

        ESP_ERROR_CHECK(dac_continuous_write(
            dac, tone_buffer, sizeof tone_buffer, NULL, -1));
    }
}
