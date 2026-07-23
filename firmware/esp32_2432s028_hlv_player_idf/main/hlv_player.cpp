#include <algorithm>
#include <climits>
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
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sdkconfig.h"
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
constexpr size_t kVideoReadAheadBytes = 16 * 1024;
constexpr size_t kAudioStreamBytes = 4096;
// A FreeRTOS static stream buffer reserves one byte to distinguish full from
// empty, so the backing array is one byte larger than its useful capacity.
constexpr size_t kAudioStreamStorageBytes = kAudioStreamBytes + 1;
constexpr size_t kAudioDmaSamples = 256;
constexpr size_t kAudioDmaBufferBytes = kAudioDmaSamples * 2;
constexpr size_t kAudioDmaDescriptors = 6;
constexpr size_t kAudioReadAheadBytes = 512;
constexpr size_t kAudioReadChunkBytes = 512;
constexpr size_t kAudioPrerollBytes = 3072;
constexpr uint32_t kAudioReaderStackBytes = 3072;
constexpr uint32_t kAudioReaderStopTimeoutMs = 500;
constexpr uint32_t kAudioPrerollTimeoutMs = 3000;
constexpr uint32_t kAudioClockWaitTimeoutMs = 3000;
constexpr uint32_t kDecodeWorkerStackBytes = 4096;

static_assert(CONFIG_FREERTOS_NUMBER_OF_CORES >= 2 ||
                  !player_settings::kUseDualCorePipeline,
              "Dual-core playback requires a two-core FreeRTOS build");
static_assert(CONFIG_DAC_DMA_AUTO_16BIT_ALIGN,
              "The DAC ring expects ESP-IDF 8-to-16-bit DMA expansion");

struct DecodeRequest {
    const HLV1Packet *packet;
};

struct DecodeResult {
    int result;
    const HLV1Frame *frame;
    uint32_t decode_us;
};

CydDisplay display;
FILE *video_file = nullptr;
FILE *audio_file = nullptr;
HlvEsp32Decoder decoder;
HLV1Header sequence_header{};
int64_t frame_period_us = 0;
int64_t next_present_us = 0;
uint32_t decoded_frames = 0;
uint32_t dropped_deadlines = 0;
int64_t last_retry_ms = 0;
uint64_t sd_read_us_total = 0;
uint32_t sd_read_us_max = 0;
uint32_t sd_packets_read = 0;
uint16_t scaled_rgb_row[kScreenWidth];
uint16_t scaled_x_map[kScreenWidth];
uint16_t scaled_y_map[kScreenHeight];
uint8_t native_y_row[kScreenWidth];
uint8_t native_u_row[kScreenWidth / 2];
uint8_t native_v_row[kScreenWidth / 2];
alignas(4) uint8_t video_read_ahead[kVideoReadAheadBytes];
alignas(4) uint8_t audio_read_ahead[kAudioReadAheadBytes];
alignas(4) uint8_t audio_read_chunk[kAudioReadChunkBytes];
sdmmc_card_t *sd_card = nullptr;
bool sd_bus_initialized = false;
bool sd_mounted = false;
StreamBufferHandle_t audio_stream = nullptr;
StaticStreamBuffer_t audio_stream_state{};
alignas(4) uint8_t audio_stream_storage[kAudioStreamStorageBytes];
alignas(4) uint8_t audio_dma_samples[kAudioDmaSamples];
dac_continuous_handle_t audio_dac = nullptr;
TaskHandle_t audio_reader_task_handle = nullptr;
void *audio_dma_buffer_keys[kAudioDmaDescriptors]{};
uint16_t audio_dma_valid_samples[kAudioDmaDescriptors]{};
bool audio_enabled = false;
volatile bool audio_started = false;
bool audio_async_started = false;
volatile bool audio_reader_stop_requested = false;
volatile bool audio_prefetch_eof = false;
volatile bool audio_rebuffering = false;
volatile bool audio_output_failed = false;
volatile int audio_reader_result = HLV1_OK;
volatile uint32_t audio_played_samples = 0;
volatile uint32_t audio_pending_samples = 0;
volatile uint32_t audio_rebuffers = 0;
volatile uint32_t audio_silence_chunks = 0;
volatile bool audio_loop_hold = false;
volatile uint32_t audio_loop_events = 0;
volatile uint32_t audio_loop_chunks = 0;
QueueHandle_t decode_request_queue = nullptr;
QueueHandle_t decode_result_queue = nullptr;
TaskHandle_t decode_task_handle = nullptr;
bool decode_in_flight = false;
HLV1Frame pending_frame{};
bool pending_frame_valid = false;
uint32_t pending_decode_us = 0;
uint32_t skipped_presentations = 0;

