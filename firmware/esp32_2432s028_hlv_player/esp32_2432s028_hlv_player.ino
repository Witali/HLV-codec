#include <Arduino.h>
#include <LittleFS.h>
#include <driver/dac_continuous.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>

#include <hlv1.h>

#include "LGFX_CYD2USB.hpp"

namespace {

constexpr char kVideoPath[] = "/littlefs/video.hlv";
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr int kRowsPerTransfer = 8;
constexpr uint32_t kRetryDelayMs = 2000;
constexpr size_t kAudioStreamBytes = 8192;
constexpr size_t kAudioWriteBytes = 256;
constexpr uint32_t kAudioReceiveTimeoutMs = 100;

LGFX_CYD2USB display;
FILE *video_file = nullptr;
HLV1Decoder *decoder = nullptr;
HLV1Header sequence_header{};
uint32_t frame_period_us = 0;
uint32_t next_present_us = 0;
uint32_t decoded_frames = 0;
uint32_t dropped_deadlines = 0;
uint32_t last_retry_ms = 0;
uint16_t rgb_rows[kScreenWidth * kRowsPerTransfer];
StreamBufferHandle_t audio_stream = nullptr;
TaskHandle_t audio_task_handle = nullptr;
dac_continuous_handle_t audio_dac = nullptr;
volatile bool audio_stop_requested = false;
bool audio_enabled = false;
bool audio_started = false;
uint32_t audio_underruns = 0;

int clamp8(int value) {
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

uint16_t yuvToRgb565(int y, int red_add, int green_add, int blue_add) {
    const int luma = 298 * (y > 16 ? y - 16 : 0);
    const int red = clamp8((luma + red_add) >> 8);
    const int green = clamp8((luma + green_add) >> 8);
    const int blue = clamp8((luma + blue_add) >> 8);
    return static_cast<uint16_t>(((red & 0xF8) << 8) |
                                 ((green & 0xFC) << 3) |
                                 (blue >> 3));
}

void showStatus(const char *title, const char *detail = nullptr) {
    display.fillScreen(0x0000);
    display.setTextColor(0xFFFF, 0x0000);
    display.setTextSize(2);
    display.setCursor(8, 12);
    display.println(title);
    if (detail) {
        display.setTextSize(1);
        display.setCursor(8, 44);
        display.println(detail);
    }
}

void audioTask(void *) {
    uint8_t samples[kAudioWriteBytes];
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (!audio_stop_requested) {
        size_t received = xStreamBufferReceive(
            audio_stream, samples, sizeof samples,
            pdMS_TO_TICKS(kAudioReceiveTimeoutMs));
        if (!received) {
            memset(samples, 128, sizeof samples);
            received = sizeof samples;
            ++audio_underruns;
        }
        size_t loaded = 0;
        const esp_err_t result = dac_continuous_write(
            audio_dac, samples, received, &loaded, -1);
        if (result != ESP_OK || loaded != received) {
            Serial.printf("DAC write failed: %d, %u/%u bytes\n",
                          static_cast<int>(result),
                          static_cast<unsigned>(loaded),
                          static_cast<unsigned>(received));
            break;
        }
    }
    audio_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void stopAudio() {
    if (audio_task_handle) {
        audio_stop_requested = true;
        xTaskNotifyGive(audio_task_handle);
        const uint32_t deadline = millis() + 250;
        while (audio_task_handle &&
               static_cast<int32_t>(deadline - millis()) > 0) {
            delay(1);
        }
    }
    if (audio_dac) {
        dac_continuous_disable(audio_dac);
        dac_continuous_del_channels(audio_dac);
        audio_dac = nullptr;
    }
    if (audio_stream) {
        vStreamBufferDelete(audio_stream);
        audio_stream = nullptr;
    }
    audio_task_handle = nullptr;
    audio_stop_requested = false;
    audio_enabled = false;
    audio_started = false;
    audio_underruns = 0;
}

bool prepareAudio(const HLV1Header &header) {
    stopAudio();
    if (!(header.flags & HLV1_FLAG_AUDIO)) return true;

    audio_stream = xStreamBufferCreate(kAudioStreamBytes, kAudioWriteBytes);
    if (!audio_stream) return false;

    dac_continuous_config_t config{};
    config.chan_mask = DAC_CHANNEL_MASK_CH1;
    config.desc_num = 6;
    config.buf_size = kAudioWriteBytes;
    config.freq_hz = header.audio_sample_rate;
    config.offset = 0;
    config.clk_src = DAC_DIGI_CLK_SRC_APLL;
    config.chan_mode = DAC_CHANNEL_MODE_SIMUL;
    if (dac_continuous_new_channels(&config, &audio_dac) != ESP_OK ||
        dac_continuous_enable(audio_dac) != ESP_OK) {
        stopAudio();
        return false;
    }

    audio_stop_requested = false;
    if (xTaskCreate(audioTask, "hlv-audio", 3072, nullptr, 2,
                    &audio_task_handle) != pdPASS) {
        stopAudio();
        return false;
    }
    audio_enabled = true;
    Serial.printf("Audio: PCM_U8 mono %u Hz on DAC GPIO26\n",
                  header.audio_sample_rate);
    return true;
}

bool queueAudio(const HLV1Packet &packet) {
    if (!audio_enabled) return true;
    const uint8_t *data = hlv1_packet_audio_data(&packet);
    size_t remaining = hlv1_packet_audio_size(&packet);
    while (remaining) {
        size_t sent = xStreamBufferSend(
            audio_stream, data, remaining, pdMS_TO_TICKS(1000));
        if (!sent) return false;
        data += sent;
        remaining -= sent;
    }
    return true;
}

void finishAudio() {
    if (!audio_enabled) return;
    uint8_t silence[kAudioWriteBytes];
    memset(silence, 128, sizeof silence);
    const size_t sent = xStreamBufferSend(
        audio_stream, silence, sizeof silence, pdMS_TO_TICKS(1000));
    if (sent != sizeof silence) {
        Serial.println("Could not flush the final audio samples");
    }
}

void startAudio() {
    if (!audio_enabled || audio_started || !audio_task_handle) return;
    audio_started = true;
    xTaskNotifyGive(audio_task_handle);
}

void closeVideo() {
    stopAudio();
    if (decoder) {
        hlv1_decoder_destroy(decoder);
        decoder = nullptr;
    }
    if (video_file) {
        fclose(video_file);
        video_file = nullptr;
    }
}

void reportHeap(const char *stage) {
    Serial.printf("%s: free heap=%u, largest block=%u\n",
                  stage,
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(
                      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

bool openVideo() {
    closeVideo();
    video_file = fopen(kVideoPath, "rb");
    if (!video_file) {
        showStatus("video.hlv missing", "Run upload_esp32.ps1 with a video");
        Serial.printf("Cannot open %s\n", kVideoPath);
        return false;
    }

    const int header_result = hlv1_header_read(video_file, &sequence_header);
    if (header_result != HLV1_OK) {
        Serial.printf("Header error: %s\n", hlv1_strerror(header_result));
        showStatus("Invalid video.hlv", hlv1_strerror(header_result));
        closeVideo();
        return false;
    }
    if (sequence_header.width > kScreenWidth ||
        sequence_header.height > kScreenHeight) {
        showStatus("Video is too large", "Maximum size is 320x240");
        Serial.printf("Unsupported dimensions: %ux%u\n",
                      sequence_header.width, sequence_header.height);
        closeVideo();
        return false;
    }
    if (!prepareAudio(sequence_header)) {
        showStatus("Audio init failed", "DAC GPIO26 could not start");
        Serial.println("Cannot initialize continuous DAC audio");
        closeVideo();
        return false;
    }

    reportHeap("before decoder");
    decoder = hlv1_decoder_create(&sequence_header);
    if (!decoder) {
        showStatus("Not enough RAM", "Use a 256x192 HLV file");
        reportHeap("decoder allocation failed");
        closeVideo();
        return false;
    }

    frame_period_us = static_cast<uint32_t>(
        (1000000ULL * sequence_header.fps_den) / sequence_header.fps_num);
    if (!frame_period_us) frame_period_us = 1;
    next_present_us = micros();
    decoded_frames = 0;
    dropped_deadlines = 0;
    display.fillScreen(0x0000);

    Serial.printf("Playing %ux%u, %u/%u fps, stream v%u\n",
                  sequence_header.width, sequence_header.height,
                  sequence_header.fps_num, sequence_header.fps_den,
                  sequence_header.version);
    reportHeap("decoder ready");
    return true;
}

void waitUntil(uint32_t deadline) {
    for (;;) {
        const int32_t remaining = static_cast<int32_t>(deadline - micros());
        if (remaining <= 0) return;
        if (remaining > 2000) {
            delay(1);
        } else {
            delayMicroseconds(static_cast<uint32_t>(remaining));
        }
    }
}

void renderFrame(const HLV1Frame *frame) {
    const int x_offset = (kScreenWidth - frame->width) / 2;
    const int y_offset = (kScreenHeight - frame->height) / 2;

    display.startWrite();
    for (int y0 = 0; y0 < frame->height; y0 += kRowsPerTransfer) {
        const int rows = min(kRowsPerTransfer, frame->height - y0);
        for (int row = 0; row < rows; ++row) {
            const int y = y0 + row;
            const uint8_t *luma = frame->y + y * frame->stride_y;
            const uint8_t *chroma_u = frame->u + (y >> 1) * frame->stride_u;
            const uint8_t *chroma_v = frame->v + (y >> 1) * frame->stride_v;
            uint16_t *output = rgb_rows + row * frame->width;

            for (int x = 0; x < frame->width; x += 2) {
                const int u = static_cast<int>(chroma_u[x >> 1]) - 128;
                const int v = static_cast<int>(chroma_v[x >> 1]) - 128;
                const int red_add = 409 * v + 128;
                const int green_add = -100 * u - 208 * v + 128;
                const int blue_add = 516 * u + 128;
                output[x] = yuvToRgb565(luma[x], red_add, green_add, blue_add);
                if (x + 1 < frame->width) {
                    output[x + 1] = yuvToRgb565(
                        luma[x + 1], red_add, green_add, blue_add);
                }
            }
        }
        display.pushImage(x_offset, y_offset + y0,
                          frame->width, rows, rgb_rows);
        yield();
    }
    display.endWrite();
}

void failPlayback(const char *title, int result) {
    Serial.printf("%s: %s\n", title, hlv1_strerror(result));
    showStatus(title, hlv1_strerror(result));
    closeVideo();
    last_retry_ms = millis();
}

void playOneFrame() {
    HLV1Packet packet{};
    const int packet_result = hlv1_packet_read(video_file, &packet);
    if (packet_result == HLV1_EOF) {
        finishAudio();
        waitUntil(next_present_us);
        Serial.printf("Loop complete: %u frames, %u late frames, "
                      "%u audio underruns\n",
                      decoded_frames, dropped_deadlines, audio_underruns);
        if (!openVideo()) last_retry_ms = millis();
        return;
    }
    if (packet_result != HLV1_OK) {
        failPlayback("Packet read error", packet_result);
        return;
    }
    if (!queueAudio(packet)) {
        hlv1_packet_free(&packet);
        failPlayback("Audio queue error", HLV1_ERR_IO);
        return;
    }

    const HLV1Frame *frame = nullptr;
    const uint32_t decode_start = micros();
    const int decode_result = hlv1_decoder_decode(decoder, &packet, &frame);
    const uint32_t decode_us = micros() - decode_start;
    hlv1_packet_free(&packet);
    if (decode_result != HLV1_OK) {
        failPlayback("Decode error", decode_result);
        return;
    }

    waitUntil(next_present_us);
    if (!decoded_frames) startAudio();
    const uint32_t render_start = micros();
    renderFrame(frame);
    const uint32_t render_us = micros() - render_start;
    ++decoded_frames;

    next_present_us += frame_period_us;
    const int32_t lateness = static_cast<int32_t>(micros() - next_present_us);
    if (lateness > static_cast<int32_t>(frame_period_us)) {
        ++dropped_deadlines;
        next_present_us = micros();
    }

    if ((decoded_frames % 60U) == 0U) {
        Serial.printf("frame=%u decode=%uus render=%uus late=%u heap=%u\n",
                      decoded_frames, decode_us, render_us, dropped_deadlines,
                      static_cast<unsigned>(ESP.getFreeHeap()));
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(250);

    display.init();
    display.setRotation(1);
    display.setBrightness(255);
    showStatus("HLV-1 player", "Mounting internal flash...");

    if (!LittleFS.begin(false, "/littlefs", 4, "spiffs")) {
        showStatus("LittleFS failed", "Flash firmware and video again");
        Serial.println("LittleFS.begin failed");
        last_retry_ms = millis();
        return;
    }

    Serial.printf("LittleFS: %u/%u bytes used\n",
                  static_cast<unsigned>(LittleFS.usedBytes()),
                  static_cast<unsigned>(LittleFS.totalBytes()));
    if (!openVideo()) last_retry_ms = millis();
}

void loop() {
    if (video_file && decoder) {
        playOneFrame();
        return;
    }

    if (millis() - last_retry_ms >= kRetryDelayMs) {
        last_retry_ms = millis();
        openVideo();
    }
    delay(20);
}
