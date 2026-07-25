#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

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
#include "amrnb_3gp.h"
#include "bpv_esp32_decoder.hpp"
#include "cyd_display.hpp"
#include "h263_3gp.h"
#include "hlv1.h"
#include "hlv_esp32_decoder.hpp"
#include "mjpeg_avi_decoder.hpp"
#include "player_settings.hpp"
#include "pl_mpeg.h"
#include "uart_file_upload.hpp"

namespace {

constexpr char kTag[] = "hlv-player";
constexpr int kScreenWidth = CydDisplay::kWidth;
constexpr int kScreenHeight = CydDisplay::kHeight;
constexpr uint32_t kRetryDelayMs = 2000;
constexpr uint32_t kSdReadFailuresBeforeReinit = 3;
constexpr size_t kVideoReadAheadBytes = 16 * 1024;
constexpr size_t kMpegVideoReadAheadBytes = 4 * 1024;
constexpr size_t kH263VideoReadAheadBytes = 4 * 1024;
constexpr size_t kAudioStreamBytes = 4096;
// A FreeRTOS static stream buffer reserves one byte to distinguish full from
// empty, so the backing array is one byte larger than its useful capacity.
constexpr size_t kAudioStreamStorageBytes = kAudioStreamBytes + 1;
constexpr size_t kAudioDmaSamples = 256;
constexpr size_t kAudioDmaBufferBytes = kAudioDmaSamples * 2;
constexpr size_t kAudioDmaDescriptors = 6;
constexpr size_t kAudioReadAheadBytes = 512;
constexpr size_t kAudioReadChunkBytes = 512;
constexpr uint32_t kAudioReaderStackBytes = 6144;
constexpr uint32_t kAudioReaderStopTimeoutMs = 500;
constexpr uint32_t kAudioPrerollTimeoutMs = 3000;
constexpr uint32_t kAudioClockWaitTimeoutMs = 3000;
constexpr uint32_t kDecodeWorkerStackBytes = 4096;
constexpr int kUploadBarX = 16;
constexpr int kUploadBarWidth = kScreenWidth - 2 * kUploadBarX;
constexpr int kUploadBarHeight = CydDisplay::kRowsPerTransfer / 2;
constexpr int kUploadBarY = (kScreenHeight - kUploadBarHeight) / 2;
constexpr int kUploadBarBorder = 2;
constexpr uint16_t kUploadBarBorderColor = 0xffff;
constexpr uint16_t kUploadBarEmptyColor = 0x2104;
constexpr uint16_t kUploadBarFillColor = 0x07e0;
// Five column, seven row glyphs for printable ASCII 0x20 through 0x7e.
// Bits run from the top row (bit 0) to the bottom row (bit 6).
constexpr uint8_t kStatusFont[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // space
    {0x00, 0x00, 0x5f, 0x00, 0x00},  // !
    {0x00, 0x07, 0x00, 0x07, 0x00},  // "
    {0x14, 0x7f, 0x14, 0x7f, 0x14},  // #
    {0x24, 0x2a, 0x7f, 0x2a, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x55, 0x22, 0x50},  // &
    {0x00, 0x05, 0x03, 0x00, 0x00},  // '
    {0x00, 0x1c, 0x22, 0x41, 0x00},  // (
    {0x00, 0x41, 0x22, 0x1c, 0x00},  // )
    {0x14, 0x08, 0x3e, 0x08, 0x14},  // *
    {0x08, 0x08, 0x3e, 0x08, 0x08},  // +
    {0x00, 0x50, 0x30, 0x00, 0x00},  // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0x00, 0x60, 0x60, 0x00, 0x00},  // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3e, 0x51, 0x49, 0x45, 0x3e},  // 0
    {0x00, 0x42, 0x7f, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4b, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7f, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3c, 0x4a, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1e},  // 9
    {0x00, 0x36, 0x36, 0x00, 0x00},  // :
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ;
    {0x08, 0x14, 0x22, 0x41, 0x00},  // <
    {0x14, 0x14, 0x14, 0x14, 0x14},  // =
    {0x00, 0x41, 0x22, 0x14, 0x08},  // >
    {0x02, 0x01, 0x51, 0x09, 0x06},  // ?
    {0x32, 0x49, 0x79, 0x41, 0x3e},  // @
    {0x7e, 0x11, 0x11, 0x11, 0x7e},  // A
    {0x7f, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3e, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7f, 0x41, 0x41, 0x22, 0x1c},  // D
    {0x7f, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7f, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3e, 0x41, 0x49, 0x49, 0x7a},  // G
    {0x7f, 0x08, 0x08, 0x08, 0x7f},  // H
    {0x00, 0x41, 0x7f, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3f, 0x01},  // J
    {0x7f, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7f, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7f, 0x02, 0x0c, 0x02, 0x7f},  // M
    {0x7f, 0x04, 0x08, 0x10, 0x7f},  // N
    {0x3e, 0x41, 0x41, 0x41, 0x3e},  // O
    {0x7f, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3e, 0x41, 0x51, 0x21, 0x5e},  // Q
    {0x7f, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7f, 0x01, 0x01},  // T
    {0x3f, 0x40, 0x40, 0x40, 0x3f},  // U
    {0x1f, 0x20, 0x40, 0x20, 0x1f},  // V
    {0x3f, 0x40, 0x38, 0x40, 0x3f},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x07, 0x08, 0x70, 0x08, 0x07},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
    {0x00, 0x7f, 0x41, 0x41, 0x00},  // [
    {0x02, 0x04, 0x08, 0x10, 0x20},  // backslash
    {0x00, 0x41, 0x41, 0x7f, 0x00},  // ]
    {0x04, 0x02, 0x01, 0x02, 0x04},  // ^
    {0x40, 0x40, 0x40, 0x40, 0x40},  // _
    {0x00, 0x01, 0x02, 0x04, 0x00},  // `
    {0x20, 0x54, 0x54, 0x54, 0x78},  // a
    {0x7f, 0x48, 0x44, 0x44, 0x38},  // b
    {0x38, 0x44, 0x44, 0x44, 0x20},  // c
    {0x38, 0x44, 0x44, 0x48, 0x7f},  // d
    {0x38, 0x54, 0x54, 0x54, 0x18},  // e
    {0x08, 0x7e, 0x09, 0x01, 0x02},  // f
    {0x0c, 0x52, 0x52, 0x52, 0x3e},  // g
    {0x7f, 0x08, 0x04, 0x04, 0x78},  // h
    {0x00, 0x44, 0x7d, 0x40, 0x00},  // i
    {0x20, 0x40, 0x44, 0x3d, 0x00},  // j
    {0x7f, 0x10, 0x28, 0x44, 0x00},  // k
    {0x00, 0x41, 0x7f, 0x40, 0x00},  // l
    {0x7c, 0x04, 0x18, 0x04, 0x78},  // m
    {0x7c, 0x08, 0x04, 0x04, 0x78},  // n
    {0x38, 0x44, 0x44, 0x44, 0x38},  // o
    {0x7c, 0x14, 0x14, 0x14, 0x08},  // p
    {0x08, 0x14, 0x14, 0x18, 0x7c},  // q
    {0x7c, 0x08, 0x04, 0x04, 0x08},  // r
    {0x48, 0x54, 0x54, 0x54, 0x20},  // s
    {0x04, 0x3f, 0x44, 0x40, 0x20},  // t
    {0x3c, 0x40, 0x40, 0x20, 0x7c},  // u
    {0x1c, 0x20, 0x40, 0x20, 0x1c},  // v
    {0x3c, 0x40, 0x30, 0x40, 0x3c},  // w
    {0x44, 0x28, 0x10, 0x28, 0x44},  // x
    {0x0c, 0x50, 0x50, 0x50, 0x3c},  // y
    {0x44, 0x64, 0x54, 0x4c, 0x44},  // z
    {0x00, 0x08, 0x36, 0x41, 0x00},  // {
    {0x00, 0x00, 0x7f, 0x00, 0x00},  // |
    {0x00, 0x41, 0x36, 0x08, 0x00},  // }
    {0x08, 0x04, 0x08, 0x10, 0x08},  // ~
};

static_assert(CONFIG_FREERTOS_NUMBER_OF_CORES >= 2 ||
                  !player_settings::kUseDualCorePipeline,
              "Dual-core playback requires a two-core FreeRTOS build");
static_assert(CONFIG_DAC_DMA_AUTO_16BIT_ALIGN,
              "The DAC ring expects ESP-IDF 8-to-16-bit DMA expansion");
static_assert(player_settings::kAudioPrerollFrames > 0,
              "Audio preroll must cover at least one video frame");
static_assert(player_settings::kMaxConsecutiveVideoSkips > 0,
              "Hybrid A/V sync must permit at least one video skip");

enum class VideoCodec {
    kNone,
    kHlv,
    kMjpeg,
    kBpv,
    kMpeg1,
    kH263,
};

enum class SelectionReadResult {
    kReady,
    kMissingOrInvalid,
    kIoError,
};

enum class VideoOpenResult {
    kReady,
    kMissingOrUnsupported,
    kIoError,
};

struct DecodeRequest {
    VideoCodec codec;
    const HLV1Packet *hlv_packet;
    const BPV1Packet *bpv_packet;
    FILE *bpv_file;
    bool bpv_prefetch;
};

struct DecodeResult {
    VideoCodec codec;
    int result;
    const HLV1Frame *hlv_frame;
    const BPV1Frame *bpv_frame;
    H2633gpFrame h263_frame;
    plm_frame_t mpeg_frame;
    bool has_mpeg_frame;
    uint32_t decode_us;
    BPV1Packet bpv_next_packet;
    int bpv_read_result;
    uint32_t bpv_read_us;
};

CydDisplay display;
FILE *video_file = nullptr;
FILE *audio_file = nullptr;
HlvEsp32Decoder decoder;
MjpegAviDecoder mjpeg_decoder;
MjpegAviInfo mjpeg_info{};
BpvEsp32Decoder bpv_decoder;
BPV1Header bpv_header{};
plm_t *mpeg_video = nullptr;
plm_t *mpeg_audio = nullptr;
H2633gpDecoder *h263_decoder = nullptr;
H2633gpInfo h263_info{};
AmrNb3gpDecoder *amrnb_audio_decoder = nullptr;
AmrNb3gpInfo amrnb_audio_info{};
H263AviPcmReader *h263_avi_audio_reader = nullptr;
UartFileUpload uart_upload;
HLV1Header sequence_header{};
VideoCodec video_codec = VideoCodec::kNone;
const char *active_video_path = nullptr;
char selected_video_path[160]{};
int64_t frame_period_us = 0;
uint32_t frame_period_remainder = 0;
uint32_t frame_period_phase = 0;
int64_t next_present_us = 0;
uint32_t decoded_frames = 0;
uint32_t dropped_deadlines = 0;
int64_t last_retry_ms = 0;
uint16_t scaled_rgb_row[kScreenWidth];
uint16_t bpv_rgb_row[kScreenWidth];
uint16_t scaled_x_map[kScreenWidth];
uint16_t scaled_y_map[kScreenHeight];
uint8_t native_y_row[kScreenWidth];
uint8_t native_u_row[kScreenWidth / 2];
uint8_t native_v_row[kScreenWidth / 2];
int32_t mpeg_red_add[kScreenWidth / 2];
int32_t mpeg_green_add[kScreenWidth / 2];
int32_t mpeg_blue_add[kScreenWidth / 2];
constexpr std::array<int32_t, 256> makeLumaTable() {
    std::array<int32_t, 256> values{};
    for (int sample = 0; sample < 256; ++sample)
        values[sample] = 298 * (sample > 16 ? sample - 16 : 0);
    return values;
}

constexpr std::array<int32_t, 256> makeChromaTable(
    int multiplier, int offset) {
    std::array<int32_t, 256> values{};
    for (int sample = 0; sample < 256; ++sample)
        values[sample] = multiplier * (sample - 128) + offset;
    return values;
}

