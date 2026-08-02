#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "driver/dac_continuous.h"
#include "driver/gpio.h"
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
#include "boot_button.h"
#include "cyd_display.hpp"
#include "divx3.h"
#include "divx3_avi.h"
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
constexpr int kMaximumH263Width = 352;
constexpr int kH263CifWidth = 352;
constexpr int kH263CifHeight = 288;
constexpr int kH263CifVisibleY = 16;
constexpr uint32_t kRetryDelayMs = 2000;
constexpr uint32_t kSdReadFailuresBeforeReinit = 3;
constexpr size_t kVideoReadAheadBytes = 16 * 1024;
constexpr size_t kMpegVideoReadAheadBytes = 4 * 1024;
constexpr size_t kDivx3VideoReadAheadBytes = 4 * 1024;
constexpr size_t kDivx3MaximumPacketBytes = 96 * 1024;
constexpr uint32_t kDivx3MaximumMacroblocks = 300;
constexpr size_t kDivx3CompactLumaPlaneBytes = 57'600;
constexpr size_t kH263VideoReadAheadBytes = 4 * 1024;
constexpr size_t kBpvVideoReadAheadBytes = 4 * 1024;
// This capacity is deliberately unrelated to BPV's maximum encoded frame.
// The decoder consumes sequential spans while CPU1 keeps the ring refilled.
constexpr size_t kBpvInputRingBytes = 16 * 1024;
constexpr size_t kBpvInputChunkBytes = 4 * 1024;
constexpr uint32_t kBpvInputReaderStackBytes = 4096;
constexpr uint32_t kBpvInputStopTimeoutMs = 500;
constexpr uint32_t kBpvInputPrerollTimeoutMs = 1000;
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
constexpr uint32_t kBootButtonTaskStackBytes = 2048;
constexpr size_t kBrowserFilenameBytes = 112;
constexpr size_t kBrowserVisibleFiles = 5;
constexpr int kUploadBarX = 16;
constexpr int kUploadBarWidth = kScreenWidth - 2 * kUploadBarX;
constexpr int kUploadBarHeight = CydDisplay::kRowsPerTransfer;
constexpr int kUploadBarY = (kScreenHeight - kUploadBarHeight) / 2;
constexpr int kUploadBarBorder = 2;
constexpr uint16_t kUploadBarBorderColor = 0xffff;
constexpr uint16_t kUploadBarEmptyColor = 0x2104;
constexpr uint16_t kUploadBarFillColor = 0x07e0;
constexpr int kUploadPercentScale = 2;
constexpr int kUploadPercentFallbackScale = 1;
constexpr int kUploadFilenameScale = 1;
constexpr int kUploadFilenameY =
    kUploadBarY + kUploadBarHeight + 12;
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
    kDivx3,
    kBpv,
    kMpeg1,
    kH263,
    kMpeg4Simple,
};

bool isPacketVideoCodec(VideoCodec codec) {
    return codec == VideoCodec::kH263 ||
           codec == VideoCodec::kMpeg4Simple;
}

int packetVideoLibraryCodec(VideoCodec codec) {
    return codec == VideoCodec::kMpeg4Simple
               ? H263_VIDEO_CODEC_MPEG4_SIMPLE
               : H263_VIDEO_CODEC_H263;
}

const char *packetVideoCodecName(VideoCodec codec) {
    return codec == VideoCodec::kMpeg4Simple ? "MPEG-4 SP" : "H.263";
}

const char *packetVideoReadError(VideoCodec codec) {
    return codec == VideoCodec::kMpeg4Simple
               ? "cannot read MPEG-4 SP video"
               : "cannot read H.263 video";
}

const char *packetVideoDecodeErrorTitle(VideoCodec codec) {
    return codec == VideoCodec::kMpeg4Simple
               ? "MPEG-4 decode error"
               : "H.263 decode error";
}

const char *packetVideoPipelineErrorTitle(VideoCodec codec) {
    return codec == VideoCodec::kMpeg4Simple
               ? "MPEG-4 pipeline error"
               : "H.263 pipeline error";
}

enum class SelectionReadResult {
    kReady,
    kMissingOrInvalid,
    kIoError,
};

enum class BrowserScanResult {
    kFound,
    kEmpty,
    kIoError,
};

enum class VideoOpenResult {
    kReady,
    kMissingOrUnsupported,
    kIoError,
};

struct DecodeRequest {
    VideoCodec codec;
    FILE *hlv_file;
    const BPV1Packet *bpv_packet;
    FILE *divx3_file;
    uint32_t divx3_packet_size;
    long divx3_next_offset;
    FILE *bpv_file;
    bool bpv_prefetch;
};

struct DecodeResult {
    VideoCodec codec;
    int result;
    const HLV1Frame *hlv_frame;
    const BPV1Frame *bpv_frame;
    Divx3Frame divx3_frame;
    H2633gpFrame h263_frame;
    plm_frame_t mpeg_frame;
    bool has_mpeg_frame;
    uint32_t decode_us;
#if HLV1_ENABLE_STAGE_PROFILE
    HLV1StageProfile hlv_profile;
    uint32_t hlv_row_guard_wait_us;
#endif
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
Divx3Decoder *divx3_decoder = nullptr;
Divx3AviInfo divx3_info{};
BpvEsp32Decoder bpv_decoder;
BPV1Header bpv_header{};
uint8_t bpv_file_version = 0;
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
char browser_filename[kBrowserFilenameBytes]{};
char browser_visible_filenames[kBrowserVisibleFiles]
                              [kBrowserFilenameBytes]{};
size_t browser_visible_count = 0;
size_t browser_selected_visible_index = 0;
bool file_browser_active = false;
QueueHandle_t boot_button_event_queue = nullptr;
TaskHandle_t boot_button_task_handle = nullptr;
boot_button_state_t cooperative_boot_button_state{};
uint32_t cooperative_boot_button_next_poll_ms = 0;
int64_t frame_period_us = 0;
uint32_t frame_period_remainder = 0;
uint32_t frame_period_phase = 0;
int64_t next_present_us = 0;
uint32_t decoded_frames = 0;
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
uint32_t bare_benchmark_frames = 0;
uint64_t bare_benchmark_read_us = 0;
uint64_t bare_benchmark_decode_us = 0;
uint64_t bare_benchmark_render_us = 0;
#endif
bool seek_fast_forward = false;
uint32_t seek_target_frame = 0;
uint32_t seek_requested_ms = 0;
uint64_t seek_discarded_audio_samples = 0;
uint32_t dropped_deadlines = 0;
int64_t last_retry_ms = 0;
uint16_t scaled_rgb_row[kScreenWidth];
uint16_t bpv_rgb_row[kScreenWidth];
uint16_t bpv_rgb565_palette[BPV1_MAX_PALETTE_COLORS];
bool bpv_rgb565_palette_valid = false;
uint16_t scaled_x_map[kScreenWidth];
uint16_t scaled_y_map[kScreenHeight];
uint8_t native_y_row[kScreenWidth];
uint8_t native_u_row[kScreenWidth / 2];
uint8_t native_v_row[kScreenWidth / 2];
int hlv_cached_chroma_y = -1;
int32_t mpeg_red_add[kMaximumH263Width / 2];
int32_t mpeg_green_add[kMaximumH263Width / 2];
int32_t mpeg_blue_add[kMaximumH263Width / 2];
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
StreamBufferHandle_t bpv_input_stream = nullptr;
TaskHandle_t bpv_input_reader_task_handle = nullptr;
uint8_t *bpv_input_chunk = nullptr;
volatile bool bpv_input_stop_requested = false;
volatile bool bpv_input_reader_done = true;
volatile int bpv_input_reader_result = BPV1_OK;
HLV1Frame pending_frame{};
bool pending_frame_valid = false;
plm_frame_t pending_mpeg_frame{};
bool pending_mpeg_frame_valid = false;
uint32_t pending_mpeg_decode_us = 0;
Divx3Frame pending_divx3_frame{};
bool pending_divx3_frame_valid = false;
H2633gpFrame pending_h263_frame{};
bool pending_h263_frame_valid = false;
uint32_t pending_h263_decode_us = 0;
bool h263_dual_buffered = false;
bool h263_row_pipelined = false;
int h263_rendered_source_rows = INT_MAX;
int h263_row_pipeline_active = 0;
uint32_t h263_row_guard_wait_us = 0;
int hlv_rendered_source_rows = INT_MAX;
int hlv_row_pipeline_active = 0;
uint32_t hlv_row_guard_wait_us = 0;
BPV1Frame pending_bpv_frame{};
bool pending_bpv_frame_valid = false;
BPV1Packet ready_bpv_packet{};
bool ready_bpv_packet_valid = false;
bool bpv_stream_eof = false;
uint32_t ready_bpv_read_us = 0;
uint32_t pending_read_us = 0;
uint32_t pending_decode_us = 0;
long pending_hlv_packet_offset = -1;
uint32_t skipped_presentations = 0;
uint32_t consecutive_skipped_presentations = 0;
int upload_progress_pixels = -1;
int upload_progress_percent = -1;
int upload_progress_scale = kUploadPercentScale;
bool uart_diagnostics_quiet = false;

void quietUartDiagnostics() {
    if (uart_diagnostics_quiet) return;
    esp_log_level_set("*", ESP_LOG_NONE);
    uart_diagnostics_quiet = true;
}

void restoreUartDiagnostics() {
    if (!uart_diagnostics_quiet) return;
    esp_log_level_set(
        "*", static_cast<esp_log_level_t>(CONFIG_LOG_DEFAULT_LEVEL));
    uart_diagnostics_quiet = false;
}

int64_t microsNow() { return esp_timer_get_time(); }

uint64_t bpvProfileNowMicros(void *) {
    return static_cast<uint64_t>(microsNow());
}

int64_t millisNow() { return microsNow() / 1000; }

void bootButtonTask(void *) {
    boot_button_state_t state{};
    boot_button_state_init(
        &state, gpio_get_level(board::kBootButton) == 0,
        static_cast<uint32_t>(millisNow()),
        player_settings::kBootButtonDebounceMs,
        player_settings::kBootButtonLongPressMs);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(player_settings::kBootButtonPollMs));
        const boot_button_event_t event = boot_button_state_update(
            &state, gpio_get_level(board::kBootButton) == 0,
            static_cast<uint32_t>(millisNow()));
        if (event != BOOT_BUTTON_EVENT_NONE) {
            (void)xQueueSend(boot_button_event_queue, &event, 0);
        }
    }
}

bool initializeBootButton() {
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << board::kBootButton;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) return false;

    if (!player_settings::kUseBootButtonTask) {
        const uint32_t now_ms = static_cast<uint32_t>(millisNow());
        boot_button_state_init(
            &cooperative_boot_button_state,
            gpio_get_level(board::kBootButton) == 0, now_ms,
            player_settings::kBootButtonDebounceMs,
            player_settings::kBootButtonLongPressMs);
        cooperative_boot_button_next_poll_ms =
            now_ms + player_settings::kBootButtonPollMs;
        return true;
    }

    boot_button_event_queue =
        xQueueCreate(4, sizeof(boot_button_event_t));
    if (!boot_button_event_queue) return false;
    if (xTaskCreatePinnedToCore(
            bootButtonTask, "boot-button", kBootButtonTaskStackBytes,
            nullptr, tskIDLE_PRIORITY + 1, &boot_button_task_handle, 0) !=
        pdPASS) {
        vQueueDelete(boot_button_event_queue);
        boot_button_event_queue = nullptr;
        return false;
    }
    return true;
}

int h263VisibleSourceY(int source_width, int source_height) {
    if (source_width == kH263CifWidth &&
        source_height == kH263CifHeight) {
        return kH263CifVisibleY;
    }
    return (
        source_height - std::min(source_height, kScreenHeight)) / 2;
}

void waitForH263OutputRow(void *, uint16_t first_y) {
    if (!__atomic_load_n(&h263_row_pipeline_active, __ATOMIC_ACQUIRE))
        return;
    const int source_height = sequence_header.height;
    const int visible_height = std::min(source_height, kScreenHeight);
    const int first_visible_y = h263VisibleSourceY(
        sequence_header.width, source_height);
    const int visible_end_y = first_visible_y + visible_height;
    const int row_end_y = std::min<int>(first_y + 16, visible_end_y);
    if (row_end_y <= first_visible_y || first_y >= visible_end_y)
        return;

    const int64_t wait_start = microsNow();
    while (__atomic_load_n(&h263_row_pipeline_active, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&h263_rendered_source_rows, __ATOMIC_ACQUIRE) <
               row_end_y) {
        taskYIELD();
    }
    __atomic_fetch_add(
        &h263_row_guard_wait_us,
        static_cast<uint32_t>(microsNow() - wait_start),
        __ATOMIC_RELAXED);
}

