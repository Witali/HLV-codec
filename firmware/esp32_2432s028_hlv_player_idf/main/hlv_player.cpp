#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "driver/dac_continuous.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "board_config.hpp"
#include "cyd_display.hpp"
#include "hlv1.h"
#include "hlv_esp32_decoder.hpp"
#include "player_settings.hpp"

namespace {

constexpr char kTag[] = "hlv-player";
constexpr int kScreenWidth = CydDisplay::kWidth;
constexpr int kScreenHeight = CydDisplay::kHeight;
constexpr int kRowsPerTransfer = CydDisplay::kRowsPerTransfer;
constexpr uint32_t kRetryDelayMs = 2000;
constexpr size_t kAudioStreamBytes = 4096;
constexpr size_t kAudioWriteBytes = 256;
constexpr uint32_t kAudioReceiveTimeoutMs = 100;
constexpr int kAudioWriteTimeoutMs = 100;

CydDisplay display;
FILE *video_file = nullptr;
HlvEsp32Decoder decoder;
HLV1Header sequence_header{};
int64_t frame_period_us = 0;
int64_t next_present_us = 0;
uint32_t decoded_frames = 0;
uint32_t dropped_deadlines = 0;
int64_t last_retry_ms = 0;
uint64_t sd_read_us_total = 0;
uint32_t sd_read_us_max = 0;
uint16_t scaled_rgb_row[kScreenWidth];
uint16_t scaled_x_map[kScreenWidth];
uint16_t scaled_y_map[kScreenHeight];
uint8_t native_y_row[kScreenWidth];
uint8_t native_u_row[kScreenWidth / 2];
uint8_t native_v_row[kScreenWidth / 2];
sdmmc_card_t *sd_card = nullptr;
bool sd_bus_initialized = false;
bool sd_mounted = false;
StreamBufferHandle_t audio_stream = nullptr;
TaskHandle_t audio_task_handle = nullptr;
dac_continuous_handle_t audio_dac = nullptr;
volatile bool audio_stop_requested = false;
bool audio_enabled = false;
bool audio_started = false;
volatile uint32_t audio_underruns = 0;

int64_t microsNow() { return esp_timer_get_time(); }

int64_t millisNow() { return microsNow() / 1000; }

int clamp8(int value) {
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

uint16_t yuvToRgb565(int y, int red_add, int green_add, int blue_add) {
    const int luma = 298 * (y > 16 ? y - 16 : 0);
    const int red = clamp8((luma + red_add) >> 8);
    const int green = clamp8((luma + green_add) >> 8);
    const int blue = clamp8((luma + blue_add) >> 8);
    return static_cast<uint16_t>(((red & 0xF8) << 8) |
                                 ((green & 0xFC) << 3) | (blue >> 3));
}

void convertNativeRow(const HLV1Frame *frame, int source_y,
                      uint16_t *output) {
    const int chroma_y = source_y >> 1;
    const uint8_t *y_row = frame->y + source_y * frame->stride_y;
    const uint8_t *u_row = frame->u + chroma_y * frame->stride_u;
    const uint8_t *v_row = frame->v + chroma_y * frame->stride_v;
    if (frame->storage_mode == HLV1_FRAME_STORAGE_Y6_U5_V5) {
        hlv1_frame_unpack_packed_samples(
            y_row, 0, 6, native_y_row, frame->width);
        hlv1_frame_unpack_packed_samples(
            u_row, 0, 5, native_u_row, (frame->width + 1) / 2);
        hlv1_frame_unpack_packed_samples(
            v_row, 0, 5, native_v_row, (frame->width + 1) / 2);
        y_row = native_y_row;
        u_row = native_u_row;
        v_row = native_v_row;
    }
    for (int x = 0; x < frame->width; x += 2) {
        const int chroma_x = x >> 1;
        const int u = static_cast<int>(u_row[chroma_x]) - 128;
        const int v = static_cast<int>(v_row[chroma_x]) - 128;
        const int red_add = 409 * v + 128;
        const int green_add = -100 * u - 208 * v + 128;
        const int blue_add = 516 * u + 128;
        output[x] = yuvToRgb565(y_row[x], red_add, green_add, blue_add);
        if (x + 1 < frame->width) {
            output[x + 1] = yuvToRgb565(
                y_row[x + 1], red_add, green_add, blue_add);
        }
    }
}

void convertScaledRow(const HLV1Frame *frame, int source_y,
                      uint16_t *output) {
    const int chroma_y = source_y >> 1;
    int previous_chroma_x = -1;
    int red_add = 0;
    int green_add = 0;
    int blue_add = 0;

    for (int output_x = 0; output_x < kScreenWidth; ++output_x) {
        const int source_x = scaled_x_map[output_x];
        const int chroma_x = source_x >> 1;
        if (chroma_x != previous_chroma_x) {
            const int u = static_cast<int>(
                              hlv1_frame_u_sample(frame, chroma_x,
                                                 chroma_y)) -
                          128;
            const int v = static_cast<int>(
                              hlv1_frame_v_sample(frame, chroma_x,
                                                 chroma_y)) -
                          128;
            red_add = 409 * v + 128;
            green_add = -100 * u - 208 * v + 128;
            blue_add = 516 * u + 128;
            previous_chroma_x = chroma_x;
        }
        output[output_x] = yuvToRgb565(
            hlv1_frame_y_sample(frame, source_x, source_y),
            red_add, green_add, blue_add);
    }
}

void showStatus(const char *title, const char *detail = nullptr) {
    if (detail) {
        ESP_LOGW(kTag, "%s: %s", title, detail);
    } else {
        ESP_LOGI(kTag, "%s", title);
    }
    const esp_err_t clear_result = display.clear(0x0000);
    if (clear_result != ESP_OK) {
        ESP_LOGE(kTag, "Could not clear status screen: %s",
                 esp_err_to_name(clear_result));
    }
}

bool mountSdCard() {
    if (sd_mounted) return true;

    if (!sd_bus_initialized) {
        spi_bus_config_t bus{};
        bus.mosi_io_num = board::kSdMosi;
        bus.miso_io_num = board::kSdMiso;
        bus.sclk_io_num = board::kSdSck;
        bus.quadwp_io_num = GPIO_NUM_NC;
        bus.quadhd_io_num = GPIO_NUM_NC;
        bus.data4_io_num = GPIO_NUM_NC;
        bus.data5_io_num = GPIO_NUM_NC;
        bus.data6_io_num = GPIO_NUM_NC;
        bus.data7_io_num = GPIO_NUM_NC;
        bus.max_transfer_sz = HlvEsp32Decoder::kPacketBlockBytes;
        const esp_err_t bus_result =
            spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
        if (bus_result != ESP_OK) {
            ESP_LOGE(kTag, "SD SPI3 DMA init failed: %s",
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
    device.gpio_cs = board::kSdCs;

    esp_vfs_fat_mount_config_t mount{};
    mount.format_if_mount_failed = false;
    mount.max_files = 1;
    mount.allocation_unit_size = 16 * 1024;
    mount.disk_status_check_enable = false;
    mount.use_one_fat = false;

    const esp_err_t mount_result = esp_vfs_fat_sdspi_mount(
        "/sdcard", &host, &device, &mount, &sd_card);
    if (mount_result != ESP_OK) {
        ESP_LOGE(kTag, "microSD mount failed: %s",
                 esp_err_to_name(mount_result));
        return false;
    }
    sd_mounted = true;
    ESP_LOGI(kTag, "microSD: SPI3 at %d kHz with DMA",
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
            std::memset(samples, 128, sizeof samples);
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
                ESP_LOGE(kTag, "DAC write failed: %s, %u/%u bytes",
                         esp_err_to_name(result),
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
        const int64_t deadline = millisNow() + 500;
        while (audio_task_handle && millisNow() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (audio_task_handle) {
            ESP_LOGW(kTag, "DAC task did not stop; deleting it");
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
    audio_stop_requested = false;
    audio_enabled = false;
    audio_started = false;
    audio_underruns = 0;
}

bool prepareAudio(const HLV1Header &header) {
    stopAudio();
    if (!(header.flags & HLV1_FLAG_AUDIO)) return true;
    if (!player_settings::kEnableAudio) {
        ESP_LOGW(kTag, "Audio output disabled in player settings");
        return true;
    }

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
    ESP_LOGI(kTag, "Audio: PCM_U8 mono %u Hz on DAC GPIO%d",
             header.audio_sample_rate, board::kAudioDac);
    return true;
}

bool queueAudio(const HLV1Packet &packet) {
    if (!audio_enabled) return true;
    size_t offset = hlv1_packet_video_payload_size(&packet);
    while (offset < packet.payload_size) {
        const uint8_t *data = nullptr;
        const size_t span =
            hlv1_packet_payload_span(&packet, offset, &data);
        if (!span || !data) return false;
        size_t sent_from_span = 0;
        while (sent_from_span < span) {
            const size_t sent = xStreamBufferSend(
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
    std::memset(silence, 128, sizeof silence);
    const size_t sent = xStreamBufferSend(
        audio_stream, silence, sizeof silence, pdMS_TO_TICKS(1000));
    if (sent != sizeof silence) {
        ESP_LOGW(kTag, "Could not flush the final audio samples");
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
        std::fclose(video_file);
        video_file = nullptr;
    }
}

void reportHeap(const char *stage) {
    ESP_LOGI(kTag,
             "%s: heap=%u largest=%u, DMA=%u largest-DMA=%u",
             stage,
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
}

bool openVideo() {
    closeVideo();
    video_file = std::fopen(player_settings::kVideoPath, "rb");
    if (!video_file) {
        showStatus("video.hlv missing",
                   "copy it to the microSD card root");
        return false;
    }
    if (std::setvbuf(video_file, nullptr, _IONBF, 0)) {
        showStatus("SD setup failed", "cannot configure direct reads");
        closeVideo();
        return false;
    }

    const int header_result = hlv1_header_read(video_file, &sequence_header);
    if (header_result != HLV1_OK) {
        showStatus("Invalid video.hlv", hlv1_strerror(header_result));
        closeVideo();
        return false;
    }
    if (sequence_header.width > kScreenWidth ||
        sequence_header.height > kScreenHeight) {
        ESP_LOGE(kTag, "Unsupported dimensions: %ux%u",
                 sequence_header.width, sequence_header.height);
        showStatus("Video is too large", "maximum size is 320x240");
        closeVideo();
        return false;
    }
    ESP_LOGI(kTag, "HLV: %ux%u, %u/%u fps, %u frames, audio=%u Hz",
             sequence_header.width, sequence_header.height,
             sequence_header.fps_num, sequence_header.fps_den,
             static_cast<unsigned>(sequence_header.frame_count),
             sequence_header.audio_sample_rate);
    if (!prepareAudio(sequence_header)) {
        showStatus("Audio init failed", "DAC GPIO26 could not start");
        closeVideo();
        return false;
    }

    reportHeap("before decoder");
    const int decoder_result = decoder.begin(
        sequence_header, player_settings::kUseCompactY6U5V5);
    if (decoder_result != HLV1_OK) {
        showStatus("Not enough RAM", "use at most the 320x180 profile");
        reportHeap("decoder or packet-pool allocation failed");
        closeVideo();
        return false;
    }
    ESP_LOGI(kTag, "Packet pool: %u x %u = %u bytes, %u direct-DMA",
             static_cast<unsigned>(HlvEsp32Decoder::kPacketBlockCount),
             static_cast<unsigned>(HlvEsp32Decoder::kPacketBlockBytes),
             static_cast<unsigned>(decoder.packetCapacity()),
             static_cast<unsigned>(decoder.dmaBlockCount()));

    frame_period_us = static_cast<int64_t>(
        (1000000ULL * sequence_header.fps_den) / sequence_header.fps_num);
    if (!frame_period_us) frame_period_us = 1;
    next_present_us = microsNow();
    decoded_frames = 0;
    dropped_deadlines = 0;
    sd_read_us_total = 0;
    sd_read_us_max = 0;
    ESP_ERROR_CHECK(display.clear(0x0000));

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

    ESP_LOGI(kTag, "Playing v%u in %s mode, frame storage=%s",
             sequence_header.version,
             player_settings::kScaleVideoToDisplay
                 ? "scale-to-320x240"
                 : "native-centred",
             decoder.compactYuv() ? "packed Y6/U5/V5 4:2:0"
                                  : "8-bit YUV 4:2:0");
    reportHeap("decoder ready");
    return true;
}

void waitUntil(int64_t deadline) {
    for (;;) {
        const int64_t remaining = deadline - microsNow();
        if (remaining <= 0) return;
        if (remaining > 2000) {
            vTaskDelay(pdMS_TO_TICKS(1));
        } else {
            esp_rom_delay_us(static_cast<uint32_t>(remaining));
        }
    }
}

bool renderFrame(const HLV1Frame *frame) {
    if (player_settings::kScaleVideoToDisplay) {
        int cached_source_y = -1;
        for (int y0 = 0; y0 < kScreenHeight; y0 += kRowsPerTransfer) {
            const int rows =
                std::min(kRowsPerTransfer, kScreenHeight - y0);
            uint16_t *pixels = display.acquireBuffer();
            if (!pixels) return false;
            for (int row = 0; row < rows; ++row) {
                const int source_y = scaled_y_map[y0 + row];
                if (source_y != cached_source_y) {
                    convertScaledRow(frame, source_y, scaled_rgb_row);
                    cached_source_y = source_y;
                }
                std::memcpy(pixels + row * kScreenWidth, scaled_rgb_row,
                            sizeof(uint16_t) * kScreenWidth);
            }
            if (display.drawBitmap(0, y0, kScreenWidth, rows, pixels) !=
                ESP_OK) {
                return false;
            }
        }
        return true;
    }

    const int x_offset = (kScreenWidth - frame->width) / 2;
    const int y_offset = (kScreenHeight - frame->height) / 2;
    for (int y0 = 0; y0 < frame->height; y0 += kRowsPerTransfer) {
        const int rows = std::min(kRowsPerTransfer, frame->height - y0);
        uint16_t *pixels = display.acquireBuffer();
        if (!pixels) return false;
        for (int row = 0; row < rows; ++row) {
            convertNativeRow(frame, y0 + row,
                             pixels + row * frame->width);
        }
        if (display.drawBitmap(x_offset, y_offset + y0, frame->width, rows,
                               pixels) != ESP_OK) {
            return false;
        }
    }
    return true;
}

void failPlayback(const char *title, int result) {
    ESP_LOGE(kTag, "%s: %s", title, hlv1_strerror(result));
    showStatus(title, hlv1_strerror(result));
    closeVideo();
    last_retry_ms = millisNow();
}

void playOneFrame() {
    HLV1Packet packet{};
    const int64_t read_start = microsNow();
    const int packet_result = decoder.readPacket(video_file, &packet);
    const uint32_t read_us =
        static_cast<uint32_t>(microsNow() - read_start);
    sd_read_us_total += read_us;
    sd_read_us_max = std::max(sd_read_us_max, read_us);
    if (packet_result == HLV1_EOF) {
        finishAudio();
        waitUntil(next_present_us);
        ESP_LOGI(kTag, "Loop: %u frames, %u late, %u audio underruns",
                 decoded_frames, dropped_deadlines,
                 static_cast<unsigned>(audio_underruns));
        if (!openVideo()) last_retry_ms = millisNow();
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
    const int64_t decode_start = microsNow();
    const int decode_result = decoder.decode(&packet, &frame);
    const uint32_t decode_us =
        static_cast<uint32_t>(microsNow() - decode_start);
    hlv1_packet_free(&packet);
    if (decode_result != HLV1_OK) {
        failPlayback("Decode error", decode_result);
        return;
    }

    waitUntil(next_present_us);
    if (!decoded_frames) startAudio();
    const int64_t render_start = microsNow();
    if (!renderFrame(frame)) {
        failPlayback("Display DMA error", HLV1_ERR_IO);
        return;
    }
    const uint32_t render_us =
        static_cast<uint32_t>(microsNow() - render_start);
    ++decoded_frames;

    next_present_us += frame_period_us;
    const int64_t lateness = microsNow() - next_present_us;
    if (lateness > frame_period_us) {
        ++dropped_deadlines;
        next_present_us = microsNow();
    }

    if ((decoded_frames % 60U) == 0U) {
        ESP_LOGI(kTag,
                 "frame=%u sd_avg=%uus sd_max=%uus decode=%uus "
                 "render_queue=%uus late=%u heap=%u",
                 decoded_frames,
                 static_cast<unsigned>(sd_read_us_total / decoded_frames),
                 sd_read_us_max, decode_us, render_us, dropped_deadlines,
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    }
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "HLV-1 ESP-IDF SD player starting");
    const esp_err_t display_result = display.init();
    if (display_result != ESP_OK) {
        ESP_LOGE(kTag, "Display initialization failed: %s",
                 esp_err_to_name(display_result));
        return;
    }
    showStatus("HLV-1 SD player", "mounting microSD");

    if (!mountSdCard()) {
        showStatus("microSD failed", "insert a FAT32 card and reset");
        last_retry_ms = millisNow();
    } else if (!openVideo()) {
        last_retry_ms = millisNow();
    }

    for (;;) {
        if (video_file && decoder.ready()) {
            playOneFrame();
            continue;
        }
        if (millisNow() - last_retry_ms >= kRetryDelayMs) {
            last_retry_ms = millisNow();
            if (mountSdCard()) openVideo();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