constexpr auto yuv_luma = makeLumaTable();
constexpr auto yuv_red_add = makeChromaTable(409, 128);
constexpr auto yuv_green_u_add = makeChromaTable(-100, 0);
constexpr auto yuv_green_v_add = makeChromaTable(-208, 128);
constexpr auto yuv_blue_add = makeChromaTable(516, 128);
int mpeg_cached_chroma_y = -1;
uint8_t *video_read_ahead = nullptr;
size_t video_read_ahead_size = 0;
alignas(4) uint8_t audio_read_ahead[kAudioReadAheadBytes];
alignas(4) uint8_t audio_read_chunk[kAudioReadChunkBytes];
alignas(4) uint8_t mpeg_audio_pcm[PLM_AUDIO_SAMPLES_PER_FRAME];
alignas(4) uint8_t amrnb_audio_pcm[AMRNB_SAMPLES_PER_FRAME];
sdmmc_card_t *sd_card = nullptr;
bool sd_bus_initialized = false;
bool sd_mounted = false;
uint32_t consecutive_sd_read_failures = 0;
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
volatile uint32_t audio_underrun_samples = 0;
volatile bool audio_loop_hold = false;
volatile uint32_t audio_loop_events = 0;
volatile uint32_t audio_loop_chunks = 0;
volatile uint32_t mpeg_audio_decode_frames = 0;
volatile uint32_t mpeg_audio_decode_us = 0;
volatile uint32_t mpeg_audio_convert_us = 0;
volatile uint32_t amrnb_audio_decode_frames = 0;
volatile uint32_t amrnb_audio_decode_us = 0;
volatile uint32_t amrnb_audio_convert_us = 0;
size_t audio_preroll_bytes = 0;
QueueHandle_t decode_request_queue = nullptr;
QueueHandle_t decode_result_queue = nullptr;
TaskHandle_t decode_task_handle = nullptr;
bool decode_in_flight = false;
HLV1Frame pending_frame{};
bool pending_frame_valid = false;
plm_frame_t pending_mpeg_frame{};
bool pending_mpeg_frame_valid = false;
uint32_t pending_mpeg_decode_us = 0;
H2633gpFrame pending_h263_frame{};
bool pending_h263_frame_valid = false;
uint32_t pending_h263_decode_us = 0;
bool h263_dual_buffered = false;
BPV1Frame pending_bpv_frame{};
bool pending_bpv_frame_valid = false;
BPV1Packet ready_bpv_packet{};
bool ready_bpv_packet_valid = false;
bool bpv_stream_eof = false;
uint32_t ready_bpv_read_us = 0;
uint32_t pending_read_us = 0;
uint32_t pending_decode_us = 0;
uint32_t skipped_presentations = 0;
uint32_t consecutive_skipped_presentations = 0;
int upload_progress_pixels = -1;

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
        result.codec = request.codec;
        const int64_t start = microsNow();
        if (request.codec == VideoCodec::kMpeg1) {
            plm_frame_t *frame =
                mpeg_video ? plm_decode_video(mpeg_video) : nullptr;
            if (frame) {
                result.mpeg_frame = *frame;
                result.has_mpeg_frame = true;
            }
            result.result =
                !frame && video_file && std::ferror(video_file)
                    ? HLV1_ERR_IO
                    : HLV1_OK;
        } else if (request.codec == VideoCodec::kHlv) {
            result.result =
                decoder.decode(request.hlv_packet, &result.hlv_frame);
        } else if (request.codec == VideoCodec::kBpv) {
            result.result =
                bpv_decoder.decode(request.bpv_packet, &result.bpv_frame);
        } else if (request.codec == VideoCodec::kH263) {
            result.result =
                h263_decoder && video_file
                    ? h263_3gp_decoder_decode_next(
                          h263_decoder, video_file, &result.h263_frame)
                    : H263_3GP_ERR_ARGUMENT;
        } else {
            result.result = HLV1_ERR_ARGUMENT;
        }
        result.decode_us = static_cast<uint32_t>(microsNow() - start);
        if (request.codec == VideoCodec::kBpv &&
            result.result == BPV1_OK && request.bpv_prefetch &&
            request.bpv_file) {
            const int64_t read_start = microsNow();
            result.bpv_read_result = bpv_decoder.readPacket(
                request.bpv_file, &result.bpv_next_packet);
            result.bpv_read_us =
                static_cast<uint32_t>(microsNow() - read_start);
        }
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
    if (xTaskCreatePinnedToCore(decodeTask, "video-decode",
                                kDecodeWorkerStackBytes, nullptr, 2,
                                &decode_task_handle, 1) != pdPASS) {
        vQueueDelete(decode_request_queue);
        vQueueDelete(decode_result_queue);
        decode_request_queue = nullptr;
        decode_result_queue = nullptr;
        return false;
    }
    ESP_LOGI(kTag,
             "Playback pipeline: CPU0 render/main I/O, "
             "CPU1 ordered decode/BPV prefetch, H.263 ping-pong");
    return true;
}

bool submitDecode(const HLV1Packet *packet) {
    if (!decode_task_handle || decode_in_flight || !packet) return false;
    DecodeRequest request{};
    request.codec = VideoCodec::kHlv;
    request.hlv_packet = packet;
    if (xQueueSend(decode_request_queue, &request, 0) != pdTRUE) return false;
    decode_in_flight = true;
    return true;
}

bool submitMpegDecode() {
    if (!decode_task_handle || decode_in_flight || !mpeg_video) return false;
    DecodeRequest request{};
    request.codec = VideoCodec::kMpeg1;
    if (xQueueSend(decode_request_queue, &request, 0) != pdTRUE) return false;
    decode_in_flight = true;
    return true;
}

bool submitH263Decode() {
    if (!decode_task_handle || decode_in_flight || !h263_decoder ||
        !video_file) {
        return false;
    }
    DecodeRequest request{};
    request.codec = VideoCodec::kH263;
    if (xQueueSend(decode_request_queue, &request, 0) != pdTRUE) return false;
    decode_in_flight = true;
    return true;
}

bool submitBpvDecode(const BPV1Packet *packet, FILE *file,
                     bool prefetch) {
    if (!decode_task_handle || decode_in_flight || !packet) return false;
    DecodeRequest request{};
    request.codec = VideoCodec::kBpv;
    request.bpv_packet = packet;
    request.bpv_file = file;
    request.bpv_prefetch = prefetch;
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

void stopDecodeWorker() {
    if (decode_in_flight) {
        DecodeResult ignored{};
        waitDecode(&ignored);
    }
    if (decode_task_handle) {
        vTaskDelete(decode_task_handle);
        decode_task_handle = nullptr;
    }
    if (decode_request_queue) {
        vQueueDelete(decode_request_queue);
        decode_request_queue = nullptr;
    }
    if (decode_result_queue) {
        vQueueDelete(decode_result_queue);
        decode_result_queue = nullptr;
    }
    decode_in_flight = false;
}

int clamp8(int value) {
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

bool mpegFpsRational(double fps, uint16_t *numerator,
                     uint16_t *denominator) {
    struct Rate {
        double value;
        uint16_t numerator;
        uint16_t denominator;
    };
    static constexpr Rate rates[] = {
        {24000.0 / 1001.0, 24000, 1001},
        {24.0, 24, 1},
        {25.0, 25, 1},
        {30000.0 / 1001.0, 30000, 1001},
        {30.0, 30, 1},
        {50.0, 50, 1},
        {60000.0 / 1001.0, 60000, 1001},
        {60.0, 60, 1},
    };
    for (const Rate &rate : rates) {
        if (std::fabs(fps - rate.value) < 0.01) {
            *numerator = rate.numerator;
            *denominator = rate.denominator;
            return true;
        }
    }
    return false;
}

uint8_t mpegSampleToU8(float left, float right) {
    const float mono = std::clamp((left + right) * 0.5f, -1.0f, 1.0f);
    return static_cast<uint8_t>(std::clamp(
        static_cast<int>(128.5f + mono * 127.0f),
        0, 255));
}

uint16_t yuvToRgb565(int y, int red_add, int green_add, int blue_add) {
    const int luma = yuv_luma[static_cast<uint8_t>(y)];
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
        hlv1_frame_unpack_corrected_samples(
            y_row, 0, source_y, 6,
            frame->correction_y, frame->correction_stride_y,
            native_y_row, frame->width);
        hlv1_frame_unpack_corrected_samples(
            u_row, 0, chroma_y, 5,
            frame->correction_u, frame->correction_stride_u,
            native_u_row, (frame->width + 1) / 2);
        hlv1_frame_unpack_corrected_samples(
            v_row, 0, chroma_y, 5,
            frame->correction_v, frame->correction_stride_v,
            native_v_row, (frame->width + 1) / 2);
        y_row = native_y_row;
        u_row = native_u_row;
        v_row = native_v_row;
    }
    for (int x = 0; x < frame->width; x += 2) {
        const int chroma_x = x >> 1;
        const uint8_t u = u_row[chroma_x];
        const uint8_t v = v_row[chroma_x];
        const int red_add = yuv_red_add[v];
        const int green_add =
            yuv_green_u_add[u] + yuv_green_v_add[v];
        const int blue_add = yuv_blue_add[u];
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
            const uint8_t u =
                hlv1_frame_u_sample(frame, chroma_x, chroma_y);
            const uint8_t v =
                hlv1_frame_v_sample(frame, chroma_x, chroma_y);
            red_add = yuv_red_add[v];
            green_add = yuv_green_u_add[u] + yuv_green_v_add[v];
            blue_add = yuv_blue_add[u];
            previous_chroma_x = chroma_x;
        }
        output[output_x] = yuvToRgb565(
            hlv1_frame_y_sample(frame, source_x, source_y),
            red_add, green_add, blue_add);
    }
}

void drawStatusTitle(const char *title) {
    if (!title || !*title) return;
    const size_t length = std::min<size_t>(std::strlen(title), 52);
    const int available_rows = display.rowsPerTransfer();
    const int scale =
        length * 12U <= kScreenWidth && 14 <= available_rows ? 2 : 1;
    const int glyph_advance = 6 * scale;
    const int width =
        static_cast<int>(length) * glyph_advance - scale;
    const int height = 7 * scale;
    uint16_t *pixels = display.acquireBuffer();
    if (!pixels || width <= 0 || width > kScreenWidth ||
        height > available_rows) {
        return;
    }
    std::fill_n(pixels, width * height, 0x0000);

    for (size_t index = 0; index < length; ++index) {
        unsigned char character =
            static_cast<unsigned char>(title[index]);
        if (character < 0x20 || character > 0x7e) character = '?';
        const uint8_t *columns = kStatusFont[character - 0x20];
        const int glyph_x = static_cast<int>(index) * glyph_advance;
        for (int source_y = 0; source_y < 7; ++source_y) {
            for (int source_x = 0; source_x < 5; ++source_x) {
                if (!(columns[source_x] & (1U << source_y))) continue;
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        pixels[(source_y * scale + dy) * width +
                               glyph_x + source_x * scale + dx] =
                            0xffff;
                    }
                }
            }
        }
    }
    const int x = (kScreenWidth - width) / 2;
    const int y = (kScreenHeight - height) / 2;
    if (display.drawBitmap(x, y, width, height, pixels) == ESP_OK) {
        display.flush();
    }
}

void showStatus(const char *title, const char *detail = nullptr) {
    esp_rom_printf("S,%s,%s\n", title, detail ? detail : "");
    if (detail) {
        ESP_LOGW(kTag, "%s: %s", title, detail);
    } else {
        ESP_LOGI(kTag, "%s", title);
    }
    const esp_err_t clear_result = display.clear(0x0000);
    if (clear_result != ESP_OK) {
        ESP_LOGE(kTag, "Could not clear status screen: %s",
                 esp_err_to_name(clear_result));
    } else {
        drawStatusTitle(title);
    }
}

void beginUploadProgress() {
    display.clear(0x0000);
    uint16_t *pixels = display.acquireBuffer();
    if (!pixels) return;
    for (int y = 0; y < kUploadBarHeight; ++y) {
        for (int x = 0; x < kUploadBarWidth; ++x) {
            const bool border =
                x < kUploadBarBorder ||
                x >= kUploadBarWidth - kUploadBarBorder ||
                y < kUploadBarBorder ||
                y >= kUploadBarHeight - kUploadBarBorder;
            pixels[y * kUploadBarWidth + x] =
                border ? kUploadBarBorderColor : kUploadBarEmptyColor;
        }
    }
    if (display.drawBitmap(kUploadBarX, kUploadBarY, kUploadBarWidth,
                           kUploadBarHeight, pixels) != ESP_OK) {
        ESP_LOGE(kTag, "Could not draw UART upload progress bar");
    }
    upload_progress_pixels = 0;
}