int64_t microsNow() { return esp_timer_get_time(); }

int64_t millisNow() { return microsNow() / 1000; }

void decodeTask(void *) {
    DecodeRequest request{};
    for (;;) {
        if (xQueueReceive(decode_request_queue, &request,
                          portMAX_DELAY) != pdTRUE) {
            continue;
        }
        DecodeResult result{};
        const int64_t start = microsNow();
        result.result = decoder.decode(request.packet, &result.frame);
        result.decode_us = static_cast<uint32_t>(microsNow() - start);
        xQueueSend(decode_result_queue, &result, portMAX_DELAY);
    }
}

bool startDecodeWorker() {
    if (!player_settings::kUseDualCorePipeline) {
        ESP_LOGI(kTag, "Playback pipeline: single-core sequential mode");
        return true;
    }
    if (decode_task_handle) return true;
    decode_request_queue = xQueueCreate(1, sizeof(DecodeRequest));
    decode_result_queue = xQueueCreate(1, sizeof(DecodeResult));
    if (!decode_request_queue || !decode_result_queue) {
        if (decode_request_queue) vQueueDelete(decode_request_queue);
        if (decode_result_queue) vQueueDelete(decode_result_queue);
        decode_request_queue = nullptr;
        decode_result_queue = nullptr;
        return false;
    }
    if (xTaskCreatePinnedToCore(decodeTask, "hlv-decode",
                                kDecodeWorkerStackBytes, nullptr, 2,
                                &decode_task_handle, 1) != pdPASS) {
        vQueueDelete(decode_request_queue);
        vQueueDelete(decode_result_queue);
        decode_request_queue = nullptr;
        decode_result_queue = nullptr;
        return false;
    }
    ESP_LOGI(kTag,
             "Playback pipeline: CPU0 SD/render, CPU1 ordered HLV decode");
    return true;
}

bool submitDecode(const HLV1Packet *packet) {
    if (!decode_task_handle || decode_in_flight || !packet) return false;
    DecodeRequest request{packet};
    if (xQueueSend(decode_request_queue, &request, 0) != pdTRUE) return false;
    decode_in_flight = true;
    return true;
}

bool waitDecode(DecodeResult *result) {
    if (!decode_in_flight || !result) return false;
    if (xQueueReceive(decode_result_queue, result, portMAX_DELAY) != pdTRUE)
        return false;
    decode_in_flight = false;
    return true;
}

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
    mount.max_files = 2;
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

uint32_t readLe32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

int prefetchAudioPacket() {
    uint8_t header[HLV1_FRAME_HEADER_SIZE];
    const size_t header_bytes = std::fread(header, 1, sizeof header, audio_file);
    if (!header_bytes && std::feof(audio_file)) return HLV1_EOF;
    if (header_bytes != sizeof header) return HLV1_ERR_IO;
    if (std::memcmp(header, "FRM1", 4)) return HLV1_ERR_FORMAT;

    const uint8_t frame_type = header[4];
    const uint8_t q_y = header[5];
    const uint8_t q_uv = header[6];
    const uint8_t q_shift = header[7];
    const uint32_t bit_length = readLe32(header + 8);
    const uint32_t payload_size = readLe32(header + 12);
    if (frame_type > HLV1_FRAME_P || !q_y || !q_uv || q_shift > 3 ||
        bit_length > static_cast<uint64_t>(payload_size) * 8U) {
        return HLV1_ERR_FORMAT;
    }

    const uint32_t video_bytes = static_cast<uint32_t>(
        (static_cast<uint64_t>(bit_length) + 7U) / 8U);
    if (video_bytes > payload_size || video_bytes > LONG_MAX ||
        std::fseek(audio_file, static_cast<long>(video_bytes), SEEK_CUR)) {
        return HLV1_ERR_IO;
    }

    size_t remaining = payload_size - video_bytes;
    while (remaining && !audio_reader_stop_requested) {
        const size_t chunk = std::min(remaining, sizeof audio_read_chunk);
        if (std::fread(audio_read_chunk, 1, chunk, audio_file) != chunk) {
            return HLV1_ERR_IO;
        }
        size_t sent = 0;
        while (sent < chunk && !audio_reader_stop_requested) {
            sent += xStreamBufferSend(
                audio_stream, audio_read_chunk + sent, chunk - sent,
                pdMS_TO_TICKS(20));
            if (audio_rebuffering &&
                xStreamBufferBytesAvailable(audio_stream) >=
                    kAudioPrerollBytes) {
                audio_rebuffering = false;
            }
        }
        remaining -= chunk;
    }
    return HLV1_OK;
}

