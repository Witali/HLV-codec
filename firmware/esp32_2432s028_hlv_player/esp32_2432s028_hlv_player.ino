#include <Arduino.h>
#include <driver/dac_continuous.h>
#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>

#include <hlv1.h>

#include "HlvEsp32Decoder.hpp"
#include "LGFX_CYD2USB.hpp"
#include "PlayerSettings.hpp"

namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr int kRowsPerTransfer = 8;
constexpr uint32_t kRetryDelayMs = 2000;
constexpr size_t kAudioStreamBytes = 8192;
constexpr size_t kAudioWriteBytes = 256;
constexpr uint32_t kAudioReceiveTimeoutMs = 100;
constexpr int kAudioWriteTimeoutMs = 100;

LGFX_CYD2USB display;
FILE *video_file = nullptr;
HlvEsp32Decoder decoder;
HLV1Header sequence_header{};
uint32_t frame_period_us = 0;
uint32_t next_present_us = 0;
uint32_t decoded_frames = 0;
uint32_t dropped_deadlines = 0;
uint32_t last_retry_ms = 0;
uint64_t sd_read_us_total = 0;
uint32_t sd_read_us_max = 0;
uint16_t rgb_rows[kScreenWidth * kRowsPerTransfer];
uint16_t scaled_rgb_row[kScreenWidth];
uint16_t scaled_x_map[kScreenWidth];
uint16_t scaled_y_map[kScreenHeight];
sdmmc_card_t *sd_card = nullptr;
bool sd_bus_initialized = false;
bool sd_mounted = false;
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

