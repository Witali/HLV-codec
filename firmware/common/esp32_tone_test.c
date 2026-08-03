#include <stdbool.h>
#include <stddef.h>
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
    kToneDmaSamples = 2016,
    kToneDmaBufferBytes = kToneDmaSamples * 2,
    kToneDmaDescriptors = 6,
    kToneRingSamples = kToneDmaSamples * kToneDmaDescriptors,
    kToneRampSeconds = 20,
    kToneRampSamples = kToneSampleRate * kToneRampSeconds,
    kGainOneQ24 = 1 << 24,
    kGainStepQ24 = kGainOneQ24 / kToneRampSamples,
    kGainRemainderQ24 = kGainOneQ24 % kToneRampSamples,
    kToneRingMilliseconds =
        (kToneRingSamples * 1000 + kToneSampleRate - 1) /
        kToneSampleRate,
};

_Static_assert(kToneSamplesPerCycle == 32,
               "The sine table must contain one exact 1 kHz period");
_Static_assert(kToneDmaSamples % kToneSamplesPerCycle == 0,
               "Every DMA descriptor must end on a sine-cycle boundary");
_Static_assert(CONFIG_DAC_DMA_AUTO_16BIT_ALIGN,
               "The tone source contains unsigned 8-bit DAC samples");

static const int8_t kSineQ7[kToneSamplesPerCycle] = {
    0,    25,   49,   71,   90,   106,  117,  125,
    127,  125,  117,  106,  90,   71,   49,   25,
    0,   -25,  -49,  -71,  -90,  -106, -117, -125,
   -127, -125, -117, -106, -90,  -71,  -49,  -25,
};

static __attribute__((aligned(4))) uint8_t
    tone_ramp_buffer[kToneDmaSamples];
static __attribute__((aligned(4))) uint8_t
    tone_cyclic_buffer[kToneRingSamples];

static uint32_t tone_phase;
static uint32_t tone_gain_q24;
static uint32_t tone_gain_remainder;
static uint32_t tone_ramp_samples;
static volatile bool tone_ramp_generated;
static volatile bool tone_output_failed;

static void fillRampBuffer(void) {
    for (size_t index = 0; index < sizeof tone_ramp_buffer; ++index) {
        const int32_t scaled =
            ((int32_t)kSineQ7[tone_phase] *
             (int32_t)tone_gain_q24) >> 24;
        tone_ramp_buffer[index] = (uint8_t)(128 + scaled);
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

static bool onToneConvertDone(dac_continuous_handle_t handle,
                              const dac_event_data_t *event,
                              void *opaque) {
    (void)opaque;
    fillRampBuffer();

    size_t loaded = 0;
    const esp_err_t result = dac_continuous_write_asynchronously(
        handle, (uint8_t *)event->buf, event->buf_size,
        tone_ramp_buffer, sizeof tone_ramp_buffer, &loaded);
    if (result != ESP_OK || loaded != sizeof tone_ramp_buffer) {
        tone_output_failed = true;
    }
    return false;
}

static void fillCyclicBuffer(void) {
    for (size_t index = 0; index < sizeof tone_cyclic_buffer; ++index) {
        tone_cyclic_buffer[index] =
            (uint8_t)(128 + kSineQ7[index & (kToneSamplesPerCycle - 1U)]);
    }
}

void app_main(void) {
    esp_rom_printf(
        "TONE 2 READY: SD/display/SPI disabled, GPIO26 DAC, "
        "%u Hz sine, %u s ramp, %u x %u-sample DMA ring\n",
        (unsigned)kToneFrequency, (unsigned)kToneRampSeconds,
        (unsigned)kToneDmaDescriptors, (unsigned)kToneDmaSamples);

    fillCyclicBuffer();

    dac_continuous_config_t config = {0};
    config.chan_mask = DAC_CHANNEL_MASK_CH1;
    config.desc_num = kToneDmaDescriptors;
    config.buf_size = kToneDmaBufferBytes;
    config.freq_hz = kToneSampleRate;
    config.offset = 0;
    config.clk_src = DAC_DIGI_CLK_SRC_APLL;
    config.chan_mode = DAC_CHANNEL_MODE_SIMUL;

    dac_continuous_handle_t dac = NULL;
    ESP_ERROR_CHECK(dac_continuous_new_channels(&config, &dac));
    ESP_ERROR_CHECK(dac_continuous_enable(dac));

    dac_event_callbacks_t callbacks = {0};
    callbacks.on_convert_done = onToneConvertDone;
    ESP_ERROR_CHECK(dac_continuous_register_event_callback(
        dac, &callbacks, NULL));

    /* Let the one startup UART message finish before DMA activity begins. */
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(dac_continuous_start_async_writing(dac));

    while (!tone_ramp_generated && !tone_output_failed) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_ERROR_CHECK(tone_output_failed ? ESP_FAIL : ESP_OK);

    /* Let the already generated ramp tail leave the six-descriptor ring. */
    vTaskDelay(pdMS_TO_TICKS(kToneRingMilliseconds));

    ESP_ERROR_CHECK(dac_continuous_stop_async_writing(dac));
    ESP_ERROR_CHECK(dac_continuous_register_event_callback(dac, NULL, NULL));

    size_t loaded = 0;
    ESP_ERROR_CHECK(dac_continuous_write_cyclically(
        dac, tone_cyclic_buffer, sizeof tone_cyclic_buffer, &loaded));
    ESP_ERROR_CHECK(loaded == sizeof tone_cyclic_buffer ? ESP_OK : ESP_FAIL);

    /* The fixed DMA ring now runs without task-side refills or restarts. */
    vTaskDelete(NULL);
}