void audioReaderTask(void *) {
    int result = HLV1_OK;
    while (!audio_reader_stop_requested) {
        result = prefetchAudioPacket();
        if (result != HLV1_OK) break;
    }
    audio_reader_result = result;
    audio_prefetch_eof = result == HLV1_EOF;
    audio_reader_task_handle = nullptr;
    vTaskDelete(nullptr);
}

bool onAudioConvertDone(dac_continuous_handle_t handle,
                        const dac_event_data_t *event, void *) {
    size_t dma_slot = kAudioDmaDescriptors;
    for (size_t slot = 0; slot < kAudioDmaDescriptors; ++slot) {
        if (audio_dma_buffer_keys[slot] == event->buf) {
            dma_slot = slot;
            break;
        }
        if (!audio_dma_buffer_keys[slot] &&
            dma_slot == kAudioDmaDescriptors) {
            dma_slot = slot;
        }
    }
    if (dma_slot < kAudioDmaDescriptors &&
        !audio_dma_buffer_keys[dma_slot]) {
        audio_dma_buffer_keys[dma_slot] = event->buf;
    }

    if (dma_slot < kAudioDmaDescriptors) {
        const uint32_t completed = audio_dma_valid_samples[dma_slot];
        audio_played_samples = audio_played_samples + completed;
        audio_pending_samples =
            completed <= audio_pending_samples
                ? audio_pending_samples - completed
                : 0;
        audio_dma_valid_samples[dma_slot] = 0;
    }

    BaseType_t task_woken = pdFALSE;
    size_t received = 0;
    const bool repeat_dma_ring = audio_started && audio_loop_hold;
    if (repeat_dma_ring) {
        // AUTO_16BIT_ALIGN stores each unsigned 8-bit DAC sample in the high
        // byte of a 16-bit DMA word. Restore the just-played descriptor into
        // the driver's 8-bit input buffer and arm that same descriptor again.
        // No stream bytes are consumed and repeated samples do not advance the
        // media clock.
        const auto *completed_dma =
            static_cast<const uint8_t *>(event->buf);
        for (size_t sample = 0; sample < kAudioDmaSamples; ++sample) {
            audio_dma_samples[sample] = completed_dma[sample * 2U + 1U];
        }
        audio_loop_chunks = audio_loop_chunks + 1;
    } else if (audio_started && !audio_rebuffering && audio_stream) {
        received = xStreamBufferReceiveFromISR(
            audio_stream, audio_dma_samples, sizeof audio_dma_samples,
            &task_woken);
        if (!received && !audio_prefetch_eof &&
            audio_reader_result >= HLV1_OK) {
            audio_rebuffering = true;
            audio_rebuffers = audio_rebuffers + 1;
        }
    }
    if (!repeat_dma_ring && received < sizeof audio_dma_samples) {
        std::memset(audio_dma_samples + received, 128,
                    sizeof audio_dma_samples - received);
        if (audio_started) {
            audio_silence_chunks = audio_silence_chunks + 1;
        }
    }

    size_t loaded = 0;
    const esp_err_t result = dac_continuous_write_asynchronously(
        handle, static_cast<uint8_t *>(event->buf), event->buf_size,
        audio_dma_samples, sizeof audio_dma_samples, &loaded);
    if (result != ESP_OK || loaded != sizeof audio_dma_samples) {
        audio_output_failed = true;
    }
    if (dma_slot < kAudioDmaDescriptors) {
        audio_dma_valid_samples[dma_slot] =
            repeat_dma_ring ? 0 : static_cast<uint16_t>(received);
        audio_pending_samples =
            audio_pending_samples + static_cast<uint32_t>(received);
    }
    return task_woken == pdTRUE;
}