void beginH263RowPipeline() {
    const int source_height = sequence_header.height;
    __atomic_store_n(&h263_row_guard_wait_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(
        &h263_rendered_source_rows,
        h263VisibleSourceY(sequence_header.width, source_height),
        __ATOMIC_RELEASE);
    __atomic_store_n(&h263_row_pipeline_active, 1, __ATOMIC_RELEASE);
}

void publishH263RenderedRows(int source_rows) {
    if (__atomic_load_n(&h263_row_pipeline_active, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(
            &h263_rendered_source_rows, source_rows, __ATOMIC_RELEASE);
    }
}

void endH263RowPipeline() {
    __atomic_store_n(&h263_rendered_source_rows, INT_MAX, __ATOMIC_RELEASE);
    __atomic_store_n(&h263_row_pipeline_active, 0, __ATOMIC_RELEASE);
}

size_t readDivx3Stream(void *context, uint8_t *buffer, size_t capacity) {
    FILE *file = static_cast<FILE *>(context);
    return file && buffer && capacity
               ? std::fread(buffer, 1, capacity, file)
               : 0;
}

void waitForHlvReferenceRows(void *, int first_y, int rows) {
    if (!__atomic_load_n(&hlv_row_pipeline_active, __ATOMIC_ACQUIRE))
        return;
    const int row_end_y =
        std::min(first_y + rows, static_cast<int>(sequence_header.height));
    if (first_y >= row_end_y) return;
    const int64_t wait_start = microsNow();
    while (__atomic_load_n(&hlv_row_pipeline_active, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(
               &hlv_rendered_source_rows, __ATOMIC_ACQUIRE) < row_end_y) {
        taskYIELD();
    }
    __atomic_fetch_add(
        &hlv_row_guard_wait_us,
        static_cast<uint32_t>(microsNow() - wait_start),
        __ATOMIC_RELAXED);
}

void beginHlvRowPipeline() {
    __atomic_store_n(&hlv_row_guard_wait_us, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&hlv_rendered_source_rows, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&hlv_row_pipeline_active, 1, __ATOMIC_RELEASE);
}

void publishHlvRenderedRows(int source_rows) {
    if (__atomic_load_n(&hlv_row_pipeline_active, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(
            &hlv_rendered_source_rows, source_rows, __ATOMIC_RELEASE);
    }
}

void endHlvRowPipeline() {
    __atomic_store_n(&hlv_rendered_source_rows, INT_MAX, __ATOMIC_RELEASE);
    __atomic_store_n(&hlv_row_pipeline_active, 0, __ATOMIC_RELEASE);
}

void decodeTask(void *) {
    DecodeRequest request{};
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    esp_rom_printf("HLVBARE 1 DECODER_CPU %d\n", xPortGetCoreID());
#endif
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
            result.result = decoder.decodeNext(
                request.hlv_file, &result.hlv_frame, nullptr,
#if HLV1_ENABLE_STAGE_PROFILE
                &result.hlv_profile);
#else
                nullptr);
#endif
        } else if (request.codec == VideoCodec::kBpv) {
            result.result =
                bpv_decoder.decode(request.bpv_packet, &result.bpv_frame);
        } else if (request.codec == VideoCodec::kDivx3) {
            result.result =
                divx3_decoder && request.divx3_file &&
                        request.divx3_packet_size &&
                        request.divx3_next_offset >= 0
                    ? divx3_decoder_decode_stream(
                          divx3_decoder, request.divx3_packet_size,
                          readDivx3Stream, request.divx3_file,
                          &result.divx3_frame)
                    : DIVX3_ERR_ARGUMENT;
            if (request.divx3_file &&
                request.divx3_next_offset >= 0 &&
                divx3_avi_finish_video_packet(
                    request.divx3_file,
                    request.divx3_next_offset) != DIVX3_AVI_OK &&
                result.result == DIVX3_OK) {
                result.result = DIVX3_ERR_BITSTREAM;
            }
        } else if (isPacketVideoCodec(request.codec)) {
            result.result =
                h263_decoder && video_file
                    ? h263_3gp_decoder_decode_next(
                          h263_decoder, video_file, &result.h263_frame)
                    : H263_3GP_ERR_ARGUMENT;
        } else {
            result.result = HLV1_ERR_ARGUMENT;
        }
        result.decode_us = static_cast<uint32_t>(microsNow() - start);
#if HLV1_ENABLE_STAGE_PROFILE
        if (request.codec == VideoCodec::kHlv) {
            result.hlv_row_guard_wait_us = __atomic_load_n(
                &hlv_row_guard_wait_us, __ATOMIC_RELAXED);
            if (result.hlv_profile.frames) {
                esp_rom_printf(
                    "H,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u\n",
                    result.hlv_profile.total_cycles,
                    result.hlv_profile.input_cycles,
                    result.hlv_profile.input_bytes,
                    result.hlv_profile.input_refills,
                    result.hlv_profile.crc_cycles,
                    result.hlv_profile.prediction_cycles,
                    result.hlv_profile.residual_cycles,
                    result.hlv_profile.inverse_wht_cycles,
                    result.hlv_profile.packing_cycles,
                    result.hlv_profile.reference_commit_cycles,
                    result.hlv_row_guard_wait_us);
            }
        }
#endif
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
             "CPU1 ordered decode/BPV prefetch, "
             "H.263 ping-pong/row pipeline");
    return true;
}

bool submitDecode(FILE *file) {
    if (!decode_task_handle || decode_in_flight || !file) return false;
    pending_hlv_packet_offset = ftell(file);
    DecodeRequest request{};
    request.codec = VideoCodec::kHlv;
    request.hlv_file = file;
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

bool submitDivx3Decode(
    FILE *file, uint32_t packet_size, long next_offset) {
    if (!decode_task_handle || decode_in_flight || !divx3_decoder ||
        !file || !packet_size || next_offset < 0) {
        return false;
    }
    DecodeRequest request{};
    request.codec = VideoCodec::kDivx3;
    request.divx3_file = file;
    request.divx3_packet_size = packet_size;
    request.divx3_next_offset = next_offset;
    if (xQueueSend(decode_request_queue, &request, 0) != pdTRUE)
        return false;
    decode_in_flight = true;
    return true;
}

bool submitH263Decode() {
    if (!decode_task_handle || decode_in_flight || !h263_decoder ||
        !video_file || !isPacketVideoCodec(video_codec)) {
        return false;
    }
    DecodeRequest request{};
    request.codec = video_codec;
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

void bpvInputReaderTask(void *) {
    int result = BPV1_OK;
    while (!bpv_input_stop_requested) {
        const size_t count = std::fread(
            bpv_input_chunk, 1, kBpvInputChunkBytes, video_file);
        size_t sent = 0;
        while (sent < count && !bpv_input_stop_requested) {
            sent += xStreamBufferSend(
                bpv_input_stream, bpv_input_chunk + sent,
                count - sent, pdMS_TO_TICKS(20));
        }
        if (bpv_input_stop_requested) break;
        if (count != kBpvInputChunkBytes) {
            result = std::ferror(video_file)
                         ? BPV1_ERR_IO
                         : BPV1_EOF;
            break;
        }
    }
    bpv_input_reader_result = result;
    bpv_input_reader_done = true;
    bpv_input_reader_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void stopBpvInputPrefetch() {
    if (!bpv_input_stream && !bpv_input_chunk &&
        !bpv_input_reader_task_handle) {
        return;
    }
    bpv_input_stop_requested = true;
    const int64_t deadline =
        millisNow() + kBpvInputStopTimeoutMs;
    while (!bpv_input_reader_done && millisNow() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!bpv_input_reader_done && bpv_input_reader_task_handle) {
        ESP_LOGW(kTag, "BPV input reader stop timed out; deleting task");
        vTaskDelete(bpv_input_reader_task_handle);
        bpv_input_reader_task_handle = nullptr;
        bpv_input_reader_done = true;
    }
    if (bpv_input_stream) {
        vStreamBufferDelete(bpv_input_stream);
        bpv_input_stream = nullptr;
    }
    heap_caps_free(bpv_input_chunk);
    bpv_input_chunk = nullptr;
    bpv_input_reader_result = BPV1_OK;
}

bool startBpvInputPrefetch() {
    if (!video_file || bpv_input_stream ||
        bpv_input_reader_task_handle) {
        return false;
    }
    bpv_input_stream =
        xStreamBufferCreate(kBpvInputRingBytes, 1);
    bpv_input_chunk = static_cast<uint8_t *>(
        heap_caps_malloc(kBpvInputChunkBytes, MALLOC_CAP_8BIT));
    if (!bpv_input_stream || !bpv_input_chunk) {
        stopBpvInputPrefetch();
        return false;
    }
    bpv_input_stop_requested = false;
    bpv_input_reader_done = false;
    bpv_input_reader_result = BPV1_OK;
    if (xTaskCreatePinnedToCore(
            bpvInputReaderTask, "bpv-input-read",
            kBpvInputReaderStackBytes, nullptr, 2,
            &bpv_input_reader_task_handle, 1) != pdPASS) {
        bpv_input_reader_done = true;
        stopBpvInputPrefetch();
        return false;
    }
    const size_t preroll_bytes = kBpvInputRingBytes / 2;
    const int64_t deadline =
        millisNow() + kBpvInputPrerollTimeoutMs;
    while (xStreamBufferBytesAvailable(bpv_input_stream) <
               preroll_bytes &&
           !bpv_input_reader_done && millisNow() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    const size_t prefetched =
        xStreamBufferBytesAvailable(bpv_input_stream);
    if (!prefetched &&
        (bpv_input_reader_done || millisNow() >= deadline)) {
        stopBpvInputPrefetch();
        return false;
    }
    ESP_LOGI(
        kTag,
        "BPV input: fixed %u-byte ring, %u-byte refill, "
        "%u bytes prefetched, CPU1 reader",
        static_cast<unsigned>(kBpvInputRingBytes),
        static_cast<unsigned>(kBpvInputChunkBytes),
        static_cast<unsigned>(prefetched));
    return true;
}

size_t readBpvPrefetchedInput(
    void *, uint8_t *destination, size_t size
) {
    size_t received = 0;
    while (received < size && bpv_input_stream) {
        received += xStreamBufferReceive(
            bpv_input_stream, destination + received,
            size - received, pdMS_TO_TICKS(20));
        if (received < size && bpv_input_reader_done &&
            !xStreamBufferBytesAvailable(bpv_input_stream)) {
            break;
        }
    }
    return received;
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
    if (frame->storage_mode == HLV1_FRAME_STORAGE_Y7_U6_V6) {
        hlv1_frame_unpack_corrected_samples(
            y_row, 0, source_y, HLV1_V14_LUMA_BITS,
            frame->correction_y, frame->correction_stride_y,
            native_y_row, frame->width);
        if (chroma_y != hlv_cached_chroma_y) {
            hlv1_frame_unpack_corrected_samples(
                u_row, 0, chroma_y, HLV1_V14_CHROMA_BITS,
                frame->correction_u, frame->correction_stride_u,
                native_u_row, (frame->width + 1) / 2);
            hlv1_frame_unpack_corrected_samples(
                v_row, 0, chroma_y, HLV1_V14_CHROMA_BITS,
                frame->correction_v, frame->correction_stride_v,
                native_v_row, (frame->width + 1) / 2);
            hlv_cached_chroma_y = chroma_y;
        }
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

bool drawStatusTextAt(
    const char *text, int x, int y, int scale, bool centered
) {
    if (!text || !*text || scale < 1) return false;
    if (x < 0 || x >= kScreenWidth) return false;
    const int available_width = centered ? kScreenWidth : kScreenWidth - x;
    const size_t maximum_length =
        static_cast<size_t>((available_width + scale) / (6 * scale));
    const size_t length =
        std::min(std::strlen(text), maximum_length);
    const int glyph_advance = 6 * scale;
    const int width =
        static_cast<int>(length) * glyph_advance - scale;
    const int height = 7 * scale;
    uint16_t *pixels = display.acquireBuffer();
    if (!pixels || width <= 0 || width > kScreenWidth ||
        height > display.rowsPerTransfer() || y < 0 ||
        y + height > kScreenHeight)
        return false;
    std::fill_n(pixels, kScreenWidth * height, 0x0000);

    const int text_x = centered ? (kScreenWidth - width) / 2 : x;
    for (size_t index = 0; index < length; ++index) {
        unsigned char character =
            static_cast<unsigned char>(text[index]);
        if (character < 0x20 || character > 0x7e) character = '?';
        const uint8_t *columns = kStatusFont[character - 0x20];
        const int glyph_x = static_cast<int>(index) * glyph_advance;
        for (int source_y = 0; source_y < 7; ++source_y) {
            for (int source_x = 0; source_x < 5; ++source_x) {
                if (!(columns[source_x] & (1U << source_y))) continue;
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        pixels[(source_y * scale + dy) * kScreenWidth +
                               text_x + glyph_x +
                               source_x * scale + dx] =
                            0xffff;
                    }
                }
            }
        }
    }
    return display.drawBitmap(
               0, y, kScreenWidth, height, pixels) == ESP_OK;
}

bool drawStatusText(const char *text, int y, int scale) {
    return drawStatusTextAt(text, 0, y, scale, true);
}

bool drawStatusTextLeft(const char *text, int y, int scale) {
    return drawStatusTextAt(text, 6, y, scale, false);
}

void formatUploadProgress(
    char *text, size_t text_bytes, unsigned percent,
    uint32_t received, uint32_t total
) {
    uint32_t divisor;
    const char *unit;
    if (total < 999500U) {
        divisor = 1000U;
        unit = "KB";
    } else if (total < 999500000U) {
        divisor = 1000U * 1000U;
        unit = "MB";
    } else {
        divisor = 1000U * 1000U * 1000U;
        unit = "GB";
    }
    const auto format_value = [divisor](
        char *output, size_t output_bytes, uint32_t bytes
    ) {
        const uint64_t hundredths =
            (static_cast<uint64_t>(bytes) * 100U + divisor / 2U) /
            divisor;
        if (hundredths >= 10000U) {
            const unsigned rounded = static_cast<unsigned>(
                (static_cast<uint64_t>(bytes) + divisor / 2U) /
                divisor);
            std::snprintf(output, output_bytes, "%u", rounded);
        } else if (hundredths >= 1000U) {
            const uint64_t tenths =
                (static_cast<uint64_t>(bytes) * 10U +
                 divisor / 2U) /
                divisor;
            if (tenths >= 1000U) {
                const unsigned rounded = static_cast<unsigned>(
                    (static_cast<uint64_t>(bytes) +
                     divisor / 2U) /
                    divisor);
                std::snprintf(
                    output, output_bytes, "%u", rounded);
            } else {
                std::snprintf(
                    output, output_bytes, "%u.%u",
                    static_cast<unsigned>(tenths / 10U),
                    static_cast<unsigned>(tenths % 10U));
            }
        } else {
            std::snprintf(
                output, output_bytes, "%u.%02u",
                static_cast<unsigned>(hundredths / 100U),
                static_cast<unsigned>(hundredths % 100U));
        }
    };
    char completed_value[24]{};
    char total_value[24]{};
    format_value(
        completed_value, sizeof completed_value, received);
    format_value(total_value, sizeof total_value, total);
    std::snprintf(
        text, text_bytes, "%u%% %s/%s%s", percent,
        completed_value, total_value, unit);
}

void drawStatusTitle(const char *title) {
    if (!title || !*title) return;
    const size_t length = std::min<size_t>(std::strlen(title), 52);
    const int available_rows = display.rowsPerTransfer();
    const int scale =
        length * 12U <= kScreenWidth && 14 <= available_rows ? 2 : 1;
    const int height = 7 * scale;
    if (drawStatusText(
            title, (kScreenHeight - height) / 2, scale)) {
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

void showUartSession(const char *command) {
    if (display.clear(0x0000) != ESP_OK) return;
    drawStatusText("UART session", 88, 2);
    drawStatusText(command ? command : "TRANSFER", 126, 2);
    display.flush();
}

void showSeekStatus(uint32_t position_ms) {
    const uint32_t total_seconds = position_ms / 1000U;
    char position[16]{};
    std::snprintf(position, sizeof position, "%u:%02u",
                  static_cast<unsigned>(total_seconds / 60U),
                  static_cast<unsigned>(total_seconds % 60U));
    if (display.clear(0x0000) != ESP_OK) return;
    drawStatusText("Seeking to", 82, 2);
    drawStatusText(position, 122, 3);
    display.flush();
}

void beginUploadProgress(const char *filename, uint32_t total) {
    if (display.setDoubleBuffered(true) == ESP_OK) {
        upload_progress_scale = kUploadPercentScale;
    } else {
        upload_progress_scale = kUploadPercentFallbackScale;
        ESP_LOGW(
            kTag,
            "Could not allocate the second LCD DMA buffer for "
            "large upload progress text");
    }
    display.clear(0x0000);
    char progress_text[48]{};
    formatUploadProgress(
        progress_text, sizeof progress_text, 0U, 0U, total);
    drawStatusText(
        progress_text,
        kUploadBarY - 7 * upload_progress_scale - 12,
        upload_progress_scale);
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
    drawStatusText(
        filename, kUploadFilenameY, kUploadFilenameScale);
    display.flush();
    upload_progress_pixels = 0;
    upload_progress_percent = 0;
}

void updateUploadProgress(uint32_t received, uint32_t total, void *) {
    if (!total) return;
    const int inner_width = kUploadBarWidth - 2 * kUploadBarBorder;
    const int filled = static_cast<int>(
        std::min<uint64_t>(
            static_cast<uint64_t>(inner_width),
            (static_cast<uint64_t>(received) * inner_width) / total));
    if (filled > upload_progress_pixels) {
        const int changed = filled - upload_progress_pixels;
        const int x = kUploadBarX + kUploadBarBorder +
                      upload_progress_pixels;
        uint16_t *pixels = display.acquireBuffer();
        if (pixels) {
            const int inner_height =
                kUploadBarHeight - 2 * kUploadBarBorder;
            std::fill_n(
                pixels, changed * inner_height,
                kUploadBarFillColor);
            if (display.drawBitmap(
                    x, kUploadBarY + kUploadBarBorder,
                    changed, inner_height, pixels) == ESP_OK) {
                upload_progress_pixels = filled;
            }
        }
    }

    const unsigned percent = static_cast<unsigned>(
        std::min<uint64_t>(
            100U,
            (static_cast<uint64_t>(received) * 100U) / total));
    if (percent != upload_progress_percent) {
        char text[48]{};
        formatUploadProgress(
            text, sizeof text, percent, received, total);
        if (drawStatusText(
                text,
                kUploadBarY - 7 * upload_progress_scale - 12,
                upload_progress_scale)) {
            upload_progress_percent = percent;
        }
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
        bus.max_transfer_sz = HlvEsp32Decoder::kStreamBufferBytes;
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
    // Require CMD59 and CRC16 verification for SD SPI data transfers.
    host.flags &= ~SDMMC_HOST_FLAG_SPI_IGNORE_DATA_CRC;
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
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    esp_rom_printf("HLVBARE 1 SD_CRC ENABLED\n");
#endif
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
    if (frame_type > HLV1_FRAME_REPEAT || !q_y || !q_uv || q_shift > 3 ||
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

int prefetchDivx3AudioChunk() {
    uint32_t payload_size = 0;
    const int result =
        divx3_avi_next_audio_chunk(audio_file, &divx3_info,
                                   &payload_size);
    if (result != DIVX3_AVI_OK) return result;
    const int audio_result = prefetchAudioBytes(payload_size);
    if (audio_result != HLV1_OK) return audio_result;
    if ((payload_size & 1U) && std::fseek(audio_file, 1, SEEK_CUR)) {
        return DIVX3_AVI_ERR_IO;
    }
    return DIVX3_AVI_OK;
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
    if (isPacketVideoCodec(video_codec)) {
        return h263_info.container == H263_CONTAINER_AVI
                   ? prefetchH263AviPcmFrame()
                   : prefetchAmrNbAudioFrame();
    }
    if (video_codec == VideoCodec::kMjpeg)
        return prefetchMjpegAudioChunk();
    if (video_codec == VideoCodec::kDivx3)
        return prefetchDivx3AudioChunk();
    if (video_codec == VideoCodec::kBpv)
        return prefetchBpvAudioPacket();
    return prefetchHlvAudioPacket();
}

void audioReaderTask(void *) {
    int result = HLV1_OK;
    while (!audio_reader_stop_requested) {
        result = prefetchAudioPacket();
        if (result != HLV1_OK) break;
        // This task runs above the player task's priority.  Cooperatively
        // delay after each refill so an underflow cannot starve core 0.
        vTaskDelay(1);
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
    } else if (isPacketVideoCodec(video_codec)) {
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
    } else if (video_codec == VideoCodec::kDivx3) {
        Divx3AviInfo audio_info{};
        if (divx3_avi_read_info(audio_file, &audio_info) !=
                DIVX3_AVI_OK ||
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
    endH263RowPipeline();
    endHlvRowPipeline();
    stopDecodeWorker();
    stopBpvInputPrefetch();
    pending_frame_valid = false;
    pending_mpeg_frame_valid = false;
    pending_divx3_frame = {};
    pending_divx3_frame_valid = false;
    pending_h263_frame_valid = false;
    pending_h263_frame = {};
    pending_h263_decode_us = 0;
    h263_dual_buffered = false;
    h263_row_pipelined = false;
    pending_bpv_frame_valid = false;
    bpv_rgb565_palette_valid = false;
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
    divx3_decoder_destroy(divx3_decoder);
    divx3_decoder = nullptr;
    divx3_info = {};
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

unsigned char asciiLower(unsigned char character) {
    return character >= 'A' && character <= 'Z'
               ? static_cast<unsigned char>(character + ('a' - 'A'))
               : character;
}

int compareFilenames(const char *left, const char *right) {
    const auto *a = reinterpret_cast<const unsigned char *>(left);
    const auto *b = reinterpret_cast<const unsigned char *>(right);
    while (*a && *b) {
        const unsigned char lower_a = asciiLower(*a);
        const unsigned char lower_b = asciiLower(*b);
        if (lower_a != lower_b) return lower_a < lower_b ? -1 : 1;
        ++a;
        ++b;
    }
    if (*a != *b) return *a ? 1 : -1;
    return std::strcmp(left, right);
}

bool filenameHasExtension(const char *name, const char *extension) {
    const size_t name_length = std::strlen(name);
    const size_t extension_length = std::strlen(extension);
    if (name_length < extension_length) return false;
    const char *suffix = name + name_length - extension_length;
    for (size_t index = 0; index < extension_length; ++index) {
        if (asciiLower(static_cast<unsigned char>(suffix[index])) !=
            asciiLower(static_cast<unsigned char>(extension[index]))) {
            return false;
        }
    }
    return true;
}

bool isSupportedVideoFilename(const char *name) {
    return isSafeVideoFilename(name) &&
           (filenameHasExtension(name, ".hlv") ||
            filenameHasExtension(name, ".bpv1") ||
            filenameHasExtension(name, ".avi") ||
            filenameHasExtension(name, ".mpg") ||
            filenameHasExtension(name, ".mpeg") ||
            filenameHasExtension(name, ".3gp") ||
            filenameHasExtension(name, ".3gpp"));
}

bool copyFilename(
    char *destination, size_t destination_bytes, const char *source) {
    const int written =
        std::snprintf(destination, destination_bytes, "%s", source);
    return written >= 0 &&
           static_cast<size_t>(written) < destination_bytes;
}

bool insertSortedFilename(
    char filenames[][kBrowserFilenameBytes], size_t *count,
    size_t capacity, const char *filename) {
    size_t position = 0;
    while (position < *count &&
           compareFilenames(filenames[position], filename) < 0) {
        ++position;
    }
    if (position < *count &&
        compareFilenames(filenames[position], filename) == 0) {
        return true;
    }
    if (position >= capacity) return true;

    const size_t old_count = *count;
    const size_t new_count =
        old_count < capacity ? old_count + 1 : old_count;
    for (size_t index = new_count - 1; index > position; --index) {
        std::memcpy(filenames[index], filenames[index - 1],
                    kBrowserFilenameBytes);
    }
    if (!copyFilename(filenames[position], kBrowserFilenameBytes,
                      filename)) {
        return false;
    }
    *count = new_count;
    return true;
}

bool insertSortedFilenameTail(
    char filenames[][kBrowserFilenameBytes], size_t *count,
    size_t capacity, const char *filename) {
    for (size_t index = 0; index < *count; ++index) {
        if (compareFilenames(filenames[index], filename) == 0) return true;
    }
    if (*count == capacity) {
        if (compareFilenames(filename, filenames[0]) <= 0) return true;
        for (size_t index = 1; index < *count; ++index) {
            std::memcpy(filenames[index - 1], filenames[index],
                        kBrowserFilenameBytes);
        }
        --*count;
    }
    return insertSortedFilename(
        filenames, count, capacity, filename);
}

bool appendBrowserVisible(const char *filename) {
    for (size_t index = 0; index < browser_visible_count; ++index) {
        if (compareFilenames(
                browser_visible_filenames[index], filename) == 0) {
            return true;
        }
    }
    if (browser_visible_count >= kBrowserVisibleFiles) return true;
    if (!copyFilename(
            browser_visible_filenames[browser_visible_count],
            kBrowserFilenameBytes, filename)) {
        return false;
    }
    ++browser_visible_count;
    return true;
}

BrowserScanResult scanBrowserFile(bool advance) {
    DIR *directory = opendir(player_settings::kVideoDirectory);
    if (!directory) return BrowserScanResult::kIoError;

    char first[kBrowserVisibleFiles][kBrowserFilenameBytes]{};
    size_t first_count = 0;
    char preceding[kBrowserVisibleFiles - 1][kBrowserFilenameBytes]{};
    size_t preceding_count = 0;
    char exact[kBrowserFilenameBytes]{};
    char following[kBrowserVisibleFiles][kBrowserFilenameBytes]{};
    size_t following_count = 0;
    bool io_error = false;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno != 0) io_error = true;
            break;
        }
        if (std::strlen(entry->d_name) >= kBrowserFilenameBytes ||
            !isSupportedVideoFilename(entry->d_name)) {
            continue;
        }

        char path[sizeof selected_video_path]{};
        const int written = std::snprintf(
            path, sizeof path, "%s/%s",
            player_settings::kVideoDirectory, entry->d_name);
        struct stat info{};
        if (written < 0 ||
            static_cast<size_t>(written) >= sizeof path ||
            stat(path, &info) != 0) {
            io_error = true;
            break;
        }
        if (!S_ISREG(info.st_mode)) continue;

        if (!insertSortedFilename(
                first, &first_count, kBrowserVisibleFiles,
                entry->d_name)) {
            io_error = true;
            break;
        }
        if (browser_filename[0]) {
            const int order =
                compareFilenames(entry->d_name, browser_filename);
            if (order == 0 &&
                !copyFilename(exact, sizeof exact, entry->d_name)) {
                io_error = true;
                break;
            }
            if (order < 0 && !insertSortedFilenameTail(
                                 preceding, &preceding_count,
                                 kBrowserVisibleFiles - 1,
                                 entry->d_name)) {
                io_error = true;
                break;
            }
            if (order > 0 && !insertSortedFilename(
                                 following, &following_count,
                                 kBrowserVisibleFiles,
                                 entry->d_name)) {
                io_error = true;
                break;
            }
        }
    }
    if (closedir(directory) != 0) io_error = true;
    if (io_error) return BrowserScanResult::kIoError;
    if (!first_count) {
        browser_filename[0] = '\0';
        browser_visible_count = 0;
        return BrowserScanResult::kEmpty;
    }

    const bool chose_exact = !advance && exact[0];
    const bool chose_following = !chose_exact && following_count;
    const bool chose_first = !chose_exact && !chose_following;
    const char *chosen = chose_exact
                             ? exact
                             : (chose_following ? following[0] : first[0]);
    if (!copyFilename(
            browser_filename, sizeof browser_filename, chosen)) {
        return BrowserScanResult::kIoError;
    }

    browser_visible_count = 0;
    browser_selected_visible_index = 0;
    if (chose_first) {
        for (size_t index = 0; index < first_count; ++index) {
            if (!appendBrowserVisible(first[index])) {
                return BrowserScanResult::kIoError;
            }
        }
        return BrowserScanResult::kFound;
    }

    char before[kBrowserVisibleFiles - 1][kBrowserFilenameBytes]{};
    size_t before_count = 0;
    for (size_t index = 0; index < preceding_count; ++index) {
        if (!copyFilename(before[before_count], kBrowserFilenameBytes,
                          preceding[index])) {
            return BrowserScanResult::kIoError;
        }
        ++before_count;
    }
    if (chose_following && exact[0] && !insertSortedFilenameTail(
                                            before, &before_count,
                                            kBrowserVisibleFiles - 1,
                                            exact)) {
        return BrowserScanResult::kIoError;
    }

    const size_t after_start = chose_following ? 1 : 0;
    const size_t after_count = following_count - after_start;
    size_t after_to_show = std::min<size_t>(2, after_count);
    size_t before_to_show = std::min(
        kBrowserVisibleFiles - 1 - after_to_show, before_count);
    if (before_to_show < 2) {
        after_to_show = std::min(
            kBrowserVisibleFiles - 1 - before_to_show, after_count);
    }
    const size_t before_start = before_count - before_to_show;
    for (size_t index = before_start; index < before_count; ++index) {
        if (!appendBrowserVisible(before[index])) {
            return BrowserScanResult::kIoError;
        }
    }
    browser_selected_visible_index = browser_visible_count;
    if (!appendBrowserVisible(chosen)) return BrowserScanResult::kIoError;
    for (size_t index = 0; index < after_to_show; ++index) {
        if (!appendBrowserVisible(following[after_start + index])) {
            return BrowserScanResult::kIoError;
        }
    }
    return BrowserScanResult::kFound;
}

void drawFileBrowser() {
    const bool has_file = browser_filename[0] != '\0';
    esp_rom_printf(
        "B,%s\n", has_file ? browser_filename : "NO VIDEO FILES");
    if (display.clear(0x0000) != ESP_OK) {
        ESP_LOGE(kTag, "Could not clear file browser");
        return;
    }
    drawStatusText("SD VIDEO FILES", 14, 2);
    if (has_file) {
        for (size_t index = 0; index < browser_visible_count; ++index) {
            char line[kBrowserFilenameBytes + 3]{};
            std::snprintf(line, sizeof line, "%s%.111s",
                          index == browser_selected_visible_index
                              ? "> " : "  ",
                          browser_visible_filenames[index]);
            esp_rom_printf("BF,%u,%s\n", static_cast<unsigned>(index),
                           browser_visible_filenames[index]);
            drawStatusTextLeft(
                line, 48 + static_cast<int>(index) * 28, 1);
        }
        drawStatusText("SHORT: NEXT   HOLD: PLAY", 211, 1);
    } else {
        drawStatusText("NO VIDEO FILES", 104, 1);
        drawStatusText("SHORT: RESCAN", 211, 1);
    }
    display.flush();
}

bool writeBrowserSelection() {
    if (!browser_filename[0]) return false;
    FILE *selection =
        std::fopen(player_settings::kVideoSelectionPath, "wb");
    if (!selection) return false;
    const size_t length = std::strlen(browser_filename);
    bool written =
        std::fwrite(browser_filename, 1, length, selection) == length &&
        std::fputc('\n', selection) != EOF && std::fflush(selection) == 0;
    if (std::fclose(selection) != 0) written = false;
    return written;
}

void enterFileBrowser() {
    if (!sd_mounted && !mountSdCard()) {
        showStatus("microSD failed", "cannot browse /HLV");
        return;
    }
    closeVideo();
    file_browser_active = true;
    (void)display.setDoubleBuffered(false);

    const char *selected_name = std::strrchr(selected_video_path, '/');
    if (selected_name && selected_name[1]) {
        (void)copyFilename(
            browser_filename, sizeof browser_filename,
            selected_name + 1);
    } else {
        browser_filename[0] = '\0';
    }
    const BrowserScanResult result = scanBrowserFile(false);
    if (result == BrowserScanResult::kIoError) {
        showStatus("SD CARD READ ERROR", "cannot list /HLV");
        return;
    }
    drawFileBrowser();
}

void advanceFileBrowser() {
    const BrowserScanResult result = scanBrowserFile(true);
    if (result == BrowserScanResult::kIoError) {
        showStatus("SD CARD READ ERROR", "cannot list /HLV");
        return;
    }
    drawFileBrowser();
}

VideoOpenResult openVideoCandidate(const char *path) {
    bpv_file_version = 0;
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
        if (signature_size >= 5) bpv_file_version = signature[4];
    } else if (signature_size == sizeof signature &&
               !std::memcmp(signature, "RIFF", 4) &&
               !std::memcmp(signature + 8, "AVI ", 4)) {
        H2633gpInfo h263_probe{};
        const int h263_result =
            h263_avi_probe(video_file, &h263_probe);
        std::clearerr(video_file);
        if (std::fseek(video_file, 0, SEEK_SET)) {
            std::fclose(video_file);
            video_file = nullptr;
            return VideoOpenResult::kIoError;
        }
        Divx3AviInfo probe{};
        const int divx3_result =
            divx3_avi_read_info(video_file, &probe);
        std::clearerr(video_file);
        if (std::fseek(video_file, 0, SEEK_SET)) {
            std::fclose(video_file);
            video_file = nullptr;
            return VideoOpenResult::kIoError;
        }
        if (h263_result == H263_3GP_OK) {
            video_codec =
                h263_probe.video_codec == H263_VIDEO_CODEC_MPEG4_SIMPLE
                    ? VideoCodec::kMpeg4Simple
                    : VideoCodec::kH263;
        } else if (divx3_result == DIVX3_AVI_OK) {
            video_codec = VideoCodec::kDivx3;
        } else {
            video_codec = VideoCodec::kMjpeg;
        }
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
        video_codec != VideoCodec::kDivx3 &&
        !(video_codec == VideoCodec::kBpv &&
          bpv_file_version >= BPV1_PIXEL_MOTION_VERSION);
    if (display.setDoubleBuffered(use_double_display_buffer) != ESP_OK) {
        showStatus("Not enough RAM", "display buffer allocation failed");
        closeVideo();
        return false;
    }
    video_read_ahead_size =
        video_codec == VideoCodec::kMpeg1
            ? kMpegVideoReadAheadBytes
            : (video_codec == VideoCodec::kDivx3
                   ? kDivx3VideoReadAheadBytes
                   : (isPacketVideoCodec(video_codec)
                          ? kH263VideoReadAheadBytes
                          : (video_codec == VideoCodec::kBpv &&
                                     bpv_file_version >=
                                         BPV1_PIXEL_MOTION_VERSION
                                 ? kBpvVideoReadAheadBytes
                                 : kVideoReadAheadBytes)));
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
    /*
     * The first simultaneous audio FILE permanently expands picolibc's
     * stdio pool. If that happens while a decoder owns most of DRAM, the
     * retained pool block can split the only two QVGA DivX luma-sized
     * regions. Reserve those regions while creating the FILE slot once;
     * releasing the reservations does not increase steady-state usage.
     */
    static bool audio_file_pool_primed = false;
    if (!audio_file_pool_primed) {
        void *luma_reservations[2] = {
            heap_caps_malloc(
                kDivx3CompactLumaPlaneBytes, MALLOC_CAP_8BIT),
            heap_caps_malloc(
                kDivx3CompactLumaPlaneBytes, MALLOC_CAP_8BIT),
        };
        if (luma_reservations[0] && luma_reservations[1]) {
            FILE *audio_slot = std::fopen(active_video_path, "rb");
            if (audio_slot) {
                std::fclose(audio_slot);
                audio_file_pool_primed = true;
            }
        }
        heap_caps_free(luma_reservations[1]);
        heap_caps_free(luma_reservations[0]);
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
        const int probe_errno = errno;
        const bool probe_io_error = std::ferror(video_file) != 0;
        const long probe_position = std::ftell(video_file);
        if (width <= 0 || width > UINT16_MAX ||
            height <= 0 || height > UINT16_MAX ||
            !mpegFpsRational(
                fps, &sequence_header.fps_num,
                &sequence_header.fps_den)) {
            ESP_LOGE(kTag,
                     "MPEG probe failed: %dx%d fps=%.6f "
                     "pos=%ld ferror=%d errno=%d",
                     width, height, fps, probe_position,
                     probe_io_error ? 1 : 0, probe_errno);
            showStatus("Invalid video.mpg", "unsupported MPEG-1 stream");
            closeVideo();
            return false;
        }
        sequence_header.width = static_cast<uint16_t>(width);
        sequence_header.height = static_cast<uint16_t>(height);
        sequence_header.frame_count = 0;
        if (audio_sample_rate > 0 && audio_sample_rate <= UINT16_MAX) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            sequence_header.audio_codec = HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                static_cast<uint16_t>(audio_sample_rate);
            sequence_header.audio_channels = 1;
        }
        plm_rewind(mpeg_video);
        ESP_LOGI(kTag,
                 "MPEG-1/PS: %ux%u, %u/%u fps, streaming frame count, "
                 "MP2 audio=%u Hz, no-B two-frame decoder",
                 sequence_header.width, sequence_header.height,
                 sequence_header.fps_num, sequence_header.fps_den,
                 sequence_header.audio_sample_rate);
        if (!startDecodeWorker()) {
            showStatus("Dual-core init failed",
                       "cannot create CPU1 decoder task");
            closeVideo();
            return false;
        }
    } else if (isPacketVideoCodec(video_codec)) {
        h263_decoder = h263_3gp_decoder_create();
        if (h263_decoder) {
            /*
             * MPEG-4 keeps two packed Y6/U5/V5 pictures and reconstructs
             * through one 16-row byte-planar workspace. Predictive QCIF
             * H.263 still promotes this request to two full frames.
             */
            h263_3gp_decoder_set_output_buffer_count(h263_decoder, 1);
        }
        int result =
            h263_decoder
                ? h263_3gp_decoder_open(
                      h263_decoder, video_file, &h263_info)
                : H263_3GP_ERR_MEMORY;
        if (result == H263_3GP_OK &&
            (h263_info.fps_num > UINT16_MAX ||
             h263_info.fps_den > UINT16_MAX)) {
            result = H263_3GP_ERR_UNSUPPORTED;
        }
        if (result != H263_3GP_OK) {
            const int library_codec =
                packetVideoLibraryCodec(video_codec);
            showStatus(video_codec == VideoCodec::kMpeg4Simple
                           ? "Invalid MPEG-4 SP"
                           : "Invalid H.263",
                       h263_3gp_codec_strerror(library_codec, result));
            closeVideo();
            return false;
        }
        video_codec =
            h263_info.video_codec == H263_VIDEO_CODEC_MPEG4_SIMPLE
                ? VideoCodec::kMpeg4Simple
                : VideoCodec::kH263;
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
                 "%s/%s: %ux%u, %u/%u fps, %u frames, "
                 "profile=%u level=%u, audio=%u Hz/%u-bit, "
                 "decoder=%u bytes",
                 packetVideoCodecName(video_codec),
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
        h263_row_pipelined =
            h263_info.container == H263_CONTAINER_AVI &&
            h263_3gp_decoder_output_buffer_count(h263_decoder) == 1;
        if (h263_row_pipelined) {
            h263_3gp_decoder_set_output_row_guard(
                h263_decoder, waitForH263OutputRow, nullptr);
        }
        if ((h263_dual_buffered || h263_row_pipelined) &&
            !startDecodeWorker()) {
            showStatus("Dual-core init failed",
                       "cannot create CPU1 decoder task");
            closeVideo();
            return false;
        }
    } else if (video_codec == VideoCodec::kMjpeg) {
        int result = mjpeg_decoder.begin(
            video_file, &mjpeg_info,
            player_settings::kScaleVideoToDisplay);
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
    } else if (video_codec == VideoCodec::kDivx3) {
        int result = divx3_avi_read_info(video_file, &divx3_info);
        const uint32_t macroblocks =
            ((static_cast<uint32_t>(divx3_info.width) + 15U) / 16U) *
            ((static_cast<uint32_t>(divx3_info.height) + 15U) / 16U);
        if (result == DIVX3_AVI_OK &&
            (divx3_info.fps_num > UINT16_MAX ||
             divx3_info.fps_den > UINT16_MAX ||
             divx3_info.audio_sample_rate > UINT16_MAX ||
             divx3_info.max_video_packet_size >
                 kDivx3MaximumPacketBytes ||
             macroblocks > kDivx3MaximumMacroblocks)) {
            result = DIVX3_AVI_ERR_RANGE;
        }
        if (result != DIVX3_AVI_OK) {
            showStatus("Invalid DivX 3 AVI",
                       divx3_avi_strerror(result));
            closeVideo();
            return false;
        }
        divx3_decoder =
            divx3_decoder_create_y6_u5_v5(
                divx3_info.width, divx3_info.height);
        if (!divx3_decoder) {
            showStatus("Not enough RAM",
                       "DivX 3 decoder allocation failed");
            reportHeap("DivX 3 decoder allocation failed");
            closeVideo();
            return false;
        }
        sequence_header.width = divx3_info.width;
        sequence_header.height = divx3_info.height;
        sequence_header.fps_num =
            static_cast<uint16_t>(divx3_info.fps_num);
        sequence_header.fps_den =
            static_cast<uint16_t>(divx3_info.fps_den);
        sequence_header.frame_count = divx3_info.frame_count;
        if (divx3_info.audio_stream != 0xff) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            sequence_header.audio_codec = HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                static_cast<uint16_t>(divx3_info.audio_sample_rate);
            sequence_header.audio_channels = 1;
        }
        ESP_LOGI(kTag,
                 "DivX 3/AVI: %ux%u, %u/%u fps, %u frames, "
                 "PCM_U8 audio=%u Hz, compact decoder=%u bytes, "
                 "4 KB stream buffer, max packet=%u bytes",
                 sequence_header.width, sequence_header.height,
                 sequence_header.fps_num, sequence_header.fps_den,
                 static_cast<unsigned>(sequence_header.frame_count),
                 sequence_header.audio_sample_rate,
                 static_cast<unsigned>(
                     divx3_decoder_memory_bytes(divx3_decoder)),
                 static_cast<unsigned>(
                     divx3_info.max_video_packet_size));
        if (!startDecodeWorker()) {
            showStatus("Dual-core init failed",
                       "cannot create CPU1 decoder task");
            closeVideo();
            return false;
        }
    } else if (video_codec == VideoCodec::kBpv) {
        const int result = bpv_decoder.begin(video_file, &bpv_header);
        if (result != BPV1_OK) {
            showStatus("Invalid video.bpv1", bpv1_strerror(result));
            reportHeap("BPV1 decoder allocation failed");
            closeVideo();
            return false;
        }
        bpv_decoder.setProfileClock(bpvProfileNowMicros, nullptr);
        if (bpv_header.version >= BPV1_PIXEL_MOTION_VERSION &&
            display.rowsPerTransfer() != 8) {
            showStatus("Display buffer error",
                       "cannot select two 8-row SPI buffers");
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
        if (bpv_header.version >= BPV1_PIXEL_MOTION_VERSION &&
            !player_settings::kEnableBpvV7StreamingTask) {
            showStatus("BPV version unsupported",
                       "v7 requires the streaming worker");
            closeVideo();
            return false;
        }
        if (bpv_header.version >= BPV1_PIXEL_MOTION_VERSION &&
            !startBpvInputPrefetch()) {
            showStatus("BPV input init failed",
                       "cannot create CPU1 stream buffer");
            closeVideo();
            return false;
        }
        if (bpv_header.version < BPV1_PIXEL_MOTION_VERSION &&
            !startDecodeWorker()) {
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
            sequence_header, player_settings::kUseCompactHlvReference);
        if (decoder_result != HLV1_OK) {
            showStatus("Not enough RAM", "use at most the 320x180 profile");
            reportHeap("decoder or packet-pool allocation failed");
            closeVideo();
            return false;
        }
        decoder.setReferenceRowGuard(waitForHlvReferenceRows, nullptr);
        ESP_LOGI(kTag, "Packet stream buffer: %u bytes, DMA-capable=%u",
                 static_cast<unsigned>(decoder.streamBufferBytes()),
                 static_cast<unsigned>(decoder.dmaBuffer()));
        // Allocate the large predictive planes and stream buffer before the
        // worker stack, preserving the largest contiguous heap regions.
        if (!startDecodeWorker()) {
            showStatus("Dual-core init failed",
                       "cannot create CPU1 decoder task");
            closeVideo();
            return false;
        }
    }
    const bool is_cif_h263 =
        video_codec == VideoCodec::kH263 &&
        sequence_header.width == 352 &&
        sequence_header.height == 288;
    if ((sequence_header.width > kScreenWidth ||
         sequence_header.height > kScreenHeight) &&
        !is_cif_h263) {
        ESP_LOGE(kTag, "Unsupported dimensions: %ux%u",
                 sequence_header.width, sequence_header.height);
        showStatus("Video is too large",
                   "use 320x240 or H.263 CIF");
        closeVideo();
        return false;
    }
    // Allocate predictive frames, bounded packet/stream storage and the
    // decoder task before the
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
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    bare_benchmark_frames = 0;
    bare_benchmark_read_us = 0;
    bare_benchmark_decode_us = 0;
    bare_benchmark_render_us = 0;
#endif
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
    } else if (isPacketVideoCodec(video_codec)) {
        ESP_LOGI(kTag,
                 "Playing %s/%s in %s mode, "
                 "frame storage=%s",
                 packetVideoCodecName(video_codec),
                 h263_info.container == H263_CONTAINER_AVI
                     ? "AVI"
                     : "3GP",
                 sequence_header.width == 352 &&
                         sequence_header.height == 288
                     ? "pixel-exact x16-y16 320x240 crop"
                     : "native-centred",
                 h263_info.video_codec ==
                         H263_VIDEO_CODEC_MPEG4_SIMPLE
                     ? "two Y6/U5/V5 frames + 16-row workspace"
                     : "bounded YUV420 frame buffers");
    } else if (video_codec == VideoCodec::kMjpeg) {
        ESP_LOGI(kTag,
                 "Playing MJPEG in %s mode, frame storage=RGB565 strip",
                 player_settings::kScaleVideoToDisplay
                     ? "scale-to-320x240"
                     : "native-centred");
    } else if (video_codec == VideoCodec::kDivx3) {
        ESP_LOGI(kTag,
                 "Playing DivX 3 in %s mode, frame storage=two compact "
                 "Y6/U5/V5 reference frames",
                 player_settings::kScaleVideoToDisplay
                     ? "scale-to-320x240"
                     : "native-centred");
    } else if (video_codec == VideoCodec::kBpv) {
        ESP_LOGI(kTag,
                 "Playing BPV1 v%u in %s mode, frame storage=%s",
                 bpv_header.version,
                 player_settings::kScaleVideoToDisplay
                     ? "scale-to-320x240"
                     : "native-centred",
                 bpv_header.version >= BPV1_PIXEL_MOTION_VERSION
                     ? "previous RGB565 frame + two 8-row SPI buffers"
                     : "two 4x4-record frames");
    } else {
        ESP_LOGI(kTag, "Playing HLV v%u in %s mode, frame storage=%s%s",
                 sequence_header.version,
                 player_settings::kScaleVideoToDisplay
                     ? "scale-to-320x240"
                     : "native-centred",
                 decoder.compactYuv()
                     ? "packed Y7/U6/V6 + per-plane Q4 corrections"
                     : "8-bit YUV 4:2:0",
                 decoder.singleReference()
                     ? ", one reference + rolling rows"
                     : "");
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
            "#frame,sd_us,decode_us,render_us,work_us,present_us"
            "[,bpv_input_us,bpv_block_us,bpv_reference_us,"
            "bpv_input_calls,bpv_input_bytes]\n");
    }
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    esp_rom_printf(
        "HLVBARE 1 OPEN %u %ux%u %u/%u %u\n",
        static_cast<unsigned>(video_codec), sequence_header.width,
        sequence_header.height, sequence_header.fps_num,
        sequence_header.fps_den,
        static_cast<unsigned>(sequence_header.frame_count));
#endif
    return true;
}

void handleBootButtonEvent(boot_button_event_t event) {
    if (event == BOOT_BUTTON_EVENT_SHORT_PRESS) {
        if (file_browser_active) {
            advanceFileBrowser();
        } else {
            enterFileBrowser();
        }
        return;
    }
    if (event != BOOT_BUTTON_EVENT_LONG_PRESS ||
        !file_browser_active || !browser_filename[0]) {
        return;
    }
    if (!writeBrowserSelection()) {
        showStatus("SD CARD WRITE ERROR", "cannot update /HLV/play.txt");
        return;
    }

    ESP_LOGI(kTag, "BOOT selected: %s", browser_filename);
    file_browser_active = false;
    showStatus("PLAYING", browser_filename);
    if (!openVideo()) last_retry_ms = millisNow();
}

void processBootButtonEvents() {
    if (!player_settings::kUseBootButtonTask) {
        const uint32_t now_ms = static_cast<uint32_t>(millisNow());
        if (static_cast<int32_t>(
                now_ms - cooperative_boot_button_next_poll_ms) < 0) {
            return;
        }
        cooperative_boot_button_next_poll_ms =
            now_ms + player_settings::kBootButtonPollMs;
        const boot_button_event_t event = boot_button_state_update(
            &cooperative_boot_button_state,
            gpio_get_level(board::kBootButton) == 0, now_ms);
        if (event != BOOT_BUTTON_EVENT_NONE) {
            handleBootButtonEvent(event);
        }
        return;
    }
    if (!boot_button_event_queue) return;
    boot_button_event_t event = BOOT_BUTTON_EVENT_NONE;
    while (xQueueReceive(boot_button_event_queue, &event, 0) == pdTRUE) {
        handleBootButtonEvent(event);
    }
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
            publishHlvRenderedRows(
                scaled_y_map[y0 + rows - 1] + 1);
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
        publishHlvRenderedRows(y0 + rows);
    }
    return true;
}

void convertMpegRow(const plm_frame_t *frame, int source_y,
                    bool scaled, int first_source_x,
                    uint16_t *output, int output_width) {
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
        for (int output_x = 0; output_x < output_width; output_x += 2) {
            const int source_x = first_source_x + output_x;
            const int chroma_x = source_x >> 1;
            const int red_add = mpeg_red_add[chroma_x];
            const int green_add = mpeg_green_add[chroma_x];
            const int blue_add = mpeg_blue_add[chroma_x];
            output[output_x] = yuvToRgb565(
                y_row[source_x], red_add, green_add, blue_add);
            if (output_x + 1 < output_width) {
                output[output_x + 1] = yuvToRgb565(
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
                    convertMpegRow(frame, source_y, true, 0,
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

    const int source_width = static_cast<int>(frame->width);
    const int source_height = static_cast<int>(frame->height);
    const int width = std::min(source_width, kScreenWidth);
    const int height = std::min(source_height, kScreenHeight);
    const int source_x = (source_width - width) / 2;
    const int source_y = (source_height - height) / 2;
    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    for (int y0 = 0; y0 < height; y0 += rows_per_transfer) {
        const int rows = std::min(rows_per_transfer, height - y0);
        uint16_t *pixels = display.acquireBuffer();
        if (!pixels) return false;
        for (int row = 0; row < rows; ++row) {
            convertMpegRow(frame, source_y + y0 + row, false, source_x,
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
    if (frame->storage_mode == H263_FRAME_STORAGE_Y6_U5_V5) {
        adapted.storage_mode = PLM_FRAME_STORAGE_Y6_U5_V5;
        adapted.y = {
            static_cast<unsigned>(frame->compact.y.width),
            static_cast<unsigned>(frame->compact.y.height),
            static_cast<unsigned>(frame->compact.y.stride),
            frame->compact.y.data,
            static_cast<unsigned>(frame->compact.y.correction_stride),
            frame->compact.y.correction};
        adapted.cb = {
            static_cast<unsigned>(frame->compact.u.width),
            static_cast<unsigned>(frame->compact.u.height),
            static_cast<unsigned>(frame->compact.u.stride),
            frame->compact.u.data,
            static_cast<unsigned>(frame->compact.u.correction_stride),
            frame->compact.u.correction};
        adapted.cr = {
            static_cast<unsigned>(frame->compact.v.width),
            static_cast<unsigned>(frame->compact.v.height),
            static_cast<unsigned>(frame->compact.v.stride),
            frame->compact.v.data,
            static_cast<unsigned>(frame->compact.v.correction_stride),
            frame->compact.v.correction};
    } else {
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
    }
    const int rows_per_transfer = display.rowsPerTransfer();
    mpeg_cached_chroma_y = -1;
    const int source_width = static_cast<int>(adapted.width);
    const int source_height = static_cast<int>(adapted.height);
    const int width = std::min(source_width, kScreenWidth);
    const int height = std::min(source_height, kScreenHeight);
    const int source_x = (source_width - width) / 2;
    const int source_y =
        h263VisibleSourceY(source_width, source_height);
    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    for (int y0 = 0; y0 < height; y0 += rows_per_transfer) {
        const int rows = std::min(rows_per_transfer, height - y0);
        uint16_t *pixels = display.acquireBuffer();
        if (!pixels) {
            endH263RowPipeline();
            return false;
        }
        for (int row = 0; row < rows; ++row) {
            convertMpegRow(
                &adapted, source_y + y0 + row, false, source_x,
                pixels + row * width, width);
        }
        publishH263RenderedRows(source_y + y0 + rows);
        if (display.drawBitmap(
                x_offset, y_offset + y0, width, rows, pixels) != ESP_OK) {
            endH263RowPipeline();
            return false;
        }
    }
    return true;
}

struct MjpegRenderContext {
    uint32_t render_us = 0;
    int next_scaled_y = 0;
    bool display_failed = false;
};

uint16_t *acquireMjpegDmaStrip(void *opaque, uint16_t source_y,
                               uint16_t source_rows) {
    auto *context = static_cast<MjpegRenderContext *>(opaque);
    if (!context || !source_rows ||
        source_y + source_rows > mjpeg_info.height) {
        return nullptr;
    }
    const int64_t render_start = microsNow();
    uint16_t *pixels = display.acquireBuffer();
    context->render_us +=
        static_cast<uint32_t>(microsNow() - render_start);
    if (!pixels) context->display_failed = true;
    return pixels;
}

bool submitMjpegDmaStrip(void *opaque, const uint16_t *pixels,
                         uint16_t source_y, uint16_t source_rows) {
    auto *context = static_cast<MjpegRenderContext *>(opaque);
    if (!context || !pixels || !source_rows) return false;
    const int64_t render_start = microsNow();
    const int width = mjpeg_info.width;
    const int height = mjpeg_info.height;
    if (source_y + source_rows > height) return false;
    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    if (display.drawBitmap(x_offset, y_offset + source_y, width,
                           source_rows, pixels) != ESP_OK) {
        context->display_failed = true;
        return false;
    }
    context->render_us +=
        static_cast<uint32_t>(microsNow() - render_start);
    return true;
}

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
    if (!bpv_rgb565_palette_valid || frame->keyframe) {
        if (bpv1_palette_build_rgb565(
                &bpv_header, frame, bpv_rgb565_palette,
                BPV1_MAX_PALETTE_COLORS) != BPV1_OK) {
            bpv_rgb565_palette_valid = false;
            return false;
        }
        bpv_rgb565_palette_valid = true;
    }
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
                    if (bpv1_frame_render_rgb565_row_cached(
                            &bpv_header, frame,
                            static_cast<uint16_t>(source_y),
                            bpv_rgb565_palette,
                            BPV1_MAX_PALETTE_COLORS,
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
        if (bpv1_frame_render_rgb565_rows_cached(
                &bpv_header, frame, static_cast<uint16_t>(y0),
                static_cast<uint16_t>(rows), bpv_rgb565_palette,
                BPV1_MAX_PALETTE_COLORS, pixels, width,
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
            : isPacketVideoCodec(video_codec)
            ? h263_3gp_codec_strerror(
                  packetVideoLibraryCodec(video_codec), result)
            : video_codec == VideoCodec::kMjpeg
            ? mjpeg_avi_strerror(result)
            : video_codec == VideoCodec::kDivx3
            ? (result <= DIVX3_AVI_ERR_ARGUMENT
                   ? divx3_avi_strerror(result)
                   : divx3_strerror(result))
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

struct PresentationState {
    int64_t start_us = 0;
    bool render = true;
    bool seeking = false;
};

struct BpvDecodeBreakdown {
    uint32_t input_us = 0;
    uint32_t block_us = 0;
    uint32_t reference_us = 0;
    uint32_t input_calls = 0;
    uint32_t input_bytes = 0;
};

PresentationState beginPresentation() {
    PresentationState state{microsNow(), true, false};
    if (seek_fast_forward && decoded_frames < seek_target_frame) {
        state.render = false;
        state.seeking = true;
        return state;
    }
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
                        uint32_t decode_us, uint32_t render_us,
                        const BpvDecodeBreakdown *bpv_breakdown = nullptr) {
    ++decoded_frames;
    consecutive_sd_read_failures = 0;
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    if (!state.seeking) {
        ++bare_benchmark_frames;
        bare_benchmark_read_us += read_us;
        bare_benchmark_decode_us += decode_us;
        bare_benchmark_render_us += render_us;
    }
    if (decoded_frames == 1U && !state.seeking) {
        esp_rom_printf("HLVBARE 1 FIRST_FRAME %u\n",
                       static_cast<unsigned>(decoded_frames));
    } else if (decoded_frames == 300U && !state.seeking) {
        esp_rom_printf("HLVBARE 1 FRAME %u\n",
                       static_cast<unsigned>(decoded_frames));
        constexpr uint32_t kHeapCaps =
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        esp_rom_printf(
            "HLVBARE 1 RAM free=%u minimum=%u largest=%u "
            "control_stack_free=%u decoder_stack_free=%u\n",
            static_cast<unsigned>(heap_caps_get_free_size(kHeapCaps)),
            static_cast<unsigned>(
                heap_caps_get_minimum_free_size(kHeapCaps)),
            static_cast<unsigned>(
                heap_caps_get_largest_free_block(kHeapCaps)),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
            decode_task_handle
                ? static_cast<unsigned>(
                      uxTaskGetStackHighWaterMark(decode_task_handle))
                : 0U);
        const uint64_t total_work_us =
            bare_benchmark_read_us + bare_benchmark_decode_us +
            bare_benchmark_render_us;
        esp_rom_printf(
            "HLVBARE 1 SPEED frames=%u read_avg_us=%u "
            "decode_avg_us=%u render_avg_us=%u "
            "decoder_fps_milli=%u work_fps_milli=%u\n",
            static_cast<unsigned>(bare_benchmark_frames),
            bare_benchmark_frames
                ? static_cast<unsigned>(bare_benchmark_read_us /
                                        bare_benchmark_frames)
                : 0U,
            bare_benchmark_frames
                ? static_cast<unsigned>(bare_benchmark_decode_us /
                                        bare_benchmark_frames)
                : 0U,
            bare_benchmark_frames
                ? static_cast<unsigned>(bare_benchmark_render_us /
                                        bare_benchmark_frames)
                : 0U,
            bare_benchmark_decode_us
                ? static_cast<unsigned>(
                      (static_cast<uint64_t>(bare_benchmark_frames) *
                       1000000000ULL) /
                      bare_benchmark_decode_us)
                : 0U,
            total_work_us
                ? static_cast<unsigned>(
                      (static_cast<uint64_t>(bare_benchmark_frames) *
                       1000000000ULL) /
                      total_work_us)
                : 0U);
    }
#endif

    if (state.seeking && audio_enabled && audio_stream) {
        const uint64_t target_samples = frameAudioTarget(decoded_frames);
        uint8_t discard[256];
        while (seek_discarded_audio_samples < target_samples) {
            const size_t wanted = static_cast<size_t>(std::min<uint64_t>(
                sizeof discard,
                target_samples - seek_discarded_audio_samples));
            const size_t received = xStreamBufferReceive(
                audio_stream, discard, wanted, pdMS_TO_TICKS(20));
            if (!received) {
                if (audio_output_failed || audio_reader_result < HLV1_OK ||
                    (audio_prefetch_eof &&
                     !xStreamBufferBytesAvailable(audio_stream))) {
                    ESP_LOGW(kTag,
                             "Audio seek stopped at %llu/%llu samples",
                             static_cast<unsigned long long>(
                                 seek_discarded_audio_samples),
                             static_cast<unsigned long long>(target_samples));
                    stopAudio();
                    break;
                }
                continue;
            }
            seek_discarded_audio_samples += received;
        }
    }

    if (state.seeking && decoded_frames >= seek_target_frame) {
        seek_fast_forward = false;
        next_present_us = microsNow();
        frame_period_phase = 0;
        if (audio_enabled) {
            audio_played_samples = static_cast<uint32_t>(
                std::min<uint64_t>(seek_discarded_audio_samples,
                                   UINT32_MAX));
        }
        const uint64_t actual_ms =
            (static_cast<uint64_t>(decoded_frames) * 1000ULL *
             sequence_header.fps_den) /
            sequence_header.fps_num;
        esp_rom_printf(
            "HLVSEEKDONE 1 %u %llu %u\n",
            static_cast<unsigned>(seek_requested_ms),
            static_cast<unsigned long long>(actual_ms),
            static_cast<unsigned>(decoded_frames));
    }

    if (!state.seeking && !audio_enabled) {
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
    if (player_settings::kLogFrameTimings && !state.seeking) {
        // Capture every value before printing. UART overhead is therefore not
        // charged to this record, although it can consume slack before the
        // following frame.
        if (bpv_breakdown) {
            esp_rom_printf(
                "F,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                decoded_frames, read_us, decode_us, render_us,
                work_us, present_us, bpv_breakdown->input_us,
                bpv_breakdown->block_us,
                bpv_breakdown->reference_us,
                bpv_breakdown->input_calls,
                bpv_breakdown->input_bytes);
        } else {
            esp_rom_printf(
                "F,%u,%u,%u,%u,%u,%u\n", decoded_frames, read_us,
                decode_us, render_us, work_us, present_us);
        }
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
    const bool result =
        presentDecodedFrame(frame, renderHlvOpaque, read_us, decode_us);
    endHlvRowPipeline();
    return result;
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
    const PresentationState state = beginPresentation();
    uint32_t render_us = 0;
    if (state.render) {
        const int64_t render_start = microsNow();
        if (!renderH263Frame(frame)) return false;
        render_us = static_cast<uint32_t>(microsNow() - render_start);
    } else {
        endH263RowPipeline();
    }
    finishPresentation(state, 0, decode_us, render_us);
    return true;
}

void finishVideoLoop() {
    if (seek_fast_forward) {
        seek_fast_forward = false;
        next_present_us = microsNow();
        frame_period_phase = 0;
        if (audio_enabled) {
            audio_played_samples = static_cast<uint32_t>(
                std::min<uint64_t>(seek_discarded_audio_samples,
                                   UINT32_MAX));
        }
        const uint64_t actual_ms =
            (static_cast<uint64_t>(decoded_frames) * 1000ULL *
             sequence_header.fps_den) /
            sequence_header.fps_num;
        esp_rom_printf(
            "HLVSEEKDONE 1 %u %llu %u\n",
            static_cast<unsigned>(seek_requested_ms),
            static_cast<unsigned long long>(actual_ms),
            static_cast<unsigned>(decoded_frames));
    }
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
    const HLV1Frame *frame = nullptr;
    const int64_t decode_start = microsNow();
    const int decode_result = decoder.decodeNext(video_file, &frame);
    const uint32_t decode_us =
        static_cast<uint32_t>(microsNow() - decode_start);
    if (decode_result == HLV1_EOF) {
        finishVideoLoop();
        return;
    }
    if (decode_result != HLV1_OK) {
        if (decode_result == HLV1_ERR_IO) {
            failSdCardRead("cannot read HLV video");
            return;
        }
        failPlayback("Decode error", decode_result);
        return;
    }

    if (!presentFrame(frame, 0, decode_us)) {
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
        failSdCardRead(packetVideoReadError(video_codec));
        return;
    }
    if (result != H263_3GP_OK) {
        failPlayback(packetVideoDecodeErrorTitle(video_codec), result);
        return;
    }
    if (!presentH263Frame(&frame, decode_us)) {
        failPlayback("Display DMA error", H263_3GP_ERR_IO);
    }
}

void playOneH263FramePipelined() {
    if (!pending_h263_frame_valid) {
        if (!submitH263Decode()) {
            failPlayback(packetVideoPipelineErrorTitle(video_codec),
                         H263_3GP_ERR_IO);
            return;
        }
        DecodeResult first{};
        if (!waitDecode(&first) || first.codec != video_codec) {
            failPlayback(packetVideoPipelineErrorTitle(video_codec),
                         H263_3GP_ERR_DECODE);
            return;
        }
        if (first.result == H263_3GP_EOF) {
            finishVideoLoop();
            return;
        }
        if (first.result == H263_3GP_ERR_IO) {
            failSdCardRead(packetVideoReadError(video_codec));
            return;
        }
        if (first.result != H263_3GP_OK) {
            failPlayback(packetVideoDecodeErrorTitle(video_codec),
                         first.result);
            return;
        }
        pending_h263_frame = first.h263_frame;
        pending_h263_decode_us = first.decode_us;
        pending_h263_frame_valid = true;
    }

    const H2633gpFrame frame = pending_h263_frame;
    const uint32_t decode_us = pending_h263_decode_us;
    pending_h263_frame_valid = false;
    if (h263_row_pipelined && !seek_fast_forward) {
        beginH263RowPipeline();
    }
    if (!submitH263Decode()) {
        endH263RowPipeline();
        failPlayback(packetVideoPipelineErrorTitle(video_codec),
                     H263_3GP_ERR_IO);
        return;
    }

    const bool rendered = presentH263Frame(&frame, decode_us);
    DecodeResult next{};
    const bool received = waitDecode(&next);
    if (h263_row_pipelined) {
        endH263RowPipeline();
        const uint32_t wait_us = __atomic_load_n(
            &h263_row_guard_wait_us, __ATOMIC_RELAXED);
        next.decode_us -= std::min(next.decode_us, wait_us);
    }
    if (!rendered) {
        failPlayback("Display DMA error", H263_3GP_ERR_IO);
        return;
    }
    if (!received || next.codec != video_codec) {
        failPlayback(packetVideoPipelineErrorTitle(video_codec),
                     H263_3GP_ERR_DECODE);
        return;
    }
    if (next.result == H263_3GP_ERR_IO) {
        failSdCardRead(packetVideoReadError(video_codec));
        return;
    }
    if (next.result != H263_3GP_OK &&
        next.result != H263_3GP_EOF) {
        failPlayback(packetVideoDecodeErrorTitle(video_codec), next.result);
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
        const int decode_result =
            player_settings::kScaleVideoToDisplay
                ? mjpeg_decoder.decode(
                      packet, renderMjpegStrip, &render_context)
                : mjpeg_decoder.decodeDirect(
                      packet, acquireMjpegDmaStrip,
                      submitMjpegDmaStrip, &render_context);
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

plm_frame_t makeDivx3RenderFrame(const Divx3Frame &source) {
    plm_frame_t frame{};
    frame.width = source.width;
    frame.height = source.height;
    frame.storage_mode =
        source.storage_mode == DIVX3_FRAME_STORAGE_Y6_U5_V5
            ? PLM_FRAME_STORAGE_Y6_U5_V5
            : PLM_FRAME_STORAGE_YUV420;
    frame.y.width = source.width;
    frame.y.height = source.height;
    frame.y.stride = source.y_stride;
    frame.y.data = const_cast<uint8_t *>(source.y);
    frame.y.correction_stride = source.correction_stride_y;
    frame.y.correction =
        const_cast<int8_t *>(source.correction_y);
    frame.cb.width = (source.width + 1U) / 2U;
    frame.cb.height = (source.height + 1U) / 2U;
    frame.cb.stride = source.c_stride;
    frame.cb.data = const_cast<uint8_t *>(source.cb);
    frame.cb.correction_stride = source.correction_stride_c;
    frame.cb.correction =
        const_cast<int8_t *>(source.correction_cb);
    frame.cr.width = frame.cb.width;
    frame.cr.height = frame.cb.height;
    frame.cr.stride = source.c_stride;
    frame.cr.data = const_cast<uint8_t *>(source.cr);
    frame.cr.correction_stride = source.correction_stride_c;
    frame.cr.correction =
        const_cast<int8_t *>(source.correction_cr);
    return frame;
}

void playOneDivx3Frame() {
    uint32_t packet_size = 0;
    long next_offset = -1;
    const long retry_offset = std::ftell(video_file);
    const int64_t read_start = microsNow();
    int packet_result = divx3_avi_begin_video_packet(
        video_file, &divx3_info, &packet_size, &next_offset);
    if (packet_result == DIVX3_AVI_ERR_IO && retry_offset >= 0) {
        for (unsigned attempt = 1; attempt <= 2; ++attempt) {
            ESP_LOGW(kTag,
                     "Recovering DivX 3 packet at %ld, attempt %u/2",
                     retry_offset, attempt);
            if (!reopenVideoAt(retry_offset)) break;
            packet_result = divx3_avi_begin_video_packet(
                video_file, &divx3_info, &packet_size, &next_offset);
            if (packet_result == DIVX3_AVI_OK) {
                ESP_LOGI(kTag, "DivX 3 packet recovered at %ld",
                         retry_offset);
                break;
            }
            if (packet_result != DIVX3_AVI_ERR_IO) break;
        }
    }
    const uint32_t read_us =
        static_cast<uint32_t>(microsNow() - read_start);
    if (packet_result == DIVX3_AVI_EOF) {
        finishVideoLoop();
        return;
    }
    if (packet_result != DIVX3_AVI_OK) {
        if (packet_result == DIVX3_AVI_ERR_IO) {
            failSdCardRead("cannot read DivX 3 video");
            return;
        }
        failPlayback("DivX 3 packet error", packet_result);
        return;
    }

    Divx3Frame decoded{};
    const int64_t decode_start = microsNow();
    int decode_result = divx3_decoder_decode_stream(
        divx3_decoder, packet_size, readDivx3Stream,
        video_file, &decoded);
    if (divx3_avi_finish_video_packet(
            video_file, next_offset) != DIVX3_AVI_OK &&
        decode_result == DIVX3_OK) {
        decode_result = DIVX3_ERR_BITSTREAM;
    }
    const uint32_t decode_us =
        static_cast<uint32_t>(microsNow() - decode_start);
    if (decode_result != DIVX3_OK) {
        failPlayback("DivX 3 decode error", decode_result);
        return;
    }
    const plm_frame_t render_frame = makeDivx3RenderFrame(decoded);
    if (!presentDecodedFrame(
            &render_frame, renderMpegOpaque, read_us, decode_us)) {
        failPlayback("Display DMA error", DIVX3_ERR_BITSTREAM);
    }
}

void playOneDivx3FramePipelined() {
    uint32_t packet_size = 0;
    long next_offset = -1;
    const long retry_offset = std::ftell(video_file);
    const int64_t read_start = microsNow();
    int packet_result = divx3_avi_begin_video_packet(
        video_file, &divx3_info, &packet_size, &next_offset);
    if (packet_result == DIVX3_AVI_ERR_IO && retry_offset >= 0) {
        for (unsigned attempt = 1; attempt <= 2; ++attempt) {
            ESP_LOGW(kTag,
                     "Recovering DivX 3 packet at %ld, attempt %u/2",
                     retry_offset, attempt);
            if (!reopenVideoAt(retry_offset)) break;
            packet_result = divx3_avi_begin_video_packet(
                video_file, &divx3_info, &packet_size, &next_offset);
            if (packet_result == DIVX3_AVI_OK) {
                ESP_LOGI(kTag, "DivX 3 packet recovered at %ld",
                         retry_offset);
                break;
            }
            if (packet_result != DIVX3_AVI_ERR_IO) break;
        }
    }
    const uint32_t read_us =
        static_cast<uint32_t>(microsNow() - read_start);
    if (packet_result == DIVX3_AVI_EOF) {
        if (pending_divx3_frame_valid) {
            const plm_frame_t render_frame =
                makeDivx3RenderFrame(pending_divx3_frame);
            const bool rendered = presentDecodedFrame(
                &render_frame, renderMpegOpaque, pending_read_us,
                pending_decode_us);
            pending_divx3_frame_valid = false;
            if (!rendered) {
                failPlayback("Display DMA error",
                             DIVX3_ERR_BITSTREAM);
                return;
            }
        }
        finishVideoLoop();
        return;
    }
    if (packet_result != DIVX3_AVI_OK) {
        if (packet_result == DIVX3_AVI_ERR_IO) {
            failSdCardRead("cannot read DivX 3 video");
            return;
        }
        failPlayback("DivX 3 packet error", packet_result);
        return;
    }
    if (!submitDivx3Decode(video_file, packet_size, next_offset)) {
        failPlayback("DivX 3 decode pipeline error",
                     DIVX3_ERR_BITSTREAM);
        return;
    }

    bool rendered = true;
    if (pending_divx3_frame_valid) {
        const plm_frame_t render_frame =
            makeDivx3RenderFrame(pending_divx3_frame);
        rendered = presentDecodedFrame(
            &render_frame, renderMpegOpaque, pending_read_us,
            pending_decode_us);
        pending_divx3_frame_valid = false;
    }

    DecodeResult result{};
    const bool received = waitDecode(&result);
    if (!rendered) {
        failPlayback("Display DMA error", DIVX3_ERR_BITSTREAM);
        return;
    }
    if (!received) {
        failPlayback("DivX 3 decode pipeline error",
                     DIVX3_ERR_BITSTREAM);
        return;
    }
    if (result.codec != VideoCodec::kDivx3 ||
        result.result != DIVX3_OK) {
        failPlayback("DivX 3 decode error", result.result);
        return;
    }
    pending_divx3_frame = result.divx3_frame;
    pending_read_us = read_us;
    pending_decode_us = result.decode_us;
    pending_divx3_frame_valid = true;
}

struct BpvDirectRenderContext {
    int x_offset = 0;
    int y_offset = 0;
    uint32_t render_us = 0;
    bool display_failed = false;
};

uint16_t *acquireBpvDmaStrip(
    void *opaque, uint16_t, uint16_t
) {
    auto *context = static_cast<BpvDirectRenderContext *>(opaque);
    if (!context) return nullptr;
    const int64_t render_start = microsNow();
    uint16_t *pixels = display.acquireBuffer();
    context->render_us +=
        static_cast<uint32_t>(microsNow() - render_start);
    if (!pixels) context->display_failed = true;
    return pixels;
}

int submitBpvDmaStrip(
    void *opaque, const uint16_t *pixels, uint16_t y, uint16_t rows
) {
    auto *context = static_cast<BpvDirectRenderContext *>(opaque);
    if (!context || !pixels) return 1;
    const int64_t render_start = microsNow();
    const esp_err_t result = display.drawBitmap(
        context->x_offset, context->y_offset + y,
        bpv_header.width, rows, pixels);
    context->render_us +=
        static_cast<uint32_t>(microsNow() - render_start);
    if (result != ESP_OK) context->display_failed = true;
    return result == ESP_OK ? 0 : 1;
}

int flushBpvDmaStrips(void *opaque) {
    auto *context = static_cast<BpvDirectRenderContext *>(opaque);
    if (!context) return 1;
    const int64_t render_start = microsNow();
    const esp_err_t result = display.flush();
    context->render_us +=
        static_cast<uint32_t>(microsNow() - render_start);
    if (result != ESP_OK) context->display_failed = true;
    return result == ESP_OK ? 0 : 1;
}

void playOneBpvFrameSequential() {
    if (bpv_header.version >= BPV1_PIXEL_MOTION_VERSION) {
        if (decoded_frames >= bpv_header.frame_count) {
            finishVideoLoop();
            return;
        }
        const PresentationState presentation = beginPresentation();
        const BPV1Frame *frame = nullptr;
        uint32_t decode_us = 0;
        uint32_t render_us = 0;
        int decode_result;
        if (presentation.render &&
            !player_settings::kScaleVideoToDisplay) {
            BpvDirectRenderContext render_context{
                (kScreenWidth - bpv_header.width) / 2,
                (kScreenHeight - bpv_header.height) / 2};
            const int64_t combined_start = microsNow();
            decode_result = bpv_decoder.decodeNextDirectFromInput(
                readBpvPrefetchedInput, nullptr,
                display.rowsPerTransfer(),
                acquireBpvDmaStrip, submitBpvDmaStrip,
                flushBpvDmaStrips, &render_context, &frame);
            const uint32_t combined_us =
                static_cast<uint32_t>(
                    microsNow() - combined_start);
            render_us = render_context.render_us;
            decode_us = combined_us > render_us
                            ? combined_us - render_us
                            : 0;
            if (render_context.display_failed &&
                decode_result != BPV1_OK) {
                failPlayback("Display DMA error", BPV1_ERR_IO);
                return;
            }
        } else {
            const int64_t decode_start = microsNow();
            decode_result = bpv_decoder.decodeNextDirectFromInput(
                readBpvPrefetchedInput, nullptr,
                display.rowsPerTransfer(),
                nullptr, nullptr, nullptr, nullptr, &frame);
            decode_us = static_cast<uint32_t>(
                microsNow() - decode_start);
            if (decode_result == BPV1_OK &&
                presentation.render) {
                const int64_t render_start = microsNow();
                if (!renderBpvFrame(frame)) {
                    failPlayback(
                        "Display DMA error", BPV1_ERR_IO);
                    return;
                }
                render_us = static_cast<uint32_t>(
                    microsNow() - render_start);
            }
        }
        if (decode_result == BPV1_ERR_IO) {
            failSdCardRead("cannot stream BPV1 video");
            return;
        }
        if (decode_result != BPV1_OK) {
            failPlayback("BPV1 decode error", decode_result);
            return;
        }
        const BPV1DecodeProfile profile = bpv_decoder.lastProfile();
        const uint64_t non_block_us =
            static_cast<uint64_t>(profile.input_us) +
            profile.reference_commit_us;
        const BpvDecodeBreakdown breakdown{
            profile.input_us,
            decode_us > non_block_us
                ? static_cast<uint32_t>(decode_us - non_block_us)
                : 0U,
            profile.reference_commit_us,
            profile.input_calls,
            profile.input_bytes};
        finishPresentation(
            presentation, 0, decode_us, render_us, &breakdown);
        return;
    }

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

    const PresentationState presentation = beginPresentation();
    const BPV1Frame *frame = nullptr;
    const int64_t decode_start = microsNow();
    const int decode_result = bpv_decoder.decode(&packet, &frame);
    const uint32_t decode_us =
        static_cast<uint32_t>(microsNow() - decode_start);
    uint32_t render_us = 0;
    if (decode_result == BPV1_OK && presentation.render) {
        const int64_t render_start = microsNow();
        if (!renderBpvFrame(frame)) {
            failPlayback("Display DMA error", BPV1_ERR_IO);
            return;
        }
        render_us =
            static_cast<uint32_t>(microsNow() - render_start);
    }
    if (decode_result != BPV1_OK) {
        failPlayback("BPV1 decode error", decode_result);
        return;
    }
    finishPresentation(
        presentation, read_us, decode_us, render_us);
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
    // BPV v4-v6 replaces the active palette at every keyframe. Finish using the
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
    if (pending_frame_valid && !seek_fast_forward)
        beginHlvRowPipeline();
    if (!submitDecode(video_file)) {
        endHlvRowPipeline();
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
    if (!received) {
        failPlayback("Decode pipeline error", HLV1_ERR_IO);
        return;
    }
    if (!rendered) {
        failPlayback("Display DMA error", HLV1_ERR_IO);
        return;
    }
    if (result.codec != VideoCodec::kHlv) {
        failPlayback("Decode error", result.result);
        return;
    }
    if (result.result == HLV1_EOF) {
        finishVideoLoop();
        return;
    }
    if (result.result == HLV1_ERR_IO) {
        failSdCardRead("cannot read HLV video");
        return;
    }
    if (result.result != HLV1_OK || !result.hlv_frame) {
        ESP_LOGE(kTag, "HLV frame %u packet range %ld..%ld",
                 static_cast<unsigned>(decoded_frames + 1U),
                 pending_hlv_packet_offset, ftell(video_file));
        failPlayback("Decode error", result.result);
        return;
    }
    pending_frame = *result.hlv_frame;
    pending_read_us = 0;
    pending_decode_us = result.decode_us;
    pending_frame_valid = true;
}

}  // namespace

extern "C" void app_main(void) {
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    esp_rom_printf("HLVBARE 1 MICROKERNEL cores=%d control_cpu=%d "
                   "decoder_target=1\n",
                   CONFIG_FREERTOS_NUMBER_OF_CORES, xPortGetCoreID());
#endif
    ESP_LOGI(kTag, "Multi-codec ESP-IDF SD player starting");
    const esp_err_t display_result = display.init();
    if (display_result != ESP_OK) {
        ESP_LOGE(kTag, "Display initialization failed: %s",
                 esp_err_to_name(display_result));
        return;
    }
    if (!initializeBootButton()) {
        ESP_LOGE(kTag, "BOOT button initialization failed");
    }
    if (player_settings::kEnableUartControl) {
        const esp_err_t uart_result =
            uart_upload.begin(CONFIG_ESP_CONSOLE_UART_BAUDRATE);
        if (uart_result != ESP_OK) {
            ESP_LOGE(kTag, "UART upload initialization failed: %s",
                     esp_err_to_name(uart_result));
        }
    }
    showStatus("Multi-codec SD player", "mounting microSD");

    if (!mountSdCard()) {
        showStatus("microSD failed", "insert a FAT32 card and reset");
        last_retry_ms = millisNow();
    } else if (!openVideo()) {
        last_retry_ms = millisNow();
    }

    for (;;) {
        processBootButtonEvents();
        if (player_settings::kEnableUartControl) {
        UartUploadRequest upload_request{};
        if (uart_upload.pollRequest(&upload_request)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_upload.reject("NO_SD");
                last_retry_ms = millisNow();
                continue;
            }
            file_browser_active = false;
            closeVideo();
            beginUploadProgress(
                upload_request.filename, upload_request.size);
            char stored_path[128]{};
            const bool stored = uart_upload.receive(
                upload_request, player_settings::kVideoDirectory,
                stored_path, sizeof stored_path, updateUploadProgress,
                nullptr);
            display.flush();
            showStatus(stored ? "UART upload complete"
                              : "UART upload failed",
                       upload_request.filename);
            continue;
        }
        char session_command[16]{};
        if (uart_upload.takeSessionRequest(
                session_command, sizeof session_command)) {
            file_browser_active = false;
            quietUartDiagnostics();
            closeVideo();
            showUartSession(session_command);
            uart_upload.sessionReady(session_command);
            continue;
        }
        if (uart_upload.takeMonitoringRequest()) {
            uart_upload.monitoringReady();
            restoreUartDiagnostics();
            showStatus("UART monitoring", "resuming video");
            if (sd_mounted && !video_file && !openVideo()) {
                last_retry_ms = millisNow();
            }
            continue;
        }
        uint32_t seek_position_ms = 0;
        if (uart_upload.takeSeekRequest(&seek_position_ms)) {
            file_browser_active = false;
            seek_fast_forward = false;
            showSeekStatus(seek_position_ms);
            closeVideo();
            if (!sd_mounted && !mountSdCard()) {
                esp_rom_printf("HLVSEEKERR 1 NO_SD\n");
                last_retry_ms = millisNow();
                continue;
            }
            if (!openVideo()) {
                esp_rom_printf("HLVSEEKERR 1 OPEN_FAILED\n");
                last_retry_ms = millisNow();
                continue;
            }
            uint64_t target_frame =
                (static_cast<uint64_t>(seek_position_ms) *
                 sequence_header.fps_num) /
                (1000ULL * sequence_header.fps_den);
            if (sequence_header.frame_count &&
                target_frame >= sequence_header.frame_count) {
                target_frame = sequence_header.frame_count - 1U;
            }
            target_frame = std::min<uint64_t>(target_frame, UINT32_MAX);
            seek_requested_ms = seek_position_ms;
            seek_target_frame = static_cast<uint32_t>(target_frame);
            seek_discarded_audio_samples = 0;
            if (!seek_target_frame) {
                esp_rom_printf(
                    "HLVSEEKDONE 1 %u 0 0\n",
                    static_cast<unsigned>(seek_requested_ms));
            } else {
                seek_fast_forward = true;
                esp_rom_printf(
                    "HLVSEEKBEGIN 1 %u %u\n",
                    static_cast<unsigned>(seek_requested_ms),
                    static_cast<unsigned>(seek_target_frame));
            }
            continue;
        }
        if (uart_upload.takeListRequest()) {
            if (!sd_mounted && !mountSdCard()) {
                uart_upload.reject("NO_SD");
                last_retry_ms = millisNow();
            } else {
                file_browser_active = false;
                closeVideo();
                showUartSession("LIST");
                uart_upload.listDirectory(
                    player_settings::kVideoDirectory);
            }
            continue;
        }
        uint32_t requested_baud = 0;
        if (uart_upload.takeBaudRequest(&requested_baud)) {
            file_browser_active = false;
            showUartSession("BAUD");
            uart_upload.changeBaud(requested_baud);
            continue;
        }
        char delete_filename[UartUploadRequest::kMaximumFilenameBytes + 1]{};
        if (uart_upload.takeDeleteRequest(
                delete_filename, sizeof delete_filename)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_upload.reject("NO_SD");
                last_retry_ms = millisNow();
            } else {
                file_browser_active = false;
                closeVideo();
                showUartSession("DELETE");
                uart_upload.deleteFile(
                    player_settings::kVideoDirectory, delete_filename);
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
                file_browser_active = false;
                closeVideo();
                showUartSession("CRC32");
                uart_upload.checksumFile(
                    player_settings::kVideoDirectory, crc_filename);
            }
            continue;
        }
        UartBlockCrcRequest block_crc_request{};
        if (uart_upload.takeBlockCrcRequest(&block_crc_request)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_upload.reject("NO_SD");
                last_retry_ms = millisNow();
            } else {
                file_browser_active = false;
                closeVideo();
                showUartSession("BLOCK CRC32");
                uart_upload.checksumBlocks(
                    player_settings::kVideoDirectory, block_crc_request);
            }
            continue;
        }
        UartReadRequest read_request{};
        if (uart_upload.takeReadRequest(&read_request)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_upload.reject("NO_SD");
                last_retry_ms = millisNow();
            } else {
                file_browser_active = false;
                closeVideo();
                showUartSession("READ");
                uart_upload.readFile(
                    player_settings::kVideoDirectory, read_request);
            }
            continue;
        }
        UartPatchRequest patch_request{};
        if (uart_upload.takePatchRequest(&patch_request)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_upload.reject("NO_SD");
                last_retry_ms = millisNow();
            } else {
                file_browser_active = false;
                closeVideo();
                showUartSession("PATCH");
                uart_upload.patchFile(
                    player_settings::kVideoDirectory, patch_request);
            }
            continue;
        }
        SdBenchmarkRequest sd_benchmark_request{};
        if (uart_upload.takeSdBenchmarkRequest(
                &sd_benchmark_request)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_upload.reject("NO_SD");
                last_retry_ms = millisNow();
            } else {
                file_browser_active = false;
                closeVideo();
                showUartSession("SD BENCHMARK");
                const bool completed = uart_upload.benchmarkSd(
                    player_settings::kVideoDirectory,
                    sd_benchmark_request);
                showStatus(completed ? "SD benchmark complete"
                                     : "SD benchmark failed",
                           completed ? "temporary file removed"
                                     : "see UART error");
            }
            continue;
        }
        }
        if (file_browser_active) {
            vTaskDelay(pdMS_TO_TICKS(20));
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
        if (video_file && isPacketVideoCodec(video_codec) &&
            h263_decoder) {
            if (player_settings::kUseDualCorePipeline &&
                (h263_dual_buffered || h263_row_pipelined)) {
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
        if (video_file && video_codec == VideoCodec::kDivx3 &&
            divx3_decoder) {
            if (player_settings::kUseDualCorePipeline) {
                playOneDivx3FramePipelined();
            } else {
                playOneDivx3Frame();
            }
            continue;
        }
        if (video_file && video_codec == VideoCodec::kBpv &&
            bpv_decoder.ready()) {
            if (player_settings::kUseDualCorePipeline &&
                bpv_header.version < BPV1_PIXEL_MOTION_VERSION) {
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