void convertNativeRow(const HLV1Frame *frame, int source_y,
                      uint16_t *output) {
    const uint8_t *luma = frame->y + source_y * frame->stride_y;
    const uint8_t *chroma_u =
        frame->u + (source_y >> 1) * frame->stride_u;
    const uint8_t *chroma_v =
        frame->v + (source_y >> 1) * frame->stride_v;

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

void convertScaledRow(const HLV1Frame *frame, int source_y,
                      uint16_t *output) {
    const uint8_t *luma = frame->y + source_y * frame->stride_y;
    const uint8_t *chroma_u =
        frame->u + (source_y >> 1) * frame->stride_u;
    const uint8_t *chroma_v =
        frame->v + (source_y >> 1) * frame->stride_v;
    int previous_chroma_x = -1;
    int red_add = 0;
    int green_add = 0;
    int blue_add = 0;

    for (int output_x = 0; output_x < kScreenWidth; ++output_x) {
        const int source_x = scaled_x_map[output_x];
        const int chroma_x = source_x >> 1;
        if (chroma_x != previous_chroma_x) {
            const int u = static_cast<int>(chroma_u[chroma_x]) - 128;
            const int v = static_cast<int>(chroma_v[chroma_x]) - 128;
            red_add = 409 * v + 128;
            green_add = -100 * u - 208 * v + 128;
            blue_add = 516 * u + 128;
            previous_chroma_x = chroma_x;
        }
        output[output_x] = yuvToRgb565(
            luma[source_x], red_add, green_add, blue_add);
    }
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

bool mountSdCard() {
    if (sd_mounted) return true;

    if (!sd_bus_initialized) {
        spi_bus_config_t bus{};
        bus.mosi_io_num = CYD_SD_MOSI;
        bus.miso_io_num = CYD_SD_MISO;
        bus.sclk_io_num = CYD_SD_SCK;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.data4_io_num = -1;
        bus.data5_io_num = -1;
        bus.data6_io_num = -1;
        bus.data7_io_num = -1;
        bus.max_transfer_sz = HlvEsp32Decoder::kPacketBlockBytes;
        const esp_err_t bus_result = spi_bus_initialize(
            SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
        if (bus_result != ESP_OK) {
            Serial.printf("SD SPI3 DMA init failed: %s\n",
                          esp_err_to_name(bus_result));
            return false;
        }
        sd_bus_initialized = true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = player_settings::kSdClockKhz;

    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SPI3_HOST;
    device.gpio_cs = static_cast<gpio_num_t>(CYD_SD_SS);

    esp_vfs_fat_mount_config_t mount{};
    mount.format_if_mount_failed = false;
    mount.max_files = 1;
    mount.allocation_unit_size = 16 * 1024;
    mount.disk_status_check_enable = false;
    mount.use_one_fat = false;

    const esp_err_t mount_result = esp_vfs_fat_sdspi_mount(
        "/sdcard", &host, &device, &mount, &sd_card);
    if (mount_result != ESP_OK) {
        Serial.printf("microSD mount failed: %s\n",
                      esp_err_to_name(mount_result));
        return false;
    }
    sd_mounted = true;
    Serial.printf("microSD mounted on SPI3 at %d kHz with DMA\n",
                  player_settings::kSdClockKhz);
    sdmmc_card_print_info(stdout, sd_card);
    return true;
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
        size_t offset = 0;
        while (offset < received && !audio_stop_requested) {
            size_t loaded = 0;
            const esp_err_t result = dac_continuous_write(
                audio_dac, samples + offset, received - offset, &loaded,
                kAudioWriteTimeoutMs);
            offset += loaded;
            if (result == ESP_ERR_TIMEOUT) continue;
            if (result != ESP_OK) {
                Serial.printf("DAC write failed: %d, %u/%u bytes\n",
                              static_cast<int>(result),
                              static_cast<unsigned>(offset),
                              static_cast<unsigned>(received));
                audio_stop_requested = true;
                break;
            }
        }
    }
    audio_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void stopAudio() {
    if (audio_task_handle) {
        audio_stop_requested = true;
        xTaskNotifyGive(audio_task_handle);
        const uint32_t deadline = millis() + 500;
        while (audio_task_handle &&
               static_cast<int32_t>(deadline - millis()) > 0) {
            delay(1);
        }
        if (audio_task_handle) {
            Serial.println("DAC task did not stop; deleting it");
            vTaskDelete(audio_task_handle);
            audio_task_handle = nullptr;
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
    size_t offset = hlv1_packet_video_payload_size(&packet);
    while (offset < packet.payload_size) {
        const uint8_t *data = nullptr;
        size_t span = hlv1_packet_payload_span(&packet, offset, &data);
        if (!span || !data) return false;
        size_t sent_from_span = 0;
        while (sent_from_span < span) {
            size_t sent = xStreamBufferSend(
                audio_stream, data + sent_from_span, span - sent_from_span,
                pdMS_TO_TICKS(1000));
            if (!sent) return false;
            sent_from_span += sent;
        }
        offset += span;
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
    decoder.end();
    if (video_file) {
        fclose(video_file);
        video_file = nullptr;
    }
}

void reportHeap(const char *stage) {
    Serial.printf("%s: free heap=%u, largest block=%u, DMA free=%u, "
                  "DMA largest=%u\n",
                  stage,
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(
                      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(
                      heap_caps_get_free_size(MALLOC_CAP_DMA)),
                  static_cast<unsigned>(
                      heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
}

bool openVideo() {
    closeVideo();
    video_file = fopen(player_settings::kVideoPath, "rb");
    if (!video_file) {
        showStatus("video.hlv missing", "Copy it to the microSD card root");
        Serial.printf("Cannot open %s\n", player_settings::kVideoPath);
        return false;
    }
    if (setvbuf(video_file, nullptr, _IONBF, 0)) {
        showStatus("SD setup failed", "Cannot configure direct reads");
        Serial.println("Could not set unbuffered microSD reads");
        closeVideo();
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
    const int decoder_result = decoder.begin(sequence_header);
    if (decoder_result != HLV1_OK) {
        showStatus("Not enough RAM", "Use at most the 320x180 profile");
        reportHeap("decoder or packet pool allocation failed");
        closeVideo();
        return false;
    }
    Serial.printf("ESP32 packet pool: %u x %u bytes = %u bytes, "
                  "%u blocks direct-DMA\n",
                  static_cast<unsigned>(HlvEsp32Decoder::kPacketBlockCount),
                  static_cast<unsigned>(HlvEsp32Decoder::kPacketBlockBytes),
                  static_cast<unsigned>(decoder.packetCapacity()),
                  static_cast<unsigned>(decoder.dmaBlockCount()));

    frame_period_us = static_cast<uint32_t>(
        (1000000ULL * sequence_header.fps_den) / sequence_header.fps_num);
    if (!frame_period_us) frame_period_us = 1;
    next_present_us = micros();
    decoded_frames = 0;
    dropped_deadlines = 0;
    sd_read_us_total = 0;
    sd_read_us_max = 0;
    display.fillScreen(0x0000);

    if (player_settings::kScaleVideoToDisplay) {
        for (int x = 0; x < kScreenWidth; ++x) {
            scaled_x_map[x] = static_cast<uint16_t>(
                (x * sequence_header.width) / kScreenWidth);
        }
        for (int y = 0; y < kScreenHeight; ++y) {
            scaled_y_map[y] = static_cast<uint16_t>(
                (y * sequence_header.height) / kScreenHeight);
        }
    }

    Serial.printf("Playing %ux%u, %u/%u fps, stream v%u\n",
                  sequence_header.width, sequence_header.height,
                  sequence_header.fps_num, sequence_header.fps_den,
                  sequence_header.version);
    Serial.printf("Display mode: %s\n",
                  player_settings::kScaleVideoToDisplay
                      ? "scale to 320x240"
                      : "native size, centred");
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
    if (player_settings::kScaleVideoToDisplay) {
        int cached_source_y = -1;
        display.startWrite();
        for (int y0 = 0; y0 < kScreenHeight; y0 += kRowsPerTransfer) {
            const int rows = min(kRowsPerTransfer, kScreenHeight - y0);
            for (int row = 0; row < rows; ++row) {
                const int source_y = scaled_y_map[y0 + row];
                if (source_y != cached_source_y) {
                    convertScaledRow(frame, source_y, scaled_rgb_row);
                    cached_source_y = source_y;
                }
                memcpy(rgb_rows + row * kScreenWidth, scaled_rgb_row,
                       sizeof(uint16_t) * kScreenWidth);
            }
            display.pushImage(0, y0, kScreenWidth, rows, rgb_rows);
            yield();
        }
        display.endWrite();
        return;
    }

    const int x_offset = (kScreenWidth - frame->width) / 2;
    const int y_offset = (kScreenHeight - frame->height) / 2;

    display.startWrite();
    for (int y0 = 0; y0 < frame->height; y0 += kRowsPerTransfer) {
        const int rows = min(kRowsPerTransfer, frame->height - y0);
        for (int row = 0; row < rows; ++row) {
            const int y = y0 + row;
            uint16_t *output = rgb_rows + row * frame->width;
            convertNativeRow(frame, y, output);
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
    const uint32_t read_start = micros();
    const int packet_result = decoder.readPacket(video_file, &packet);
    const uint32_t read_us = micros() - read_start;
    sd_read_us_total += read_us;
    sd_read_us_max = max(sd_read_us_max, read_us);
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
    const int decode_result = decoder.decode(&packet, &frame);
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
        Serial.printf("frame=%u sd_avg=%uus sd_max=%uus decode=%uus "
                      "render=%uus late=%u heap=%u\n",
                      decoded_frames,
                      static_cast<unsigned>(sd_read_us_total / decoded_frames),
                      sd_read_us_max, decode_us, render_us, dropped_deadlines,
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
    showStatus("HLV-1 SD player", "Mounting microSD...");

    if (!mountSdCard()) {
        showStatus("microSD failed", "Insert a FAT32 card and reset");
        last_retry_ms = millis();
        return;
    }
    if (!openVideo()) last_retry_ms = millis();
}

void loop() {
    if (video_file && decoder.ready()) {
        playOneFrame();
        return;
    }

    if (millis() - last_retry_ms >= kRetryDelayMs) {
        last_retry_ms = millis();
        if (mountSdCard()) openVideo();
    }
    delay(20);
}