void stopAudio() {
    audio_started = false;
    if (audio_reader_task_handle) {
        audio_reader_stop_requested = true;
        const int64_t deadline =
            millisNow() + kAudioReaderStopTimeoutMs;
        while (audio_reader_task_handle && millisNow() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (audio_reader_task_handle) {
            ESP_LOGW(kTag, "Audio reader did not stop; deleting it");
            vTaskDelete(audio_reader_task_handle);
            audio_reader_task_handle = nullptr;
        }
    }
    if (audio_dac) {
        if (audio_async_started) {
            dac_continuous_stop_async_writing(audio_dac);
            audio_async_started = false;
        }
        dac_continuous_disable(audio_dac);
        dac_continuous_del_channels(audio_dac);
        audio_dac = nullptr;
    }
    if (audio_file) {
        std::fclose(audio_file);
        audio_file = nullptr;
    }
    if (audio_stream) {
        vStreamBufferDelete(audio_stream);
        audio_stream = nullptr;
    }
    audio_enabled = false;
    audio_reader_stop_requested = false;
    audio_prefetch_eof = false;
    audio_rebuffering = false;
    audio_output_failed = false;
    audio_reader_result = HLV1_OK;
    audio_played_samples = 0;
    audio_pending_samples = 0;
    audio_rebuffers = 0;
    audio_silence_chunks = 0;
    audio_loop_hold = false;
    audio_loop_events = 0;
    audio_loop_chunks = 0;
    std::memset(audio_dma_buffer_keys, 0, sizeof audio_dma_buffer_keys);
    std::memset(audio_dma_valid_samples, 0,
                sizeof audio_dma_valid_samples);
}

bool prepareAudio(const HLV1Header &header) {
    stopAudio();
    if (!(header.flags & HLV1_FLAG_AUDIO)) {
        ESP_LOGI(kTag, "Audio clock unavailable: video has no audio track");
        return true;
    }
    if (!player_settings::kEnableAudio) {
        ESP_LOGI(kTag,
                 "Audio output disabled; using the ESP timer video clock");
        return true;
    }

    audio_stream = xStreamBufferCreateStatic(
        sizeof audio_stream_storage, kAudioDmaSamples,
        audio_stream_storage, &audio_stream_state);
    if (!audio_stream) return false;

    audio_file = std::fopen(player_settings::kVideoPath, "rb");
    if (!audio_file ||
        std::setvbuf(audio_file,
                     reinterpret_cast<char *>(audio_read_ahead),
                     _IOFBF, sizeof audio_read_ahead)) {
        stopAudio();
        return false;
    }
    HLV1Header audio_header{};
    if (hlv1_header_read(audio_file, &audio_header) != HLV1_OK ||
        audio_header.width != header.width ||
        audio_header.height != header.height ||
        audio_header.fps_num != header.fps_num ||
        audio_header.fps_den != header.fps_den ||
        audio_header.frame_count != header.frame_count ||
        audio_header.audio_sample_rate != header.audio_sample_rate ||
        audio_header.audio_codec != HLV1_AUDIO_PCM_U8 ||
        audio_header.audio_channels != 1) {
        stopAudio();
        return false;
    }

    dac_continuous_config_t config{};
    config.chan_mask = DAC_CHANNEL_MASK_CH1;
    config.desc_num = kAudioDmaDescriptors;
    config.buf_size = kAudioDmaBufferBytes;
    config.freq_hz = header.audio_sample_rate;
    config.offset = 0;
    config.clk_src = DAC_DIGI_CLK_SRC_APLL;
    config.chan_mode = DAC_CHANNEL_MODE_SIMUL;
    if (dac_continuous_new_channels(&config, &audio_dac) != ESP_OK ||
        dac_continuous_enable(audio_dac) != ESP_OK) {
        stopAudio();
        return false;
    }

    dac_event_callbacks_t callbacks{};
    callbacks.on_convert_done = onAudioConvertDone;
    if (dac_continuous_register_event_callback(
            audio_dac, &callbacks, nullptr) != ESP_OK ||
        dac_continuous_start_async_writing(audio_dac) != ESP_OK) {
        stopAudio();
        return false;
    }
    audio_async_started = true;
    audio_enabled = true;

    audio_reader_stop_requested = false;
    audio_prefetch_eof = false;
    audio_reader_result = HLV1_OK;
    if (xTaskCreatePinnedToCore(
            audioReaderTask, "hlv-audio-read", kAudioReaderStackBytes,
            nullptr, 3, &audio_reader_task_handle, 0) != pdPASS) {
        stopAudio();
        return false;
    }

    const int64_t preroll_deadline =
        millisNow() + kAudioPrerollTimeoutMs;
    while (xStreamBufferBytesAvailable(audio_stream) <
               kAudioPrerollBytes &&
           !audio_prefetch_eof && audio_reader_result == HLV1_OK &&
           millisNow() < preroll_deadline) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    const size_t prefetched =
        xStreamBufferBytesAvailable(audio_stream);
    if (audio_reader_result < HLV1_OK || !prefetched ||
        (!audio_prefetch_eof && prefetched < kAudioPrerollBytes)) {
        stopAudio();
        return false;
    }

    ESP_LOGI(kTag,
             "Audio: PCM_U8 mono %u Hz on DAC GPIO%d, static %u-byte queue, "
             "%u x %u-sample DMA ring, %u-byte preroll",
             header.audio_sample_rate, board::kAudioDac,
             static_cast<unsigned>(kAudioStreamBytes),
             static_cast<unsigned>(kAudioDmaDescriptors),
             static_cast<unsigned>(kAudioDmaSamples),
             static_cast<unsigned>(prefetched));
    return true;
}

void startAudio() {
    if (!audio_enabled || audio_started) return;
    audio_rebuffering = false;
    audio_started = true;
}

void closeVideo() {
    if (decode_in_flight) {
        DecodeResult ignored{};
        waitDecode(&ignored);
    }
    pending_frame_valid = false;
    pending_decode_us = 0;
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
    if (std::setvbuf(video_file,
                     reinterpret_cast<char *>(video_read_ahead),
                     _IOFBF, sizeof video_read_ahead)) {
        showStatus("SD setup failed", "cannot configure read-ahead");
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
    reportHeap("before decoder");
    const int decoder_result = decoder.begin(
        sequence_header, player_settings::kUseCompactY6U5V5);
    if (decoder_result != HLV1_OK) {
        showStatus("Not enough RAM", "use at most the 320x180 profile");
        reportHeap("decoder or packet-pool allocation failed");
        closeVideo();
        return false;
    }
    ESP_LOGI(kTag, "Packet pool: %u x %u = %u bytes, %u DMA-capable",
             static_cast<unsigned>(HlvEsp32Decoder::kPacketBlockCount),
             static_cast<unsigned>(HlvEsp32Decoder::kPacketBlockBytes),
             static_cast<unsigned>(decoder.packetCapacity()),
             static_cast<unsigned>(decoder.dmaBlockCount()));
    // Allocate the large predictive planes and packet blocks before the
    // worker stack, preserving the largest possible contiguous heap regions.
    if (!startDecodeWorker()) {
        showStatus("Dual-core init failed",
                   "cannot create CPU1 decoder task");
        closeVideo();
        return false;
    }
    // Allocate the predictive frames, packet pool and decoder task before the
    // smaller DAC descriptors and audio task stack. This keeps the large
    // internal-RAM allocations immune to audio heap fragmentation.
    if (!prepareAudio(sequence_header)) {
        ESP_LOGW(kTag,
                 "Audio initialization failed; continuing with timer clock");
        stopAudio();
    }

    frame_period_us = static_cast<int64_t>(
        (1000000ULL * sequence_header.fps_den) / sequence_header.fps_num);
    if (!frame_period_us) frame_period_us = 1;
    next_present_us = microsNow();
    decoded_frames = 0;
    dropped_deadlines = 0;
    skipped_presentations = 0;
    sd_read_us_total = 0;
    sd_read_us_max = 0;
    sd_packets_read = 0;
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

void fallBackToTimerClock(const char *reason) {
    ESP_LOGW(kTag, "%s; switching to the ESP timer video clock", reason);
    stopAudio();
    next_present_us = microsNow();
}

uint64_t frameAudioTarget(uint32_t frame_index) {
    return (static_cast<uint64_t>(frame_index) *
            sequence_header.audio_sample_rate *
            sequence_header.fps_den) /
           sequence_header.fps_num;
}

bool waitForAudioTarget(uint64_t target_samples) {
    const int64_t deadline = millisNow() + kAudioClockWaitTimeoutMs;
    while (audio_enabled) {
        if (audio_output_failed || audio_reader_result < HLV1_OK) {
            return false;
        }
        const uint64_t estimated_position =
            static_cast<uint64_t>(audio_played_samples) +
            kAudioDmaSamples;
        if (estimated_position >= target_samples) return true;
        if (audio_prefetch_eof &&
            !xStreamBufferBytesAvailable(audio_stream) &&
            !audio_pending_samples) {
            return false;
        }
        if (millisNow() >= deadline) return false;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

bool presentFrame(const HLV1Frame *frame, uint32_t decode_us) {
    bool render = true;
    if (audio_enabled) {
        if (!audio_started) {
            startAudio();
            const uint64_t lead_us =
                (static_cast<uint64_t>(kAudioDmaDescriptors - 1) *
                 kAudioDmaSamples * 1000000ULL) /
                sequence_header.audio_sample_rate;
            waitUntil(microsNow() + static_cast<int64_t>(lead_us));
        }

        const uint64_t target_samples = frameAudioTarget(decoded_frames);
        const uint64_t frame_samples =
            (static_cast<uint64_t>(sequence_header.audio_sample_rate) *
                 sequence_header.fps_den +
             sequence_header.fps_num - 1U) /
            sequence_header.fps_num;
        if (player_settings::kAvSyncMode ==
            player_settings::AvSyncMode::kLoopAudioForLateVideo) {
            if (audio_output_failed || audio_reader_result < HLV1_OK) {
                fallBackToTimerClock("Audio clock stopped");
            } else {
                const uint64_t estimated_position =
                    static_cast<uint64_t>(audio_played_samples) +
                    kAudioDmaSamples;
                if (audio_loop_hold) {
                    if (estimated_position <=
                        target_samples + frame_samples) {
                        audio_loop_hold = false;
                    }
                } else if (estimated_position >
                           target_samples + frame_samples) {
                    audio_loop_hold = true;
                    audio_loop_events = audio_loop_events + 1;
                }

                if (!audio_loop_hold &&
                    !waitForAudioTarget(target_samples)) {
                    fallBackToTimerClock("Audio clock stopped");
                }
            }
        } else if (!waitForAudioTarget(target_samples)) {
            fallBackToTimerClock("Audio clock stopped");
        } else {
            const uint64_t estimated_position =
                static_cast<uint64_t>(audio_played_samples) +
                kAudioDmaSamples;
            if (estimated_position > target_samples + frame_samples) {
                render = false;
                ++skipped_presentations;
            }
        }
    }

    if (!audio_enabled) waitUntil(next_present_us);
    uint32_t render_us = 0;
    if (render) {
        const int64_t render_start = microsNow();
        if (!renderFrame(frame)) return false;
        render_us = static_cast<uint32_t>(microsNow() - render_start);
    }
    ++decoded_frames;

    if (!audio_enabled) {
        next_present_us += frame_period_us;
        const int64_t lateness = microsNow() - next_present_us;
        if (lateness > frame_period_us) {
            ++dropped_deadlines;
            next_present_us = microsNow();
        }
    }

    if ((decoded_frames % 60U) == 0U) {
        const uint32_t sd_average = sd_packets_read
                                        ? static_cast<uint32_t>(
                                              sd_read_us_total /
                                              sd_packets_read)
                                        : 0;
        const size_t audio_queued =
            audio_stream ? xStreamBufferBytesAvailable(audio_stream) : 0;
        ESP_LOGI(kTag,
                 "frame=%u sd_avg=%uus sd_max=%uus decode=%uus "
                 "render=%uus late=%u skip=%u audio=%u/%u "
                 "rebuffer=%u silence=%u loop=%u/%u hold=%u "
                 "played=%u heap=%u",
                 decoded_frames, sd_average, sd_read_us_max, decode_us,
                 render_us, dropped_deadlines,
                 skipped_presentations,
                 static_cast<unsigned>(audio_queued),
                 static_cast<unsigned>(kAudioStreamBytes),
                 static_cast<unsigned>(audio_rebuffers),
                 static_cast<unsigned>(audio_silence_chunks),
                 static_cast<unsigned>(audio_loop_events),
                 static_cast<unsigned>(audio_loop_chunks),
                 static_cast<unsigned>(audio_loop_hold),
                 static_cast<unsigned>(audio_played_samples),
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    }
    return true;
}

void readPacket(HLV1Packet *packet, int *result) {
    const int64_t read_start = microsNow();
    *result = decoder.readPacket(video_file, packet);
    const uint32_t read_us =
        static_cast<uint32_t>(microsNow() - read_start);
    sd_read_us_total += read_us;
    sd_read_us_max = std::max(sd_read_us_max, read_us);
    if (*result == HLV1_OK) ++sd_packets_read;
}

void finishVideoLoop() {
    if (audio_enabled) {
        // A held DMA ring never drains by itself. Release it so the remaining
        // queued PCM can finish before the file is reopened.
        audio_loop_hold = false;
        const int64_t deadline =
            millisNow() + kAudioClockWaitTimeoutMs;
        while (!audio_output_failed &&
               audio_reader_result >= HLV1_OK &&
               (!audio_prefetch_eof ||
                xStreamBufferBytesAvailable(audio_stream) ||
                audio_pending_samples) &&
               millisNow() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    } else {
        waitUntil(next_present_us);
    }
    ESP_LOGI(kTag,
             "Loop: %u frames, %u late, %u skipped, %u rebuffers, "
             "%u audio loops (%u DMA chunks)",
             decoded_frames, dropped_deadlines, skipped_presentations,
             static_cast<unsigned>(audio_rebuffers),
             static_cast<unsigned>(audio_loop_events),
             static_cast<unsigned>(audio_loop_chunks));
    if (!openVideo()) last_retry_ms = millisNow();
}

void playOneFrameSequential() {
    HLV1Packet packet{};
    int packet_result = HLV1_OK;
    readPacket(&packet, &packet_result);
    if (packet_result == HLV1_EOF) {
        finishVideoLoop();
        return;
    }
    if (packet_result != HLV1_OK) {
        failPlayback("Packet read error", packet_result);
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

    if (!presentFrame(frame, decode_us)) {
        failPlayback("Display DMA error", HLV1_ERR_IO);
    }
}

void playOneFramePipelined() {
    HLV1Packet packet{};
    int packet_result = HLV1_OK;
    readPacket(&packet, &packet_result);
    if (packet_result == HLV1_EOF) {
        if (pending_frame_valid) {
            const bool rendered =
                presentFrame(&pending_frame, pending_decode_us);
            pending_frame_valid = false;
            if (!rendered) {
                failPlayback("Display DMA error", HLV1_ERR_IO);
                return;
            }
        }
        finishVideoLoop();
        return;
    }
    if (packet_result != HLV1_OK) {
        failPlayback("Packet read error", packet_result);
        return;
    }
    if (!submitDecode(&packet)) {
        hlv1_packet_free(&packet);
        failPlayback("Decode pipeline error", HLV1_ERR_IO);
        return;
    }

    bool rendered = true;
    if (pending_frame_valid) {
        rendered = presentFrame(&pending_frame, pending_decode_us);
        pending_frame_valid = false;
    }

    DecodeResult result{};
    const bool received = waitDecode(&result);
    hlv1_packet_free(&packet);
    if (!received) {
        failPlayback("Decode pipeline error", HLV1_ERR_IO);
        return;
    }
    if (!rendered) {
        failPlayback("Display DMA error", HLV1_ERR_IO);
        return;
    }
    if (result.result != HLV1_OK) {
        failPlayback("Decode error", result.result);
        return;
    }
    pending_frame = *result.frame;
    pending_decode_us = result.decode_us;
    pending_frame_valid = true;
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
            if (player_settings::kUseDualCorePipeline) {
                playOneFramePipelined();
            } else {
                playOneFrameSequential();
            }
            continue;
        }
        if (millisNow() - last_retry_ms >= kRetryDelayMs) {
            last_retry_ms = millisNow();
            if (mountSdCard()) openVideo();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