void updateUploadProgress(uint32_t received, uint32_t total, void *) {
    if (!total) return;
    const int inner_width = kUploadBarWidth - 2 * kUploadBarBorder;
    const int filled = static_cast<int>(
        (static_cast<uint64_t>(received) * inner_width) / total);
    if (filled <= upload_progress_pixels) return;

    const int changed = filled - upload_progress_pixels;
    const int x = kUploadBarX + kUploadBarBorder +
                  upload_progress_pixels;
    uint16_t *pixels = display.acquireBuffer();
    if (!pixels) return;
    const int inner_height = kUploadBarHeight - 2 * kUploadBarBorder;
    std::fill_n(pixels, changed * inner_height, kUploadBarFillColor);
    if (display.drawBitmap(x, kUploadBarY + kUploadBarBorder,
                           changed, inner_height, pixels) == ESP_OK) {
        upload_progress_pixels = filled;
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
    if (mkdir(player_settings::kVideoDirectory, 0775) != 0 &&
        errno != EEXIST) {
        ESP_LOGE(kTag, "Cannot create %s: errno=%d",
                 player_settings::kVideoDirectory, errno);
        esp_vfs_fat_sdcard_unmount("/sdcard", sd_card);
        sd_card = nullptr;
        sd_mounted = false;
        return false;
    }
    ESP_LOGI(kTag, "microSD: SPI3 at %d kHz with DMA",
             player_settings::kSdClockKhz);
    if (!player_settings::kLogFrameTimings) {
        sdmmc_card_print_info(stdout, sd_card);
    }
    return true;
}

uint32_t readLe32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

int prefetchAudioBytes(size_t remaining) {
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
                    audio_preroll_bytes) {
                audio_rebuffering = false;
            }
        }
        remaining -= chunk;
    }
    return HLV1_OK;
}

int prefetchHlvAudioPacket() {
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

    return prefetchAudioBytes(payload_size - video_bytes);
}

int prefetchMjpegAudioChunk() {
    uint32_t payload_size = 0;
    const int result =
        mjpeg_avi_next_audio_chunk(audio_file, mjpeg_info, &payload_size);
    if (result != MJPEG_AVI_OK) return result;
    const int audio_result = prefetchAudioBytes(payload_size);
    if (audio_result != HLV1_OK) return audio_result;
    if ((payload_size & 1U) && std::fseek(audio_file, 1, SEEK_CUR)) {
        return MJPEG_AVI_ERR_IO;
    }
    return MJPEG_AVI_OK;
}

int prefetchBpvAudioPacket() {
    BPV1FrameInfo info{};
    const int result =
        bpv1_frame_info_read(audio_file, &bpv_header, &info);
    if (result != BPV1_OK) return result;
    if (info.frame_bytes > LONG_MAX ||
        std::fseek(audio_file, static_cast<long>(info.frame_bytes),
                   SEEK_CUR)) {
        return BPV1_ERR_IO;
    }
    return prefetchAudioBytes(info.audio_bytes);
}

int prefetchMpegAudioFrame() {
    if (!mpeg_audio) return HLV1_ERR_FORMAT;
    const int64_t decode_start = microsNow();
    plm_samples_t *samples = plm_decode_audio(mpeg_audio);
    const uint32_t decode_us =
        static_cast<uint32_t>(microsNow() - decode_start);
    if (!samples) {
        return audio_file && std::ferror(audio_file)
                   ? HLV1_ERR_IO
                   : HLV1_EOF;
    }
    mpeg_audio_decode_frames = mpeg_audio_decode_frames + 1;
    mpeg_audio_decode_us = mpeg_audio_decode_us + decode_us;
    const int64_t convert_start = microsNow();
    for (unsigned index = 0; index < samples->count; ++index) {
        mpeg_audio_pcm[index] = mpegSampleToU8(
            samples->interleaved[index * 2U],
            samples->interleaved[index * 2U + 1U]);
    }
    mpeg_audio_convert_us =
        mpeg_audio_convert_us +
        static_cast<uint32_t>(microsNow() - convert_start);
    size_t sent = 0;
    while (sent < samples->count && !audio_reader_stop_requested) {
        sent += xStreamBufferSend(
            audio_stream, mpeg_audio_pcm + sent,
            samples->count - sent, pdMS_TO_TICKS(20));
        if (audio_rebuffering &&
            xStreamBufferBytesAvailable(audio_stream) >=
                audio_preroll_bytes) {
            audio_rebuffering = false;
        }
    }
    return HLV1_OK;
}

int prefetchAmrNbAudioFrame() {
    if (!amrnb_audio_decoder) return AMRNB_3GP_ERR_FORMAT;
    AmrNb3gpFrame frame{};
    const int64_t decode_start = microsNow();
    const int result = amrnb_3gp_decoder_decode_next(
        amrnb_audio_decoder, audio_file, &frame);
    const uint32_t decode_us =
        static_cast<uint32_t>(microsNow() - decode_start);
    if (result != AMRNB_3GP_OK) return result;
    amrnb_audio_decode_frames = amrnb_audio_decode_frames + 1;
    amrnb_audio_decode_us = amrnb_audio_decode_us + decode_us;

    const int64_t convert_start = microsNow();
    for (uint16_t index = 0; index < frame.sample_count; ++index) {
        amrnb_audio_pcm[index] = static_cast<uint8_t>(
            (static_cast<int32_t>(frame.samples[index]) + 32768) >> 8);
    }
    amrnb_audio_convert_us =
        amrnb_audio_convert_us +
        static_cast<uint32_t>(microsNow() - convert_start);

    size_t sent = 0;
    while (sent < frame.sample_count && !audio_reader_stop_requested) {
        sent += xStreamBufferSend(
            audio_stream, amrnb_audio_pcm + sent,
            frame.sample_count - sent, pdMS_TO_TICKS(20));
        if (audio_rebuffering &&
            xStreamBufferBytesAvailable(audio_stream) >=
                audio_preroll_bytes) {
            audio_rebuffering = false;
        }
    }
    return AMRNB_3GP_OK;
}

int prefetchH263AviPcmFrame() {
    if (!h263_avi_audio_reader) return H263_3GP_ERR_FORMAT;
    H263AviPcmFrame frame{};
    const int result = h263_avi_pcm_reader_decode_next(
        h263_avi_audio_reader, audio_file, &frame);
    if (result != H263_3GP_OK) return result;

    size_t sent = 0;
    while (sent < frame.sample_count &&
           !audio_reader_stop_requested) {
        sent += xStreamBufferSend(
            audio_stream, frame.samples + sent,
            frame.sample_count - sent, pdMS_TO_TICKS(20));
        if (audio_rebuffering &&
            xStreamBufferBytesAvailable(audio_stream) >=
                audio_preroll_bytes) {
            audio_rebuffering = false;
        }
    }
    return H263_3GP_OK;
}

int prefetchAudioPacket() {
    if (video_codec == VideoCodec::kMpeg1)
        return prefetchMpegAudioFrame();
    if (video_codec == VideoCodec::kH263) {
        return h263_info.container == H263_CONTAINER_AVI
                   ? prefetchH263AviPcmFrame()
                   : prefetchAmrNbAudioFrame();
    }
    if (video_codec == VideoCodec::kMjpeg)
        return prefetchMjpegAudioChunk();
    if (video_codec == VideoCodec::kBpv)
        return prefetchBpvAudioPacket();
    return prefetchHlvAudioPacket();
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
            if (!audio_prefetch_eof) {
                audio_underrun_samples =
                    audio_underrun_samples +
                    static_cast<uint32_t>(
                        sizeof audio_dma_samples - received);
            }
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
        if (mpeg_audio) {
            plm_destroy(mpeg_audio);
            mpeg_audio = nullptr;
        }
        if (amrnb_audio_decoder) {
            amrnb_3gp_decoder_destroy(amrnb_audio_decoder);
            amrnb_audio_decoder = nullptr;
        }
        std::fclose(audio_file);
        audio_file = nullptr;
    }
    if (h263_avi_audio_reader) {
        h263_avi_pcm_reader_destroy(h263_avi_audio_reader);
        h263_avi_audio_reader = nullptr;
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
    audio_underrun_samples = 0;
    audio_loop_hold = false;
    audio_loop_events = 0;
    audio_loop_chunks = 0;
    mpeg_audio_decode_frames = 0;
    mpeg_audio_decode_us = 0;
    mpeg_audio_convert_us = 0;
    amrnb_audio_decode_frames = 0;
    amrnb_audio_decode_us = 0;
    amrnb_audio_convert_us = 0;
    amrnb_audio_info = {};
    audio_preroll_bytes = 0;
    consecutive_skipped_presentations = 0;
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

    audio_file = std::fopen(active_video_path, "rb");
    if (!audio_file ||
        std::setvbuf(audio_file,
                     reinterpret_cast<char *>(audio_read_ahead),
                     _IOFBF, sizeof audio_read_ahead)) {
        stopAudio();
        return false;
    }
    if (video_codec == VideoCodec::kMpeg1) {
        mpeg_audio = plm_create_with_file(audio_file, 0);
        if (!mpeg_audio) {
            stopAudio();
            return false;
        }
        plm_set_video_enabled(mpeg_audio, 0);
        if (plm_get_num_audio_streams(mpeg_audio) < 1 ||
            plm_get_samplerate(mpeg_audio) !=
                header.audio_sample_rate) {
            stopAudio();
            return false;
        }
    } else if (video_codec == VideoCodec::kH263) {
        if (h263_info.container == H263_CONTAINER_AVI) {
            H2633gpInfo audio_info{};
            h263_avi_audio_reader = h263_avi_pcm_reader_create();
            const int result =
                h263_avi_audio_reader
                    ? h263_avi_pcm_reader_open(
                          h263_avi_audio_reader, audio_file,
                          &audio_info)
                    : H263_3GP_ERR_MEMORY;
            if (result != H263_3GP_OK ||
                audio_info.width != header.width ||
                audio_info.height != header.height ||
                audio_info.fps_num != header.fps_num ||
                audio_info.fps_den != header.fps_den ||
                audio_info.frame_count != header.frame_count ||
                audio_info.audio_sample_rate !=
                    header.audio_sample_rate ||
                audio_info.audio_channels != 1 ||
                (audio_info.audio_bits_per_sample != 8 &&
                 audio_info.audio_bits_per_sample != 16)) {
                stopAudio();
                return false;
            }
        } else {
            amrnb_audio_decoder = amrnb_3gp_decoder_create();
            const int result =
                amrnb_audio_decoder
                    ? amrnb_3gp_decoder_open(
                          amrnb_audio_decoder, audio_file,
                          &amrnb_audio_info)
                    : AMRNB_3GP_ERR_MEMORY;
            if (result != AMRNB_3GP_OK ||
                amrnb_audio_info.sample_rate !=
                    header.audio_sample_rate ||
                amrnb_audio_info.channels != 1) {
                stopAudio();
                return false;
            }
        }
    } else if (video_codec == VideoCodec::kMjpeg) {
        MjpegAviInfo audio_info{};
        if (mjpeg_avi_read_info(audio_file, &audio_info) != MJPEG_AVI_OK ||
            audio_info.width != header.width ||
            audio_info.height != header.height ||
            audio_info.fps_num != header.fps_num ||
            audio_info.fps_den != header.fps_den ||
            audio_info.frame_count != header.frame_count ||
            audio_info.audio_sample_rate != header.audio_sample_rate ||
            audio_info.audio_channels != 1 ||
            audio_info.audio_bits_per_sample != 8) {
            stopAudio();
            return false;
        }
    } else if (video_codec == VideoCodec::kBpv) {
        BPV1Header audio_header{};
        if (bpv1_header_read(audio_file, &audio_header) != BPV1_OK ||
            audio_header.width != header.width ||
            audio_header.height != header.height ||
            audio_header.fps_num != header.fps_num ||
            audio_header.fps_den != header.fps_den ||
            audio_header.frame_count != header.frame_count ||
            audio_header.audio_sample_rate != header.audio_sample_rate ||
            audio_header.audio_codec != BPV1_AUDIO_PCM_U8 ||
            audio_header.audio_channels != 1) {
            stopAudio();
            return false;
        }
    } else {
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

    const uint64_t audio_samples_per_frame =
        (static_cast<uint64_t>(header.audio_sample_rate) *
             header.fps_den +
         header.fps_num - 1U) /
        header.fps_num;
    audio_preroll_bytes = static_cast<size_t>(std::min<uint64_t>(
        kAudioStreamBytes,
        audio_samples_per_frame *
            player_settings::kAudioPrerollFrames));

    audio_reader_stop_requested = false;
    audio_prefetch_eof = false;
    audio_reader_result = HLV1_OK;
    if (xTaskCreatePinnedToCore(
            audioReaderTask, "video-audio-read", kAudioReaderStackBytes,
            nullptr, 3, &audio_reader_task_handle, 0) != pdPASS) {
        stopAudio();
        return false;
    }

    const int64_t preroll_deadline =
        millisNow() + kAudioPrerollTimeoutMs;
    while (xStreamBufferBytesAvailable(audio_stream) <
               audio_preroll_bytes &&
           !audio_prefetch_eof && audio_reader_result == HLV1_OK &&
           millisNow() < preroll_deadline) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    const size_t prefetched =
        xStreamBufferBytesAvailable(audio_stream);
    if (audio_reader_result < HLV1_OK || !prefetched ||
        (!audio_prefetch_eof && prefetched < audio_preroll_bytes)) {
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
    stopDecodeWorker();
    pending_frame_valid = false;
    pending_mpeg_frame_valid = false;
    pending_h263_frame_valid = false;
    pending_h263_frame = {};
    pending_h263_decode_us = 0;
    h263_dual_buffered = false;
    pending_bpv_frame_valid = false;
    ready_bpv_packet = {};
    ready_bpv_packet_valid = false;
    bpv_stream_eof = false;
    ready_bpv_read_us = 0;
    pending_read_us = 0;
    pending_decode_us = 0;
    stopAudio();
    decoder.end();
    mjpeg_decoder.end();
    mjpeg_info = {};
    bpv_decoder.end();
    bpv_header = {};
    if (mpeg_video) {
        plm_destroy(mpeg_video);
        mpeg_video = nullptr;
    }
    if (h263_decoder) {
        h263_3gp_decoder_destroy(h263_decoder);
        h263_decoder = nullptr;
    }
    h263_info = {};
    if (video_file) {
        std::fclose(video_file);
        video_file = nullptr;
    }
    heap_caps_free(video_read_ahead);
    video_read_ahead = nullptr;
    video_read_ahead_size = 0;
    video_codec = VideoCodec::kNone;
    active_video_path = nullptr;
}

void deinitializeSdCard() {
    closeVideo();

    if (sd_mounted && sd_card) {
        const esp_err_t unmount_result =
            esp_vfs_fat_sdcard_unmount("/sdcard", sd_card);
        if (unmount_result != ESP_OK) {
            ESP_LOGE(kTag, "microSD unmount failed: %s",
                     esp_err_to_name(unmount_result));
        }
    }
    sd_card = nullptr;
    sd_mounted = false;

    if (sd_bus_initialized) {
        const esp_err_t bus_result = spi_bus_free(SPI3_HOST);
        if (bus_result == ESP_OK) {
            sd_bus_initialized = false;
        } else {
            ESP_LOGE(kTag, "SD SPI3 release failed: %s",
                     esp_err_to_name(bus_result));
        }
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

bool isSafeVideoFilename(const char *name) {
    if (!name || !*name || !std::strcmp(name, ".") ||
        !std::strcmp(name, "..") || std::strstr(name, "..")) {
        return false;
    }
    for (const unsigned char *cursor =
             reinterpret_cast<const unsigned char *>(name);
         *cursor; ++cursor) {
        const bool letter = (*cursor >= 'a' && *cursor <= 'z') ||
                            (*cursor >= 'A' && *cursor <= 'Z');
        const bool digit = *cursor >= '0' && *cursor <= '9';
        if (!letter && !digit && *cursor != '.' && *cursor != '_' &&
            *cursor != '-' && *cursor != ' ') {
            return false;
        }
    }
    return true;
}

SelectionReadResult readSelectedVideoPath() {
    errno = 0;
    FILE *selection =
        std::fopen(player_settings::kVideoSelectionPath, "rb");
    if (!selection) {
        return errno == ENOENT
                   ? SelectionReadResult::kMissingOrInvalid
                   : SelectionReadResult::kIoError;
    }

    char filename[112]{};
    const bool read =
        std::fgets(filename, sizeof filename, selection) != nullptr;
    const bool io_error = std::ferror(selection) != 0;
    const bool close_error = std::fclose(selection) != 0;
    if (io_error || close_error) return SelectionReadResult::kIoError;
    if (!read) return SelectionReadResult::kMissingOrInvalid;

    char *start = filename;
    while (*start == ' ' || *start == '\t') ++start;
    char *end = start + std::strlen(start);
    while (end > start &&
           (end[-1] == '\r' || end[-1] == '\n' ||
            end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    *end = '\0';
    if (!isSafeVideoFilename(start)) {
        return SelectionReadResult::kMissingOrInvalid;
    }

    const int written = std::snprintf(
        selected_video_path, sizeof selected_video_path, "%s/%s",
        player_settings::kVideoDirectory, start);
    return written > 0 &&
                   static_cast<size_t>(written) <
                       sizeof selected_video_path
               ? SelectionReadResult::kReady
               : SelectionReadResult::kMissingOrInvalid;
}

VideoOpenResult openVideoCandidate(const char *path) {
    errno = 0;
    video_file = std::fopen(path, "rb");
    if (!video_file) {
        return errno == ENOENT
                   ? VideoOpenResult::kMissingOrUnsupported
                   : VideoOpenResult::kIoError;
    }
    uint8_t signature[12]{};
    const size_t signature_size =
        std::fread(signature, 1, sizeof signature, video_file);
    const bool io_error = std::ferror(video_file) != 0 ||
                          std::fseek(video_file, 0, SEEK_SET) != 0;
    if (io_error) {
        std::fclose(video_file);
        video_file = nullptr;
        return VideoOpenResult::kIoError;
    }
    if (signature_size >= 4 && !std::memcmp(signature, "HLV1", 4)) {
        video_codec = VideoCodec::kHlv;
    } else if (signature_size >= 4 &&
               !std::memcmp(signature, "BPV1", 4)) {
        video_codec = VideoCodec::kBpv;
    } else if (signature_size == sizeof signature &&
               !std::memcmp(signature, "RIFF", 4) &&
               !std::memcmp(signature + 8, "AVI ", 4)) {
        H2633gpInfo probe_info{};
        const int probe_result =
            h263_avi_probe(video_file, &probe_info);
        if (std::fseek(video_file, 0, SEEK_SET) != 0) {
            std::fclose(video_file);
            video_file = nullptr;
            return VideoOpenResult::kIoError;
        }
        video_codec =
            probe_result == H263_3GP_OK
                ? VideoCodec::kH263
                : VideoCodec::kMjpeg;
    } else if (signature_size >= 4 &&
               signature[0] == 0x00 && signature[1] == 0x00 &&
               signature[2] == 0x01 && signature[3] == 0xba) {
        video_codec = VideoCodec::kMpeg1;
    } else if (signature_size == sizeof signature &&
               !std::memcmp(signature + 4, "ftyp", 4)) {
        video_codec = VideoCodec::kH263;
    } else {
        std::fclose(video_file);
        video_file = nullptr;
        return VideoOpenResult::kMissingOrUnsupported;
    }
    active_video_path = path;
    return VideoOpenResult::kReady;
}

int probeMpegAudioSampleRate(const char *path) {
    FILE *file = std::fopen(path, "rb");
    if (!file) return 0;
    plm_t *mpeg = plm_create_with_file(file, 0);
    int sample_rate = 0;
    if (mpeg) {
        plm_set_video_enabled(mpeg, 0);
        if (plm_get_num_audio_streams(mpeg) > 0)
            sample_rate = plm_get_samplerate(mpeg);
        plm_destroy(mpeg);
    }
    std::fclose(file);
    return sample_rate;
}

int probeAmrNbAudio(const char *path, AmrNb3gpInfo *info) {
    FILE *file = std::fopen(path, "rb");
    if (!file) return AMRNB_3GP_ERR_IO;
    AmrNb3gpDecoder *probe = amrnb_3gp_decoder_create();
    const int result =
        probe
            ? amrnb_3gp_decoder_open(probe, file, info)
            : AMRNB_3GP_ERR_MEMORY;
    amrnb_3gp_decoder_destroy(probe);
    std::fclose(file);
    return result;
}

bool reopenVideoAt(long offset) {
    if (!active_video_path || !video_read_ahead ||
        !video_read_ahead_size || offset < 0) {
        return false;
    }
    if (video_file) std::fclose(video_file);
    video_file = std::fopen(active_video_path, "rb");
    if (!video_file) return false;
    if (std::setvbuf(video_file,
                     reinterpret_cast<char *>(video_read_ahead),
                     _IOFBF, video_read_ahead_size) ||
        std::fseek(video_file, offset, SEEK_SET)) {
        std::fclose(video_file);
        video_file = nullptr;
        return false;
    }
    return true;
}

bool openVideo() {
    closeVideo();
    const SelectionReadResult selection_result = readSelectedVideoPath();
    if (selection_result == SelectionReadResult::kIoError) {
        showStatus("SD CARD READ ERROR", "cannot read /HLV/play.txt");
        return false;
    }
    if (selection_result != SelectionReadResult::kReady) {
        showStatus("NO SELECTED FILE.",
                   "create /HLV/play.txt");
        return false;
    }
    const VideoOpenResult open_result =
        openVideoCandidate(selected_video_path);
    if (open_result == VideoOpenResult::kIoError) {
        showStatus("SD CARD READ ERROR", "cannot open selected video");
        return false;
    }
    if (open_result != VideoOpenResult::kReady) {
        showStatus("SELECTED FILE ERROR",
                   "missing or unsupported video");
        return false;
    }
    const bool use_double_display_buffer =
        video_codec != VideoCodec::kH263;
    if (display.setDoubleBuffered(use_double_display_buffer) != ESP_OK) {
        showStatus("Not enough RAM", "display buffer allocation failed");
        closeVideo();
        return false;
    }
    video_read_ahead_size =
        video_codec == VideoCodec::kMpeg1
            ? kMpegVideoReadAheadBytes
            : (video_codec == VideoCodec::kH263
                   ? kH263VideoReadAheadBytes
                   : kVideoReadAheadBytes);
    video_read_ahead = static_cast<uint8_t *>(
        heap_caps_malloc(video_read_ahead_size, MALLOC_CAP_8BIT));
    if (!video_read_ahead) {
        showStatus("Not enough RAM", "read-ahead allocation failed");
        closeVideo();
        return false;
    }
    if (std::setvbuf(video_file,
                     reinterpret_cast<char *>(video_read_ahead),
                     _IOFBF, video_read_ahead_size)) {
        showStatus("SD setup failed", "cannot configure read-ahead");
        closeVideo();
        return false;
    }

    sequence_header = {};
    reportHeap("before decoder");
    if (video_codec == VideoCodec::kMpeg1) {
        const int audio_sample_rate =
            probeMpegAudioSampleRate(active_video_path);
        std::clearerr(video_file);
        errno = 0;
        mpeg_video = plm_create_with_file(video_file, 0);
        if (!mpeg_video) {
            showStatus("Not enough RAM", "MPEG-1 demux allocation failed");
            closeVideo();
            return false;
        }
        plm_set_audio_enabled(mpeg_video, 0);
        const int width = plm_get_width(mpeg_video);
        const int height = plm_get_height(mpeg_video);
        const double fps = plm_get_framerate(mpeg_video);
        const double duration = plm_get_duration(mpeg_video);
        const int probe_errno = errno;
        const bool probe_io_error = std::ferror(video_file) != 0;
        const long probe_position = std::ftell(video_file);
        if (width <= 0 || width > UINT16_MAX ||
            height <= 0 || height > UINT16_MAX ||
            !mpegFpsRational(
                fps, &sequence_header.fps_num,
                &sequence_header.fps_den) ||
            !(duration > 0.0)) {
            ESP_LOGE(kTag,
                     "MPEG probe failed: %dx%d fps=%.6f "
                     "duration=%.6f pos=%ld ferror=%d errno=%d",
                     width, height, fps, duration, probe_position,
                     probe_io_error ? 1 : 0, probe_errno);
            showStatus("Invalid video.mpg", "unsupported MPEG-1 stream");
            closeVideo();
            return false;
        }
        sequence_header.width = static_cast<uint16_t>(width);
        sequence_header.height = static_cast<uint16_t>(height);
        const double frame_count =
            std::floor(duration * sequence_header.fps_num /
                       sequence_header.fps_den + 0.5);
        if (frame_count < 1.0 || frame_count > UINT32_MAX) {
            showStatus("Invalid video.mpg", "invalid duration");
            closeVideo();
            return false;
        }
        sequence_header.frame_count =
            static_cast<uint32_t>(frame_count);
        if (audio_sample_rate > 0 && audio_sample_rate <= UINT16_MAX) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            sequence_header.audio_codec = HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                static_cast<uint16_t>(audio_sample_rate);
            sequence_header.audio_channels = 1;
        }
        plm_rewind(mpeg_video);
        ESP_LOGI(kTag,
                 "MPEG-1/PS: %ux%u, %u/%u fps, ~%u frames, "
                 "MP2 audio=%u Hz, no-B two-frame decoder",
                 sequence_header.width, sequence_header.height,
                 sequence_header.fps_num, sequence_header.fps_den,
                 static_cast<unsigned>(sequence_header.frame_count),
                 sequence_header.audio_sample_rate);
        if (!startDecodeWorker()) {
            showStatus("Dual-core init failed",
                       "cannot create CPU1 decoder task");
            closeVideo();
            return false;
        }
    } else if (video_codec == VideoCodec::kH263) {
        h263_decoder = h263_3gp_decoder_create();
        if (h263_decoder && player_settings::kUseDualCorePipeline) {
            h263_3gp_decoder_set_output_buffer_count(h263_decoder, 2);
        }
        int result =
            h263_decoder
                ? h263_3gp_decoder_open(
                      h263_decoder, video_file, &h263_info)
                : H263_3GP_ERR_MEMORY;
        if (result == H263_3GP_ERR_FRAME_MEMORY &&
            player_settings::kUseDualCorePipeline && h263_decoder) {
            ESP_LOGW(kTag,
                     "H.263 second output buffer unavailable; "
                     "falling back to sequential decode");
            h263_3gp_decoder_set_output_buffer_count(h263_decoder, 1);
            result = h263_3gp_decoder_open(
                h263_decoder, video_file, &h263_info);
        }
        if (result == H263_3GP_OK &&
            (h263_info.fps_num > UINT16_MAX ||
             h263_info.fps_den > UINT16_MAX)) {
            result = H263_3GP_ERR_UNSUPPORTED;
        }
        if (result != H263_3GP_OK) {
            showStatus("Invalid H263 video", h263_3gp_strerror(result));
            closeVideo();
            return false;
        }
        sequence_header.width = h263_info.width;
        sequence_header.height = h263_info.height;
        sequence_header.fps_num =
            static_cast<uint16_t>(h263_info.fps_num);
        sequence_header.fps_den =
            static_cast<uint16_t>(h263_info.fps_den);
        sequence_header.frame_count = h263_info.frame_count;
        if (h263_info.container == H263_CONTAINER_AVI &&
            h263_info.audio_sample_rate) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            sequence_header.audio_codec = HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                static_cast<uint16_t>(h263_info.audio_sample_rate);
            sequence_header.audio_channels = h263_info.audio_channels;
        } else if (h263_info.container == H263_CONTAINER_3GP) {
            AmrNb3gpInfo audio_info{};
            const int audio_result =
                probeAmrNbAudio(active_video_path, &audio_info);
            if (audio_result == AMRNB_3GP_OK) {
                sequence_header.flags = HLV1_FLAG_AUDIO;
                sequence_header.audio_codec = HLV1_AUDIO_PCM_U8;
                sequence_header.audio_sample_rate =
                    audio_info.sample_rate;
                sequence_header.audio_channels = audio_info.channels;
            } else if (audio_result != AMRNB_3GP_ERR_UNSUPPORTED) {
                showStatus("Invalid audio.3gp",
                           amrnb_3gp_strerror(audio_result));
                closeVideo();
                return false;
            }
        }
        ESP_LOGI(kTag,
                 "H.263/%s: %ux%u, %u/%u fps, %u frames, "
                 "profile=%u level=%u, audio=%u Hz/%u-bit, "
                 "decoder=%u bytes",
                 h263_info.container == H263_CONTAINER_AVI
                     ? "AVI"
                     : "3GP",
                 sequence_header.width, sequence_header.height,
                 sequence_header.fps_num, sequence_header.fps_den,
                 static_cast<unsigned>(sequence_header.frame_count),
                 h263_info.profile, h263_info.level,
                 sequence_header.audio_sample_rate,
                 h263_info.container == H263_CONTAINER_AVI
                     ? h263_info.audio_bits_per_sample
                     : 0,
                 static_cast<unsigned>(
                     h263_3gp_decoder_memory_bytes(h263_decoder)));
        h263_dual_buffered =
            h263_3gp_decoder_output_buffer_count(h263_decoder) == 2;
        if (h263_dual_buffered && !startDecodeWorker()) {
            showStatus("Dual-core init failed",
                       "cannot create CPU1 decoder task");
            closeVideo();
            return false;
        }
    } else if (video_codec == VideoCodec::kMjpeg) {
        int result = mjpeg_decoder.begin(video_file, &mjpeg_info);
        if (result == MJPEG_AVI_OK &&
            (mjpeg_info.fps_num > UINT16_MAX ||
             mjpeg_info.fps_den > UINT16_MAX ||
             mjpeg_info.audio_sample_rate > UINT16_MAX)) {
            result = MJPEG_AVI_ERR_RANGE;
        }
        if (result != MJPEG_AVI_OK) {
            showStatus("Invalid video.avi", mjpeg_avi_strerror(result));
            closeVideo();
            return false;
        }
        sequence_header.width = mjpeg_info.width;
        sequence_header.height = mjpeg_info.height;
        sequence_header.fps_num =
            static_cast<uint16_t>(mjpeg_info.fps_num);
        sequence_header.fps_den =
            static_cast<uint16_t>(mjpeg_info.fps_den);
        sequence_header.frame_count = mjpeg_info.frame_count;
        if (mjpeg_info.audio_stream != 0xff) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            sequence_header.audio_codec = HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                static_cast<uint16_t>(mjpeg_info.audio_sample_rate);
            sequence_header.audio_channels = 1;
        }
        ESP_LOGI(kTag,
                 "MJPEG/AVI: %ux%u, %u/%u fps, %u frames, "
                 "PCM_U8 audio=%u Hz, max JPEG=%u",
                 sequence_header.width, sequence_header.height,
                 sequence_header.fps_num, sequence_header.fps_den,
                 static_cast<unsigned>(sequence_header.frame_count),
                 sequence_header.audio_sample_rate,
                 static_cast<unsigned>(
                     mjpeg_decoder.compressedCapacity()));
    } else if (video_codec == VideoCodec::kBpv) {
        const int result = bpv_decoder.begin(video_file, &bpv_header);
        if (result != BPV1_OK) {
            showStatus("Invalid video.bpv1", bpv1_strerror(result));
            closeVideo();
            return false;
        }
        sequence_header.width = bpv_header.width;
        sequence_header.height = bpv_header.height;
        sequence_header.fps_num = bpv_header.fps_num;
        sequence_header.fps_den = bpv_header.fps_den;
        sequence_header.frame_count = bpv_header.frame_count;
        sequence_header.gop = bpv_header.keyframe_interval;
        sequence_header.version = bpv_header.version;
        sequence_header.search_radius = bpv_header.search_radius;
        if (bpv_header.audio_codec == BPV1_AUDIO_PCM_U8) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            sequence_header.audio_codec = HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                bpv_header.audio_sample_rate;
            sequence_header.audio_channels = bpv_header.audio_channels;
        }
        ESP_LOGI(kTag,
                 "BPV1 v%u: %ux%u, %u/%u fps, %u frames, "
                 "audio=%u Hz, decoder=%u bytes, packet=%u bytes",
                 bpv_header.version, bpv_header.width, bpv_header.height,
                 bpv_header.fps_num, bpv_header.fps_den,
                 static_cast<unsigned>(bpv_header.frame_count),
                 bpv_header.audio_sample_rate,
                 static_cast<unsigned>(bpv_decoder.memoryBytes()),
                 static_cast<unsigned>(bpv_decoder.packetCapacity()));
        if (!startDecodeWorker()) {
            showStatus("Dual-core init failed",
                       "cannot create CPU1 decoder task");
            closeVideo();
            return false;
        }
    } else {
        const int header_result =
            hlv1_header_read(video_file, &sequence_header);
        if (header_result != HLV1_OK) {
            showStatus("Invalid video.hlv",
                       hlv1_strerror(header_result));
            closeVideo();
            return false;
        }
        ESP_LOGI(kTag, "HLV: %ux%u, %u/%u fps, %u frames, audio=%u Hz",
                 sequence_header.width, sequence_header.height,
                 sequence_header.fps_num, sequence_header.fps_den,
                 static_cast<unsigned>(sequence_header.frame_count),
                 sequence_header.audio_sample_rate);
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
        // worker stack, preserving the largest contiguous heap regions.
        if (!startDecodeWorker()) {
            showStatus("Dual-core init failed",
                       "cannot create CPU1 decoder task");
            closeVideo();
            return false;
        }
    }
    if (sequence_header.width > kScreenWidth ||
        sequence_header.height > kScreenHeight) {
        ESP_LOGE(kTag, "Unsupported dimensions: %ux%u",
                 sequence_header.width, sequence_header.height);
        showStatus("Video is too large", "maximum size is 320x240");
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

    const uint64_t frame_period_numerator =
        1000000ULL * sequence_header.fps_den;
    frame_period_us = static_cast<int64_t>(
        frame_period_numerator / sequence_header.fps_num);
    frame_period_remainder = static_cast<uint32_t>(
        frame_period_numerator % sequence_header.fps_num);
    frame_period_phase = 0;
    next_present_us = microsNow();
    decoded_frames = 0;
    dropped_deadlines = 0;
    skipped_presentations = 0;
    consecutive_skipped_presentations = 0;
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

    if (video_codec == VideoCodec::kMpeg1) {
        ESP_LOGI(kTag,
                 "Playing MPEG-1 in %s mode, frame storage=two YCbCr "
                 "reference frames",
                 player_settings::kScaleVideoToDisplay
                     ? "scale-to-320x240"
                     : "native-centred");
    } else if (video_codec == VideoCodec::kH263) {
        ESP_LOGI(kTag,
                 "Playing H.263/%s in %s mode, "
                 "frame storage=bounded YUV420 frame buffers",
                 h263_info.container == H263_CONTAINER_AVI
                     ? "AVI"
                     : "3GP",
                 player_settings::kScaleVideoToDisplay
                     ? "scale-to-320x240"
                     : "native-centred");
    } else if (video_codec == VideoCodec::kMjpeg) {
        ESP_LOGI(kTag,
                 "Playing MJPEG in %s mode, frame storage=RGB565 strip",
                 player_settings::kScaleVideoToDisplay
                     ? "scale-to-320x240"
                     : "native-centred");
    } else if (video_codec == VideoCodec::kBpv) {
        ESP_LOGI(kTag,
                 "Playing BPV1 v%u in %s mode, frame storage=4x4 records",
                 bpv_header.version,
                 player_settings::kScaleVideoToDisplay
                     ? "scale-to-320x240"
                     : "native-centred");
    } else {
        ESP_LOGI(kTag, "Playing HLV v%u in %s mode, frame storage=%s",
                 sequence_header.version,
                 player_settings::kScaleVideoToDisplay
                     ? "scale-to-320x240"
                     : "native-centred",
                 decoder.compactYuv() ? "packed Y6/U5/V5 + Q4 corrections"
                                      : "8-bit YUV 4:2:0");
    }
    reportHeap("decoder ready");
    if (player_settings::kLogFrameTimings) {
        esp_rom_printf(
            "V,%u,%u,%u,%u,%u,%u\n",
            sequence_header.width, sequence_header.height,
            sequence_header.fps_num, sequence_header.fps_den,
            sequence_header.audio_sample_rate,
            static_cast<unsigned>(sequence_header.frame_count));
        esp_rom_printf(
            "#frame,sd_us,decode_us,render_us,work_us,present_us\n");
    }
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
    const int rows_per_transfer = display.rowsPerTransfer();
    if (player_settings::kScaleVideoToDisplay) {
        int cached_source_y = -1;
        for (int y0 = 0; y0 < kScreenHeight; y0 += rows_per_transfer) {
            const int rows =
                std::min(rows_per_transfer, kScreenHeight - y0);
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
    for (int y0 = 0; y0 < frame->height; y0 += rows_per_transfer) {
        const int rows = std::min(rows_per_transfer, frame->height - y0);
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

void convertMpegRow(const plm_frame_t *frame, int source_y,
                    bool scaled, uint16_t *output, int output_width) {
    const uint8_t *y_row =
        frame->y.data + static_cast<size_t>(source_y) * frame->y.stride;
    const int chroma_y = source_y >> 1;
    const uint8_t *cb_row =
        frame->cb.data +
        static_cast<size_t>(chroma_y) * frame->cb.stride;
    const uint8_t *cr_row =
        frame->cr.data +
        static_cast<size_t>(chroma_y) * frame->cr.stride;
    const int chroma_width =
        (static_cast<int>(frame->width) + 1) >> 1;
    if (frame->storage_mode == PLM_FRAME_STORAGE_Y6_U5_V5) {
        plm_plane_unpack_compact_samples(
            &frame->y, 0, source_y, 6, native_y_row,
            static_cast<int>(frame->width));
        y_row = native_y_row;
    }

    if (chroma_y != mpeg_cached_chroma_y) {
        if (frame->storage_mode == PLM_FRAME_STORAGE_Y6_U5_V5) {
            plm_plane_unpack_compact_samples(
                &frame->cb, 0, chroma_y, 5,
                native_u_row, chroma_width);
            plm_plane_unpack_compact_samples(
                &frame->cr, 0, chroma_y, 5,
                native_v_row, chroma_width);
            cb_row = native_u_row;
            cr_row = native_v_row;
        }
        for (int chroma_x = 0; chroma_x < chroma_width; ++chroma_x) {
            const uint8_t cb = cb_row[chroma_x];
            const uint8_t cr = cr_row[chroma_x];
            mpeg_red_add[chroma_x] = yuv_red_add[cr];
            mpeg_green_add[chroma_x] =
                yuv_green_u_add[cb] + yuv_green_v_add[cr];
            mpeg_blue_add[chroma_x] = yuv_blue_add[cb];
        }
        mpeg_cached_chroma_y = chroma_y;
    }

    if (!scaled) {
        for (int source_x = 0; source_x < output_width; source_x += 2) {
            const int chroma_x = source_x >> 1;
            const int red_add = mpeg_red_add[chroma_x];
            const int green_add = mpeg_green_add[chroma_x];
            const int blue_add = mpeg_blue_add[chroma_x];
            output[source_x] = yuvToRgb565(
                y_row[source_x], red_add, green_add, blue_add);
            if (source_x + 1 < output_width) {
                output[source_x + 1] = yuvToRgb565(
                    y_row[source_x + 1], red_add, green_add, blue_add);
            }
        }
        return;
    }

    for (int destination_x = 0;
         destination_x < output_width; ++destination_x) {
        const int source_x = scaled_x_map[destination_x];
        const int chroma_x = source_x >> 1;
        output[destination_x] = yuvToRgb565(
            y_row[source_x], mpeg_red_add[chroma_x],
            mpeg_green_add[chroma_x], mpeg_blue_add[chroma_x]);
    }
}

bool renderMpegFrame(const plm_frame_t *frame) {
    if (!frame) return false;
    const int rows_per_transfer = display.rowsPerTransfer();
    mpeg_cached_chroma_y = -1;
    if (player_settings::kScaleVideoToDisplay) {
        int cached_source_y = -1;
        for (int y0 = 0; y0 < kScreenHeight;
             y0 += rows_per_transfer) {
            const int rows =
                std::min(rows_per_transfer, kScreenHeight - y0);
            uint16_t *pixels = display.acquireBuffer();
            if (!pixels) return false;
            for (int row = 0; row < rows; ++row) {
                const int source_y = scaled_y_map[y0 + row];
                if (source_y != cached_source_y) {
                    convertMpegRow(frame, source_y, true,
                                   scaled_rgb_row, kScreenWidth);
                    cached_source_y = source_y;
                }
                std::memcpy(
                    pixels + row * kScreenWidth, scaled_rgb_row,
                    sizeof(uint16_t) * kScreenWidth);
            }
            if (display.drawBitmap(
                    0, y0, kScreenWidth, rows, pixels) != ESP_OK) {
                return false;
            }
        }
        return true;
    }

    const int width = static_cast<int>(frame->width);
    const int height = static_cast<int>(frame->height);
    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    for (int y0 = 0; y0 < height; y0 += rows_per_transfer) {
        const int rows = std::min(rows_per_transfer, height - y0);
        uint16_t *pixels = display.acquireBuffer();
        if (!pixels) return false;
        for (int row = 0; row < rows; ++row) {
            convertMpegRow(frame, y0 + row, false,
                           pixels + row * width, width);
        }
        if (display.drawBitmap(
                x_offset, y_offset + y0, width, rows, pixels) != ESP_OK) {
            return false;
        }
    }
    return true;
}

bool renderH263Frame(const H2633gpFrame *frame) {
    if (!frame) return false;
    plm_frame_t adapted{};
    adapted.width = frame->width;
    adapted.height = frame->height;
    adapted.storage_mode = PLM_FRAME_STORAGE_YUV420;
    adapted.y = {
        frame->width, frame->height, frame->y_stride,
        const_cast<uint8_t *>(frame->y), 0, nullptr};
    adapted.cb = {
        static_cast<unsigned>(frame->width / 2),
        static_cast<unsigned>(frame->height / 2),
        frame->chroma_stride, const_cast<uint8_t *>(frame->u), 0, nullptr};
    adapted.cr = {
        static_cast<unsigned>(frame->width / 2),
        static_cast<unsigned>(frame->height / 2),
        frame->chroma_stride, const_cast<uint8_t *>(frame->v), 0, nullptr};
    return renderMpegFrame(&adapted);
}

struct MjpegRenderContext {
    uint32_t render_us = 0;
    int next_scaled_y = 0;
    bool display_failed = false;
};

bool renderMjpegStrip(void *opaque, const uint16_t *strip,
                      uint16_t source_y, uint16_t source_rows) {
    auto *context = static_cast<MjpegRenderContext *>(opaque);
    if (!context || !strip || !source_rows) return false;
    const int64_t render_start = microsNow();
    const int width = mjpeg_info.width;
    const int height = mjpeg_info.height;
    const int rows_per_transfer = display.rowsPerTransfer();
    const int source_end = source_y + source_rows;
    if (source_end > height) return false;

    if (player_settings::kScaleVideoToDisplay) {
        while (context->next_scaled_y < kScreenHeight &&
               scaled_y_map[context->next_scaled_y] < source_end) {
            const int destination_y = context->next_scaled_y;
            int rows = 0;
            while (rows < rows_per_transfer &&
                   destination_y + rows < kScreenHeight &&
                   scaled_y_map[destination_y + rows] < source_end) {
                if (scaled_y_map[destination_y + rows] < source_y)
                    return false;
                ++rows;
            }
            if (!rows) break;
            uint16_t *pixels = display.acquireBuffer();
            if (!pixels) {
                context->display_failed = true;
                return false;
            }
            for (int row = 0; row < rows; ++row) {
                const uint16_t *source =
                    strip +
                    (scaled_y_map[destination_y + row] - source_y) *
                        width;
                uint16_t *destination =
                    pixels + row * kScreenWidth;
                for (int x = 0; x < kScreenWidth; ++x) {
                    destination[x] = source[scaled_x_map[x]];
                }
            }
            if (display.drawBitmap(0, destination_y, kScreenWidth, rows,
                                   pixels) != ESP_OK) {
                context->display_failed = true;
                return false;
            }
            context->next_scaled_y += rows;
        }
        context->render_us +=
            static_cast<uint32_t>(microsNow() - render_start);
        return true;
    }

    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    uint16_t *pixels = display.acquireBuffer();
    if (!pixels) {
        context->display_failed = true;
        return false;
    }
    std::memcpy(pixels, strip,
                static_cast<size_t>(width) * source_rows *
                    sizeof(uint16_t));
    if (display.drawBitmap(x_offset, y_offset + source_y, width,
                           source_rows, pixels) != ESP_OK) {
        context->display_failed = true;
        return false;
    }
    context->render_us +=
        static_cast<uint32_t>(microsNow() - render_start);
    return true;
}

bool renderBpvFrame(const BPV1Frame *frame) {
    if (!frame) return false;
    const int width = frame->width;
    const int height = frame->height;
    const int rows_per_transfer = display.rowsPerTransfer();
    if (player_settings::kScaleVideoToDisplay) {
        int cached_source_y = -1;
        for (int y0 = 0; y0 < kScreenHeight; y0 += rows_per_transfer) {
            const int rows =
                std::min(rows_per_transfer, kScreenHeight - y0);
            uint16_t *pixels = display.acquireBuffer();
            if (!pixels) return false;
            for (int row = 0; row < rows; ++row) {
                const int source_y = scaled_y_map[y0 + row];
                if (source_y != cached_source_y) {
                    if (bpv1_frame_render_rgb565_row(
                            &bpv_header, frame,
                            static_cast<uint16_t>(source_y),
                            bpv_rgb_row, width) != BPV1_OK) {
                        return false;
                    }
                    cached_source_y = source_y;
                }
                uint16_t *destination =
                    pixels + row * kScreenWidth;
                for (int x = 0; x < kScreenWidth; ++x) {
                    destination[x] = bpv_rgb_row[scaled_x_map[x]];
                }
            }
            if (display.drawBitmap(0, y0, kScreenWidth, rows, pixels) !=
                ESP_OK) {
                return false;
            }
        }
        return true;
    }

    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    for (int y0 = 0; y0 < height; y0 += rows_per_transfer) {
        const int rows = std::min(rows_per_transfer, height - y0);
        uint16_t *pixels = display.acquireBuffer();
        if (!pixels) return false;
        if (bpv1_frame_render_rgb565_rows(
                &bpv_header, frame, static_cast<uint16_t>(y0),
                static_cast<uint16_t>(rows), pixels, width,
                static_cast<size_t>(width) * rows) != BPV1_OK) {
            return false;
        }
        if (display.drawBitmap(x_offset, y_offset + y0, width, rows,
                               pixels) != ESP_OK) {
            return false;
        }
    }
    return true;
}

void failPlayback(const char *title, int result) {
    const char *detail =
        video_codec == VideoCodec::kMpeg1
            ? "invalid, truncated or unsupported MPEG-1 stream"
            : video_codec == VideoCodec::kH263
            ? h263_3gp_strerror(result)
            : video_codec == VideoCodec::kMjpeg
            ? mjpeg_avi_strerror(result)
            : video_codec == VideoCodec::kBpv
                ? bpv1_strerror(result)
                : hlv1_strerror(result);
    ESP_LOGE(kTag, "%s: %s", title, detail);
    showStatus(title, detail);
    closeVideo();
    last_retry_ms = millisNow();
}

void failSdCardRead(const char *detail) {
    if (consecutive_sd_read_failures < UINT32_MAX) {
        ++consecutive_sd_read_failures;
    }
    const bool reinitialize =
        consecutive_sd_read_failures >= kSdReadFailuresBeforeReinit;
    ESP_LOGE(kTag, "SD card read failed (%u/%u): %s",
             static_cast<unsigned>(consecutive_sd_read_failures),
             static_cast<unsigned>(kSdReadFailuresBeforeReinit), detail);
    showStatus(reinitialize ? "SD CARD REINIT"
                            : "SD CARD READ ERROR",
               detail);
    if (reinitialize) {
        ESP_LOGE(kTag, "Reinitializing FAT, SDSPI and SPI3");
        deinitializeSdCard();
        consecutive_sd_read_failures = 0;
    } else {
        closeVideo();
    }
    last_retry_ms = millisNow();
}

void fallBackToTimerClock(const char *reason) {
    ESP_LOGW(kTag, "%s; switching to the ESP timer video clock", reason);
    stopAudio();
    next_present_us = microsNow();
    frame_period_phase = 0;
}

void advanceTimerDeadline() {
    next_present_us += frame_period_us;
    frame_period_phase += frame_period_remainder;
    if (frame_period_phase >= sequence_header.fps_num) {
        ++next_present_us;
        frame_period_phase -= sequence_header.fps_num;
    }
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

using RenderFunction = bool (*)(const void *);

bool renderHlvOpaque(const void *frame) {
    return renderFrame(static_cast<const HLV1Frame *>(frame));
}

bool renderBpvOpaque(const void *frame) {
    return renderBpvFrame(static_cast<const BPV1Frame *>(frame));
}

bool renderMpegOpaque(const void *frame) {
    return renderMpegFrame(static_cast<const plm_frame_t *>(frame));
}

bool renderH263Opaque(const void *frame) {
    return renderH263Frame(static_cast<const H2633gpFrame *>(frame));
}

struct PresentationState {
    int64_t start_us = 0;
    bool render = true;
};

PresentationState beginPresentation() {
    PresentationState state{microsNow(), true};
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
        const auto av_sync_mode = player_settings::kAvSyncMode;
        const bool loop_every_late_frame =
            av_sync_mode ==
            player_settings::AvSyncMode::kLoopAudioForLateVideo;
        const bool hybrid_sync =
            av_sync_mode ==
            player_settings::AvSyncMode::kDropThenLoopAudio;
        if (audio_output_failed || audio_reader_result < HLV1_OK) {
            fallBackToTimerClock("Audio clock stopped");
        } else {
            uint64_t estimated_position =
                static_cast<uint64_t>(audio_played_samples) +
                kAudioDmaSamples;
            const uint64_t latest_on_time_sample =
                target_samples + frame_samples;

            if (audio_loop_hold &&
                estimated_position <= latest_on_time_sample) {
                audio_loop_hold = false;
                consecutive_skipped_presentations = 0;
            }

            if (!audio_loop_hold &&
                !waitForAudioTarget(target_samples)) {
                fallBackToTimerClock("Audio clock stopped");
            } else if (audio_enabled && !audio_loop_hold) {
                estimated_position =
                    static_cast<uint64_t>(audio_played_samples) +
                    kAudioDmaSamples;
                if (estimated_position > latest_on_time_sample) {
                    if (loop_every_late_frame ||
                        (hybrid_sync &&
                         consecutive_skipped_presentations >=
                             player_settings::
                                 kMaxConsecutiveVideoSkips)) {
                        audio_loop_hold = true;
                        audio_loop_events = audio_loop_events + 1;
                        consecutive_skipped_presentations = 0;
                    } else {
                        state.render = false;
                        ++skipped_presentations;
                        ++consecutive_skipped_presentations;
                    }
                } else {
                    consecutive_skipped_presentations = 0;
                }
            }
        }
    }

    if (!audio_enabled) waitUntil(next_present_us);
    return state;
}

void finishPresentation(const PresentationState &state, uint32_t read_us,
                        uint32_t decode_us, uint32_t render_us) {
    ++decoded_frames;
    consecutive_sd_read_failures = 0;

    if (!audio_enabled) {
        advanceTimerDeadline();
        const int64_t lateness = microsNow() - next_present_us;
        if (lateness > frame_period_us) {
            ++dropped_deadlines;
            next_present_us = microsNow();
            frame_period_phase = 0;
        }
    }

    const uint32_t present_us =
        static_cast<uint32_t>(microsNow() - state.start_us);
    const uint32_t work_us = read_us + decode_us + render_us;
    if (player_settings::kLogFrameTimings) {
        // Capture every value before printing. UART overhead is therefore not
        // charged to this record, although it can consume slack before the
        // following frame.
        esp_rom_printf("F,%u,%u,%u,%u,%u,%u\n", decoded_frames, read_us,
                       decode_us, render_us, work_us, present_us);
        if (audio_enabled && decoded_frames % 30U == 0U) {
            esp_rom_printf(
                "A,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                decoded_frames,
                static_cast<unsigned>(
                    xStreamBufferBytesAvailable(audio_stream)),
                static_cast<unsigned>(audio_pending_samples),
                static_cast<unsigned>(audio_played_samples),
                static_cast<unsigned>(audio_rebuffers),
                static_cast<unsigned>(audio_underrun_samples),
                static_cast<unsigned>(audio_silence_chunks),
                static_cast<unsigned>(audio_loop_events),
                static_cast<unsigned>(audio_loop_chunks),
                static_cast<unsigned>(mpeg_audio_decode_frames),
                static_cast<unsigned>(mpeg_audio_decode_us),
                static_cast<unsigned>(mpeg_audio_convert_us));
        }
    }
}

bool presentDecodedFrame(const void *frame, RenderFunction render_function,
                         uint32_t read_us, uint32_t decode_us) {
    const PresentationState state = beginPresentation();
    uint32_t render_us = 0;
    if (state.render) {
        const int64_t render_start = microsNow();
        if (!render_function(frame)) return false;
        render_us = static_cast<uint32_t>(microsNow() - render_start);
    }
    finishPresentation(state, read_us, decode_us, render_us);
    return true;
}

bool presentFrame(const HLV1Frame *frame, uint32_t read_us,
                  uint32_t decode_us) {
    return presentDecodedFrame(frame, renderHlvOpaque, read_us, decode_us);
}

bool presentBpvFrame(const BPV1Frame *frame, uint32_t read_us,
                     uint32_t decode_us) {
    return presentDecodedFrame(frame, renderBpvOpaque, read_us, decode_us);
}

bool presentMpegFrame(const plm_frame_t *frame, uint32_t decode_us) {
    return presentDecodedFrame(
        frame, renderMpegOpaque, 0, decode_us);
}

bool presentH263Frame(const H2633gpFrame *frame, uint32_t decode_us) {
    return presentDecodedFrame(frame, renderH263Opaque, 0, decode_us);
}

uint32_t readPacket(HLV1Packet *packet, int *result) {
    const int64_t read_start = microsNow();
    *result = decoder.readPacket(video_file, packet);
    return static_cast<uint32_t>(microsNow() - read_start);
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
             "%u missing audio samples, %u silence chunks, "
             "%u audio loops (%u DMA chunks)",
             decoded_frames, dropped_deadlines, skipped_presentations,
             static_cast<unsigned>(audio_rebuffers),
             static_cast<unsigned>(audio_underrun_samples),
             static_cast<unsigned>(audio_silence_chunks),
             static_cast<unsigned>(audio_loop_events),
             static_cast<unsigned>(audio_loop_chunks));
    if (!openVideo()) last_retry_ms = millisNow();
}

void playOneFrameSequential() {
    HLV1Packet packet{};
    int packet_result = HLV1_OK;
    const uint32_t read_us = readPacket(&packet, &packet_result);
    if (packet_result == HLV1_EOF) {
        finishVideoLoop();
        return;
    }
    if (packet_result != HLV1_OK) {
        if (packet_result == HLV1_ERR_IO) {
            failSdCardRead("cannot read HLV video");
            return;
        }
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

    if (!presentFrame(frame, read_us, decode_us)) {
        failPlayback("Display DMA error", HLV1_ERR_IO);
    }
}

void playOneMpegFrameSequential() {
    const int64_t decode_start = microsNow();
    plm_frame_t *frame = plm_decode_video(mpeg_video);
    const uint32_t decode_us =
        static_cast<uint32_t>(microsNow() - decode_start);
    if (!frame) {
        if (video_file && std::ferror(video_file)) {
            failSdCardRead("cannot read MPEG video");
            return;
        }
        finishVideoLoop();
        return;
    }
    if (!presentMpegFrame(frame, decode_us)) {
        failPlayback("Display DMA error", HLV1_ERR_IO);
    }
}

void playOneH263Frame() {
    H2633gpFrame frame{};
    const int64_t decode_start = microsNow();
    const int result =
        h263_3gp_decoder_decode_next(h263_decoder, video_file, &frame);
    const uint32_t decode_us =
        static_cast<uint32_t>(microsNow() - decode_start);
    if (result == H263_3GP_EOF) {
        finishVideoLoop();
        return;
    }
    if (result == H263_3GP_ERR_IO) {
        failSdCardRead("cannot read H.263 video");
        return;
    }
    if (result != H263_3GP_OK) {
        failPlayback("H.263 decode error", result);
        return;
    }
    if (!presentH263Frame(&frame, decode_us)) {
        failPlayback("Display DMA error", H263_3GP_ERR_IO);
    }
}

void playOneH263FramePipelined() {
    if (!pending_h263_frame_valid) {
        if (!submitH263Decode()) {
            failPlayback("H.263 pipeline error", H263_3GP_ERR_IO);
            return;
        }
        DecodeResult first{};
        if (!waitDecode(&first) || first.codec != VideoCodec::kH263) {
            failPlayback("H.263 pipeline error", H263_3GP_ERR_DECODE);
            return;
        }
        if (first.result == H263_3GP_EOF) {
            finishVideoLoop();
            return;
        }
        if (first.result == H263_3GP_ERR_IO) {
            failSdCardRead("cannot read H.263 video");
            return;
        }
        if (first.result != H263_3GP_OK) {
            failPlayback("H.263 decode error", first.result);
            return;
        }
        pending_h263_frame = first.h263_frame;
        pending_h263_decode_us = first.decode_us;
        pending_h263_frame_valid = true;
    }

    const H2633gpFrame frame = pending_h263_frame;
    const uint32_t decode_us = pending_h263_decode_us;
    pending_h263_frame_valid = false;
    if (!submitH263Decode()) {
        failPlayback("H.263 pipeline error", H263_3GP_ERR_IO);
        return;
    }

    const bool rendered = presentH263Frame(&frame, decode_us);
    DecodeResult next{};
    const bool received = waitDecode(&next);
    if (!rendered) {
        failPlayback("Display DMA error", H263_3GP_ERR_IO);
        return;
    }
    if (!received || next.codec != VideoCodec::kH263) {
        failPlayback("H.263 pipeline error", H263_3GP_ERR_DECODE);
        return;
    }
    if (next.result == H263_3GP_ERR_IO) {
        failSdCardRead("cannot read H.263 video");
        return;
    }
    if (next.result != H263_3GP_OK &&
        next.result != H263_3GP_EOF) {
        failPlayback("H.263 decode error", next.result);
        return;
    }
    if (next.result == H263_3GP_OK) {
        pending_h263_frame = next.h263_frame;
        pending_h263_decode_us = next.decode_us;
        pending_h263_frame_valid = true;
    } else {
        finishVideoLoop();
    }
}

void playOneMpegFramePipelined() {
    if (!pending_mpeg_frame_valid) {
        if (!submitMpegDecode()) {
            failPlayback("Decode pipeline error", HLV1_ERR_IO);
            return;
        }
        DecodeResult first{};
        if (!waitDecode(&first)) {
            failPlayback("MPEG-1 decode error", HLV1_ERR_BITSTREAM);
            return;
        }
        if (first.result == HLV1_ERR_IO) {
            failSdCardRead("cannot read MPEG video");
            return;
        }
        if (first.result != HLV1_OK) {
            failPlayback("MPEG-1 decode error", first.result);
            return;
        }
        if (!first.has_mpeg_frame) {
            finishVideoLoop();
            return;
        }
        pending_mpeg_frame = first.mpeg_frame;
        pending_mpeg_decode_us = first.decode_us;
        pending_mpeg_frame_valid = true;
    }

    const plm_frame_t frame = pending_mpeg_frame;
    const uint32_t decode_us = pending_mpeg_decode_us;
    pending_mpeg_frame_valid = false;
    if (!submitMpegDecode()) {
        failPlayback("Decode pipeline error", HLV1_ERR_IO);
        return;
    }

    const bool rendered = presentMpegFrame(&frame, decode_us);
    DecodeResult next{};
    const bool received = waitDecode(&next);
    if (!rendered) {
        failPlayback("Display DMA error", HLV1_ERR_IO);
        return;
    }
    if (!received) {
        failPlayback("MPEG-1 decode error", HLV1_ERR_BITSTREAM);
        return;
    }
    if (next.result == HLV1_ERR_IO) {
        failSdCardRead("cannot read MPEG video");
        return;
    }
    if (next.result != HLV1_OK) {
        failPlayback("MPEG-1 decode error", next.result);
        return;
    }
    if (next.has_mpeg_frame) {
        pending_mpeg_frame = next.mpeg_frame;
        pending_mpeg_decode_us = next.decode_us;
        pending_mpeg_frame_valid = true;
    } else {
        finishVideoLoop();
    }
}

void playOneMjpegFrame() {
    MjpegAviPacket packet{};
    const int64_t read_start = microsNow();
    int packet_result =
        mjpeg_decoder.readPacket(video_file, &packet);
    if (packet_result == MJPEG_AVI_ERR_IO) {
        const long retry_offset = mjpeg_decoder.lastPacketOffset();
        for (unsigned attempt = 1; attempt <= 2; ++attempt) {
            ESP_LOGW(kTag,
                     "Recovering MJPEG packet at %ld, attempt %u/2",
                     retry_offset, attempt);
            if (!reopenVideoAt(retry_offset)) break;
            packet_result =
                mjpeg_decoder.readPacket(video_file, &packet);
            if (packet_result == MJPEG_AVI_OK) {
                ESP_LOGI(kTag, "MJPEG packet recovered at %ld",
                         retry_offset);
                break;
            }
            if (packet_result != MJPEG_AVI_ERR_IO) break;
        }
    }
    const uint32_t read_us =
        static_cast<uint32_t>(microsNow() - read_start);
    if (packet_result == MJPEG_AVI_EOF) {
        finishVideoLoop();
        return;
    }
    if (packet_result != MJPEG_AVI_OK) {
        if (packet_result == MJPEG_AVI_ERR_IO) {
            failSdCardRead("cannot read MJPEG video");
            return;
        }
        failPlayback("MJPEG packet error", packet_result);
        return;
    }

    const PresentationState presentation = beginPresentation();
    uint32_t decode_us = 0;
    uint32_t render_us = 0;
    if (presentation.render) {
        MjpegRenderContext render_context{};
        const int64_t decode_start = microsNow();
        const int decode_result = mjpeg_decoder.decode(
            packet, renderMjpegStrip, &render_context);
        const uint32_t combined_us =
            static_cast<uint32_t>(microsNow() - decode_start);
        render_us = render_context.render_us;
        decode_us = combined_us > render_us
                        ? combined_us - render_us
                        : 0;
        if (decode_result != MJPEG_AVI_OK) {
            failPlayback(render_context.display_failed
                             ? "Display DMA error"
                             : "JPEG decode error",
                         decode_result);
            return;
        }
        if (player_settings::kScaleVideoToDisplay &&
            render_context.next_scaled_y != kScreenHeight) {
            failPlayback("JPEG output error", MJPEG_AVI_ERR_DECODE);
            return;
        }
    }
    finishPresentation(presentation, read_us, decode_us, render_us);
}

void playOneBpvFrameSequential() {
    BPV1Packet packet{};
    const int64_t read_start = microsNow();
    const int packet_result =
        bpv_decoder.readPacket(video_file, &packet);
    const uint32_t read_us =
        static_cast<uint32_t>(microsNow() - read_start);
    if (packet_result == BPV1_EOF) {
        finishVideoLoop();
        return;
    }
    if (packet_result != BPV1_OK) {
        if (packet_result == BPV1_ERR_IO) {
            failSdCardRead("cannot read BPV1 video");
            return;
        }
        failPlayback("BPV1 packet error", packet_result);
        return;
    }

    const BPV1Frame *frame = nullptr;
    const int64_t decode_start = microsNow();
    const int decode_result = bpv_decoder.decode(&packet, &frame);
    const uint32_t decode_us =
        static_cast<uint32_t>(microsNow() - decode_start);
    if (decode_result != BPV1_OK) {
        failPlayback("BPV1 decode error", decode_result);
        return;
    }
    if (!presentBpvFrame(frame, read_us, decode_us)) {
        failPlayback("Display DMA error", BPV1_ERR_IO);
    }
}

void playOneBpvFramePipelined() {
    if (bpv_stream_eof && !ready_bpv_packet_valid) {
        if (pending_bpv_frame_valid) {
            const bool rendered =
                presentBpvFrame(&pending_bpv_frame, pending_read_us,
                                pending_decode_us);
            pending_bpv_frame_valid = false;
            if (!rendered) {
                failPlayback("Display DMA error", BPV1_ERR_IO);
                return;
            }
        }
        finishVideoLoop();
        return;
    }

    if (!ready_bpv_packet_valid) {
        const int64_t read_start = microsNow();
        const int packet_result =
            bpv_decoder.readPacket(video_file, &ready_bpv_packet);
        ready_bpv_read_us =
            static_cast<uint32_t>(microsNow() - read_start);
        if (packet_result == BPV1_EOF) {
            bpv_stream_eof = true;
            return;
        }
        if (packet_result != BPV1_OK) {
            if (packet_result == BPV1_ERR_IO) {
                failSdCardRead("cannot read BPV1 video");
                return;
            }
            failPlayback("BPV1 packet error", packet_result);
            return;
        }
        ready_bpv_packet_valid = true;
    }

    BPV1Packet packet = ready_bpv_packet;
    const uint32_t read_us = ready_bpv_read_us;
    ready_bpv_packet_valid = false;

    bool rendered = true;
    // BPV v4/v5 replaces the active palette at every keyframe. Finish using the
    // preceding palette before CPU1 starts changing it. Between keyframes the
    // decoder's two block arrays already provide safe zero-copy ping-pong
    // storage: CPU0 reads the previous array while CPU1 writes the current one.
    if (pending_bpv_frame_valid && packet.info.keyframe &&
        bpv_header.version >= BPV1_ACTIVE_PALETTE_VERSION) {
        rendered =
            presentBpvFrame(&pending_bpv_frame, pending_read_us,
                            pending_decode_us);
        pending_bpv_frame_valid = false;
    }

    if (rendered &&
        !submitBpvDecode(&packet, video_file, true)) {
        failPlayback("BPV1 decode pipeline error", BPV1_ERR_IO);
        return;
    }

    if (rendered && pending_bpv_frame_valid) {
        rendered =
            presentBpvFrame(&pending_bpv_frame, pending_read_us,
                            pending_decode_us);
        pending_bpv_frame_valid = false;
    }

    DecodeResult result{};
    const bool received = rendered && waitDecode(&result);
    if (!rendered) {
        if (decode_in_flight) {
            DecodeResult ignored{};
            waitDecode(&ignored);
        }
        failPlayback("Display DMA error", BPV1_ERR_IO);
        return;
    }
    if (!received) {
        failPlayback("BPV1 decode pipeline error", BPV1_ERR_IO);
        return;
    }
    if (result.codec != VideoCodec::kBpv ||
        result.result != BPV1_OK || !result.bpv_frame) {
        failPlayback("BPV1 decode error", result.result);
        return;
    }
    pending_bpv_frame = *result.bpv_frame;
    pending_read_us = read_us;
    pending_decode_us = result.decode_us;
    pending_bpv_frame_valid = true;

    if (result.bpv_read_result == BPV1_OK) {
        ready_bpv_packet = result.bpv_next_packet;
        ready_bpv_read_us = result.bpv_read_us;
        ready_bpv_packet_valid = true;
    } else if (result.bpv_read_result == BPV1_EOF) {
        bpv_stream_eof = true;
    } else if (result.bpv_read_result == BPV1_ERR_IO) {
        failSdCardRead("cannot read BPV1 video");
    } else {
        failPlayback("BPV1 packet error", result.bpv_read_result);
    }
}

void playOneFramePipelined() {
    HLV1Packet packet{};
    int packet_result = HLV1_OK;
    const uint32_t read_us = readPacket(&packet, &packet_result);
    if (packet_result == HLV1_EOF) {
        if (pending_frame_valid) {
            const bool rendered = presentFrame(
                &pending_frame, pending_read_us, pending_decode_us);
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
        if (packet_result == HLV1_ERR_IO) {
            failSdCardRead("cannot read HLV video");
            return;
        }
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
        rendered = presentFrame(&pending_frame, pending_read_us,
                                pending_decode_us);
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
    if (result.codec != VideoCodec::kHlv ||
        result.result != HLV1_OK || !result.hlv_frame) {
        failPlayback("Decode error", result.result);
        return;
    }
    pending_frame = *result.hlv_frame;
    pending_read_us = read_us;
    pending_decode_us = result.decode_us;
    pending_frame_valid = true;
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "Multi-codec ESP-IDF SD player starting");
    const esp_err_t display_result = display.init();
    if (display_result != ESP_OK) {
        ESP_LOGE(kTag, "Display initialization failed: %s",
                 esp_err_to_name(display_result));
        return;
    }
    const esp_err_t uart_result =
        uart_upload.begin(CONFIG_ESP_CONSOLE_UART_BAUDRATE);
    if (uart_result != ESP_OK) {
        ESP_LOGE(kTag, "UART upload initialization failed: %s",
                 esp_err_to_name(uart_result));
    }
    showStatus("Multi-codec SD player", "mounting microSD");

    if (!mountSdCard()) {
        showStatus("microSD failed", "insert a FAT32 card and reset");
        last_retry_ms = millisNow();
    } else if (!openVideo()) {
        last_retry_ms = millisNow();
    }

    for (;;) {
        UartUploadRequest upload_request{};
        if (uart_upload.pollRequest(&upload_request)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_upload.reject("NO_SD");
                last_retry_ms = millisNow();
                continue;
            }
            closeVideo();
            beginUploadProgress();
            char stored_path[128]{};
            const bool stored = uart_upload.receive(
                upload_request, player_settings::kVideoDirectory,
                stored_path, sizeof stored_path, updateUploadProgress,
                nullptr);
            display.flush();
            showStatus(stored ? "UART upload complete"
                              : "UART upload failed",
                       upload_request.filename);
            if (!openVideo()) last_retry_ms = millisNow();
            continue;
        }
        if (uart_upload.takeListRequest()) {
            if (!sd_mounted && !mountSdCard()) {
                uart_upload.reject("NO_SD");
                last_retry_ms = millisNow();
            } else {
                uart_upload.listDirectory(
                    player_settings::kVideoDirectory);
            }
            continue;
        }
        char crc_filename[UartUploadRequest::kMaximumFilenameBytes + 1]{};
        if (uart_upload.takeCrcRequest(
                crc_filename, sizeof crc_filename)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_upload.reject("NO_SD");
                last_retry_ms = millisNow();
            } else {
                closeVideo();
                uart_upload.checksumFile(
                    player_settings::kVideoDirectory, crc_filename);
                if (!openVideo()) last_retry_ms = millisNow();
            }
            continue;
        }
        if (video_file && video_codec == VideoCodec::kMpeg1 &&
            mpeg_video) {
            if (player_settings::kUseDualCorePipeline) {
                playOneMpegFramePipelined();
            } else {
                playOneMpegFrameSequential();
            }
            continue;
        }
        if (video_file && video_codec == VideoCodec::kH263 &&
            h263_decoder) {
            if (player_settings::kUseDualCorePipeline &&
                h263_dual_buffered) {
                playOneH263FramePipelined();
            } else {
                playOneH263Frame();
            }
            continue;
        }
        if (video_file && video_codec == VideoCodec::kMjpeg &&
            mjpeg_decoder.ready()) {
            playOneMjpegFrame();
            continue;
        }
        if (video_file && video_codec == VideoCodec::kBpv &&
            bpv_decoder.ready()) {
            if (player_settings::kUseDualCorePipeline) {
                playOneBpvFramePipelined();
            } else {
                playOneBpvFrameSequential();
            }
            continue;
        }
        if (video_file && video_codec == VideoCodec::kHlv &&
            decoder.ready()) {
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
