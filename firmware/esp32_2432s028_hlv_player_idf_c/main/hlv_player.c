#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/i2s_pdm.h"
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

#include "board_config.h"
#include "amrnb_3gp.h"
#include "bpv_esp32_decoder.h"
#include "boot_button.h"
#include "cyd_display.h"
#include "divx3.h"
#include "divx3_avi.h"
#include "h263_3gp.h"
#include "hlv1.h"
#include "ima_adpcm.h"
#include "hlv_esp32_decoder.h"
#include "mjpeg_avi_decoder.h"
#include "player_settings.h"
#include "pl_mpeg.h"
#include "uart_file_upload.h"
#include "y6u5v5_rgb565.h"
#include "avi_demux.h"

#ifndef MPEG1_RENDER_PROFILE
#define MPEG1_RENDER_PROFILE 0
#endif

#ifndef COMPACT_YUV_RGB565_FAST_PATH
#define COMPACT_YUV_RGB565_FAST_PATH 0
#endif

#ifndef COMPACT_YUV_RGB565_CLAMP_TABLES
#define COMPACT_YUV_RGB565_CLAMP_TABLES 0
#endif

#ifndef COMPACT_YUV_RGB565_HOT_IRAM
#define COMPACT_YUV_RGB565_HOT_IRAM 0
#endif

#ifndef COMPACT_YUV_Q4_LUT
#define COMPACT_YUV_Q4_LUT 0
#endif

#ifndef COMPACT_YUV_RGB565_VERIFY
#define COMPACT_YUV_RGB565_VERIFY 0
#endif

#ifndef MJPEG_INPUT_PREFETCH
#define MJPEG_INPUT_PREFETCH 0
#endif

#if MPEG1_RENDER_PROFILE
#include "esp_cpu.h"
#endif

static const char kTag[] = "hlv-player";
enum {
    kScreenWidth = CYD_DISPLAY_WIDTH,
    kScreenHeight = CYD_DISPLAY_HEIGHT,
    kMaximumH263Width = 352,
    kH263CifWidth = 352,
    kH263CifHeight = 288,
    kH263CifVisibleY = 16,
    kRetryDelayMs = 2000,
    kSdReadFailuresBeforeReinit = 3,
    kVideoReadAheadBytes = 16 * 1024,
    kMpegVideoReadAheadBytes = 4 * 1024,
    kMpegDimensionProbeBytes = 256 * 1024,
    kDivx3VideoReadAheadBytes = 4 * 1024,
    kDivx3MaximumPacketBytes = 96 * 1024,
    kDivx3MaximumMacroblocks = 300,
    kDivx3CompactLumaPlaneBytes = 57600,
    kH263VideoReadAheadBytes = 4 * 1024,
    kBpvVideoReadAheadBytes = 4 * 1024,
    /* Capacity is deliberately unrelated to the maximum encoded frame. */
    kBpvInputRingBytes = 8 * 1024,
    kBpvInputChunkBytes = 4 * 1024,
    kBpvInputReaderStackBytes = 4096,
    kBpvInputStopTimeoutMs = 500,
    kBpvInputPrerollTimeoutMs = 1000,
#if MJPEG_INPUT_PREFETCH
    /* Capacity is deliberately unrelated to the maximum JPEG packet. */
    kMjpegInputRingBytes = 8 * 1024,
    kMjpegInputRingStorageBytes = kMjpegInputRingBytes + 1,
    kMjpegInputChunkBytes = 512,
    /* Keep one producer chunk free so a late skip can never leave the reader
       blocked in xStreamBufferSend before it observes the skip command. */
    kMjpegInputSpeculativeBytes =
        kMjpegInputRingBytes - kMjpegInputChunkBytes,
    kMjpegInputReaderStackBytes = 3072,
    kMjpegInputStopTimeoutMs = 500,
    kMjpegInputWaitTimeoutMs = 3000,
    kMjpegMaximumPacketBytes = 1024 * 1024,
    kMjpegInputCommandDecode = 1,
    kMjpegInputCommandSkip = 2,
    kMjpegInputCommandStop = 3,
#endif
    kAudioStreamBytes = 4096,
// A FreeRTOS static stream buffer reserves one byte to distinguish full from
// empty, so the backing array is one byte larger than its useful capacity.
    kAudioStreamStorageBytes = kAudioStreamBytes + 1,
    kAudioDmaSamples = 256,
    kAudioDmaBufferBytes = kAudioDmaSamples * 2,
    kAudioDmaDescriptors = 4,
    kSdDmaMinimumBlockBytes = 512,
    kAudioDmaMinimumFreeBytes =
        kAudioDmaDescriptors * kAudioDmaBufferBytes +
        2 * kSdDmaMinimumBlockBytes,
    kAudioReadAheadBytes = 512,
    kAudioReadChunkBytes = 512,
    /* HLV PCM peaked below 1.9 KiB on hardware, so it keeps four KiB. The
       compact MP2 task measured below two KiB and uses 2.5 KiB so its
       proactive compressed refill fits beside the decoder and PDM heap. */
    kHlvAudioReaderStackBytes = 4096,
    kMpegAudioReaderStackBytes = 2560,
    kMpegAudioPrefetchThresholdBytes = 512,
    kAudioReaderStackBytes = 6144,
    kAudioReaderStopTimeoutMs = 500,
    kAudioPrerollTimeoutMs = 3000,
    kAudioClockWaitTimeoutMs = 3000,
    kAudioBiasRampMs = 100,
    kAudioBiasRampSettleMs = 2,
    kDecodeWorkerStackBytes = 4096,
    kBootButtonTaskStackBytes = 2048,
    kBrowserFilenameBytes = 112,
    kBrowserVisibleFiles = 5,
    kUploadBarX = 16,
    kUploadBarWidth = kScreenWidth - 2 * kUploadBarX,
    kUploadBarHeight = CYD_DISPLAY_ROWS_PER_TRANSFER,
    kUploadBarY = (kScreenHeight - kUploadBarHeight) / 2,
    kUploadBarBorder = 2,
    kUploadBarBorderColor = 0xffff,
    kUploadBarEmptyColor = 0x2104,
    kUploadBarFillColor = 0x07e0,
    kUploadPercentScale = 2,
    kUploadPercentFallbackScale = 1,
    kUploadFilenameScale = 1,
    kUploadFilenameY = kUploadBarY + kUploadBarHeight + 12,
};

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(value, low, high) \
    ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))

static void fillU16(uint16_t *values, size_t count, uint16_t value) {
    for (size_t index = 0; index < count; ++index) values[index] = value;
}
// Five column, seven row glyphs for printable ASCII 0x20 through 0x7e.
// Bits run from the top row (bit 0) to the bottom row (bit 6).
static const uint8_t kStatusFont[95][5] = {
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

_Static_assert(CONFIG_FREERTOS_NUMBER_OF_CORES >= 2 ||
                  !PLAYER_USE_DUAL_CORE_PIPELINE,
              "Dual-core playback requires a two-core FreeRTOS build");
_Static_assert(PLAYER_KEYFRAME_CATCHUP_LATE_FRAMES > 0,
              "Keyframe catch-up needs a positive late-frame threshold");
_Static_assert(kHlvAudioReaderStackBytes <= kAudioReaderStackBytes &&
                   kMpegAudioReaderStackBytes <= kAudioReaderStackBytes,
               "The shared static audio-reader stack must fit every codec");

typedef enum VideoCodec {
    VIDEO_CODEC_kNone,
    VIDEO_CODEC_kHlv,
    VIDEO_CODEC_kMjpeg,
    VIDEO_CODEC_kDivx3,
    VIDEO_CODEC_kBpv,
    VIDEO_CODEC_kMpeg1,
    VIDEO_CODEC_kH263,
    VIDEO_CODEC_kMpeg4Simple,
} VideoCodec;

typedef enum AudioBiasState {
    AUDIO_BIAS_kLow,
    AUDIO_BIAS_kRampUp,
    AUDIO_BIAS_kCentered,
    AUDIO_BIAS_kRampDown,
} AudioBiasState;

typedef enum BpvInputStartResult {
    BPV_INPUT_START_kReady,
    BPV_INPUT_START_kInvalidState,
    BPV_INPUT_START_kNoMemory,
    BPV_INPUT_START_kReadFailed,
} BpvInputStartResult;

#if MJPEG_INPUT_PREFETCH
typedef struct MjpegInputPacket {
    int result;
    size_t jpeg_size;
} MjpegInputPacket;
#endif

static bool isPacketVideoCodec(VideoCodec codec) {
    return codec == VIDEO_CODEC_kH263 ||
           codec == VIDEO_CODEC_kMpeg4Simple;
}

static int packetVideoLibraryCodec(VideoCodec codec) {
    return codec == VIDEO_CODEC_kMpeg4Simple
               ? H263_VIDEO_CODEC_MPEG4_SIMPLE
               : H263_VIDEO_CODEC_H263;
}

static const char *packetVideoCodecName(VideoCodec codec) {
    return codec == VIDEO_CODEC_kMpeg4Simple ? "MPEG-4 SP" : "H.263";
}

static const char *packetVideoReadError(VideoCodec codec) {
    return codec == VIDEO_CODEC_kMpeg4Simple
               ? "cannot read MPEG-4 SP video"
               : "cannot read H.263 video";
}

static const char *packetVideoDecodeErrorTitle(VideoCodec codec) {
    return codec == VIDEO_CODEC_kMpeg4Simple
               ? "MPEG-4 decode error"
               : "H.263 decode error";
}

static const char *packetVideoPipelineErrorTitle(VideoCodec codec) {
    return codec == VIDEO_CODEC_kMpeg4Simple
               ? "MPEG-4 pipeline error"
               : "H.263 pipeline error";
}

typedef enum SelectionReadResult {
    SELECTION_READ_kReady,
    SELECTION_READ_kMissingOrInvalid,
    SELECTION_READ_kIoError,
} SelectionReadResult;

typedef enum VideoOpenResult {
    VIDEO_OPEN_kReady,
    VIDEO_OPEN_kMissingOrUnsupported,
    VIDEO_OPEN_kIoError,
} VideoOpenResult;

typedef enum BrowserScanResult {
    BROWSER_SCAN_kFound,
    BROWSER_SCAN_kEmpty,
    BROWSER_SCAN_kIoError,
} BrowserScanResult;

typedef struct DecodeRequest {
    VideoCodec codec;
    FILE *hlv_file;
    const BPV1Packet *bpv_packet;
    FILE *divx3_file;
    uint32_t divx3_packet_size;
    long divx3_next_offset;
    FILE *bpv_file;
    bool bpv_prefetch;
    bool skip_predictive;
} DecodeRequest;

typedef struct DecodeResult {
    VideoCodec codec;
    int result;
    const HLV1Frame *hlv_frame;
    const BPV1Frame *bpv_frame;
    Divx3Frame divx3_frame;
    H2633gpFrame h263_frame;
    plm_frame_t mpeg_frame;
    bool has_mpeg_frame;
    bool keyframe;
    bool skipped;
    uint32_t compressed_skips;
    uint32_t decode_us;
#if HLV1_ENABLE_STAGE_PROFILE
    HLV1StageProfile hlv_profile;
    uint32_t hlv_row_guard_wait_us;
#endif
    BPV1Packet bpv_next_packet;
    int bpv_read_result;
    uint32_t bpv_read_us;
} DecodeResult;

cyd_display_t display = {0};
FILE *video_file = NULL;
FILE *audio_file = NULL;
hlv_esp32_decoder_t decoder = {0};
mjpeg_avi_decoder_t mjpeg_decoder = {0};
mjpeg_avi_info_t mjpeg_info = {0};
Divx3Decoder *divx3_decoder = NULL;
Divx3AviInfo divx3_info = {0};
typedef struct {
    bpv_esp32_decoder_t decoder;
    BPV1Header header;
    uint16_t rgb565_palette[BPV1_MAX_PALETTE_COLORS];
    uint16_t rgb_row[kScreenWidth];
} BpvRuntime;
BpvRuntime *bpv_runtime = NULL;
#define bpv_decoder (bpv_runtime->decoder)
#define bpv_header (bpv_runtime->header)
#define bpv_rgb565_palette (bpv_runtime->rgb565_palette)
#define bpv_rgb_row (bpv_runtime->rgb_row)
uint8_t bpv_file_version = 0;
plm_t *mpeg_video = NULL;
plm_t *mpeg_audio = NULL;
H2633gpDecoder *h263_decoder = NULL;
H2633gpInfo h263_info = {0};
AmrNb3gpDecoder *amrnb_audio_decoder = NULL;
AmrNb3gpInfo amrnb_audio_info = {0};
H263AviPcmReader *h263_avi_audio_reader = NULL;
uart_file_upload_t uart_upload = {0};
QueueHandle_t boot_button_event_queue = NULL;
TaskHandle_t boot_button_task_handle = NULL;
boot_button_state_t cooperative_boot_button_state = {0};
uint32_t cooperative_boot_button_next_poll_ms = 0;
HLV1Header sequence_header = {0};
VideoCodec video_codec = VIDEO_CODEC_kNone;
const char *active_video_path = NULL;
char selected_video_path[160] = {0};
char browser_filename[kBrowserFilenameBytes] = {0};
char browser_visible_filenames[kBrowserVisibleFiles]
                              [kBrowserFilenameBytes] = {{0}};
size_t browser_visible_count = 0;
size_t browser_selected_visible_index = 0;
bool file_browser_active = false;
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
#if MPEG1_RENDER_PROFILE
typedef struct MpegRenderProfile {
    uint64_t whole_cycles;
    uint64_t acquire_cycles;
    uint64_t y_unpack_cycles;
    uint64_t uv_unpack_cycles;
    uint64_t chroma_cycles;
    uint64_t rgb565_cycles;
    uint64_t fused_rgb565_cycles;
    uint64_t submit_cycles;
    uint64_t wall_us;
    uint32_t frames;
    uint32_t wall_frames;
    uint32_t transfers;
    bool printed;
} MpegRenderProfile;
MpegRenderProfile mpeg_render_profile = {0};
#endif
uint16_t scaled_rgb_row[kScreenWidth];
bool bpv_rgb565_palette_valid = false;
uint16_t scaled_x_map[kScreenWidth];
uint16_t scaled_y_map[kScreenHeight];
uint8_t native_y_row[kScreenWidth];
uint8_t native_u_row[kScreenWidth / 2];
uint8_t native_v_row[kScreenWidth / 2];
int hlv_cached_chroma_y = -1;
typedef union {
    struct {
        int32_t red_add[kMaximumH263Width / 2];
        int32_t green_add[kMaximumH263Width / 2];
        int32_t blue_add[kMaximumH263Width / 2];
    } mpeg;
    uint8_t audio_read_chunk[kAudioReadChunkBytes];
    uint8_t amrnb_audio_pcm[AMRNB_SAMPLES_PER_FRAME];
} PlayerScratch;
PlayerScratch player_scratch = {0};
#define mpeg_red_add (player_scratch.mpeg.red_add)
#define mpeg_green_add (player_scratch.mpeg.green_add)
#define mpeg_blue_add (player_scratch.mpeg.blue_add)
#define audio_read_chunk (player_scratch.audio_read_chunk)
#define amrnb_audio_pcm (player_scratch.amrnb_audio_pcm)
#define YUV_TABLE_VALUES(entry) \
    entry(0), entry(1), entry(2), entry(3), entry(4), entry(5), entry(6), entry(7), \
    entry(8), entry(9), entry(10), entry(11), entry(12), entry(13), entry(14), entry(15), \
    entry(16), entry(17), entry(18), entry(19), entry(20), entry(21), entry(22), entry(23), \
    entry(24), entry(25), entry(26), entry(27), entry(28), entry(29), entry(30), entry(31), \
    entry(32), entry(33), entry(34), entry(35), entry(36), entry(37), entry(38), entry(39), \
    entry(40), entry(41), entry(42), entry(43), entry(44), entry(45), entry(46), entry(47), \
    entry(48), entry(49), entry(50), entry(51), entry(52), entry(53), entry(54), entry(55), \
    entry(56), entry(57), entry(58), entry(59), entry(60), entry(61), entry(62), entry(63), \
    entry(64), entry(65), entry(66), entry(67), entry(68), entry(69), entry(70), entry(71), \
    entry(72), entry(73), entry(74), entry(75), entry(76), entry(77), entry(78), entry(79), \
    entry(80), entry(81), entry(82), entry(83), entry(84), entry(85), entry(86), entry(87), \
    entry(88), entry(89), entry(90), entry(91), entry(92), entry(93), entry(94), entry(95), \
    entry(96), entry(97), entry(98), entry(99), entry(100), entry(101), entry(102), entry(103), \
    entry(104), entry(105), entry(106), entry(107), entry(108), entry(109), entry(110), entry(111), \
    entry(112), entry(113), entry(114), entry(115), entry(116), entry(117), entry(118), entry(119), \
    entry(120), entry(121), entry(122), entry(123), entry(124), entry(125), entry(126), entry(127), \
    entry(128), entry(129), entry(130), entry(131), entry(132), entry(133), entry(134), entry(135), \
    entry(136), entry(137), entry(138), entry(139), entry(140), entry(141), entry(142), entry(143), \
    entry(144), entry(145), entry(146), entry(147), entry(148), entry(149), entry(150), entry(151), \
    entry(152), entry(153), entry(154), entry(155), entry(156), entry(157), entry(158), entry(159), \
    entry(160), entry(161), entry(162), entry(163), entry(164), entry(165), entry(166), entry(167), \
    entry(168), entry(169), entry(170), entry(171), entry(172), entry(173), entry(174), entry(175), \
    entry(176), entry(177), entry(178), entry(179), entry(180), entry(181), entry(182), entry(183), \
    entry(184), entry(185), entry(186), entry(187), entry(188), entry(189), entry(190), entry(191), \
    entry(192), entry(193), entry(194), entry(195), entry(196), entry(197), entry(198), entry(199), \
    entry(200), entry(201), entry(202), entry(203), entry(204), entry(205), entry(206), entry(207), \
    entry(208), entry(209), entry(210), entry(211), entry(212), entry(213), entry(214), entry(215), \
    entry(216), entry(217), entry(218), entry(219), entry(220), entry(221), entry(222), entry(223), \
    entry(224), entry(225), entry(226), entry(227), entry(228), entry(229), entry(230), entry(231), \
    entry(232), entry(233), entry(234), entry(235), entry(236), entry(237), entry(238), entry(239), \
    entry(240), entry(241), entry(242), entry(243), entry(244), entry(245), entry(246), entry(247), \
    entry(248), entry(249), entry(250), entry(251), entry(252), entry(253), entry(254), entry(255)
#define YUV_LUMA_ENTRY(sample) (298 * ((sample) > 16 ? (sample) - 16 : 0))
#define YUV_RED_ENTRY(sample) (409 * ((sample) - 128) + 128)
#define YUV_GREEN_U_ENTRY(sample) (-100 * ((sample) - 128))
#define YUV_GREEN_V_ENTRY(sample) (-208 * ((sample) - 128) + 128)
#define YUV_BLUE_ENTRY(sample) (516 * ((sample) - 128) + 128)
static const int32_t yuv_luma[256] = {
    YUV_TABLE_VALUES(YUV_LUMA_ENTRY)};
static const int32_t yuv_red_add[256] = {
    YUV_TABLE_VALUES(YUV_RED_ENTRY)};
static const int32_t yuv_green_u_add[256] = {
    YUV_TABLE_VALUES(YUV_GREEN_U_ENTRY)};
static const int32_t yuv_green_v_add[256] = {
    YUV_TABLE_VALUES(YUV_GREEN_V_ENTRY)};
static const int32_t yuv_blue_add[256] = {
    YUV_TABLE_VALUES(YUV_BLUE_ENTRY)};
#undef YUV_BLUE_ENTRY
#undef YUV_GREEN_V_ENTRY
#undef YUV_GREEN_U_ENTRY
#undef YUV_RED_ENTRY
#undef YUV_LUMA_ENTRY
#undef YUV_TABLE_VALUES
#if COMPACT_YUV_RGB565_FAST_PATH
static const y6u5v5_rgb565_color_tables_t y6u5v5_color_tables = {
    yuv_luma, yuv_red_add, yuv_green_u_add,
    yuv_green_v_add, yuv_blue_add};
#endif
#if COMPACT_YUV_RGB565_VERIFY
static uint32_t compact_yuv_attempted_pairs = 0;
static uint32_t compact_yuv_verified_pairs = 0;
#endif
static void initializeYuvTables(void) {
#if COMPACT_YUV_RGB565_FAST_PATH
    y6u5v5_rgb565_initialize();
#endif
}
int mpeg_cached_chroma_y = -1;
uint8_t *video_read_ahead = NULL;
size_t video_read_ahead_size = 0;
__attribute__((aligned(4))) uint8_t audio_read_ahead[kAudioReadAheadBytes];
sdmmc_card_t *sd_card = NULL;
void *sd_dma_aligned_buffer = NULL;
bool sd_bus_initialized = false;
bool sd_mounted = false;
uint32_t consecutive_sd_read_failures = 0;
StreamBufferHandle_t audio_stream = NULL;
StaticStreamBuffer_t audio_stream_state = {0};
__attribute__((aligned(4))) uint8_t audio_stream_storage[kAudioStreamStorageBytes];
__attribute__((aligned(4))) uint8_t audio_dma_samples[kAudioDmaBufferBytes];
i2s_chan_handle_t audio_pdm = NULL;
TaskHandle_t audio_reader_task_handle = NULL;
StaticTask_t audio_reader_task_state = {0};
StackType_t audio_reader_task_stack[
    (kAudioReaderStackBytes + sizeof(StackType_t) - 1U) /
    sizeof(StackType_t)] = {0};
volatile bool audio_reader_task_finished = true;
void *audio_dma_buffer_keys[kAudioDmaDescriptors] = {0};
uint16_t audio_dma_valid_samples[kAudioDmaDescriptors] = {0};
bool audio_enabled = false;
volatile bool audio_started = false;
bool audio_pdm_enabled = false;
volatile AudioBiasState audio_bias_state = AUDIO_BIAS_kLow;
volatile uint32_t audio_bias_phase_q16 = 0;
volatile uint32_t audio_bias_step_q16 = 0;
volatile uint32_t audio_bias_ramp_remaining = 0;
uint32_t audio_output_sample_rate = 0;
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
volatile uint32_t mpeg_audio_decode_frames = 0;
volatile uint32_t mpeg_audio_decode_us = 0;
volatile uint32_t mpeg_audio_convert_us = 0;
volatile uint32_t amrnb_audio_decode_frames = 0;
volatile uint32_t amrnb_audio_decode_us = 0;
volatile uint32_t amrnb_audio_convert_us = 0;
size_t audio_preroll_bytes = 0;
uint8_t audio_output_codec = HLV1_AUDIO_NONE;
bool audio_output_signed_pcm16 = false;
QueueHandle_t decode_request_queue = NULL;
QueueHandle_t decode_result_queue = NULL;
TaskHandle_t decode_task_handle = NULL;
bool decode_in_flight = false;
StreamBufferHandle_t bpv_input_stream = NULL;
TaskHandle_t bpv_input_reader_task_handle = NULL;
uint8_t *bpv_input_chunk = NULL;
volatile bool bpv_input_stop_requested = false;
volatile bool bpv_input_reader_done = true;
volatile int bpv_input_reader_result = BPV1_OK;
#if MJPEG_INPUT_PREFETCH
StreamBufferHandle_t mjpeg_input_stream = NULL;
StaticStreamBuffer_t mjpeg_input_stream_state = {0};
__attribute__((aligned(4))) uint8_t
    mjpeg_input_stream_storage[kMjpegInputRingStorageBytes];
QueueHandle_t mjpeg_input_packet_queue = NULL;
StaticQueue_t mjpeg_input_packet_queue_state = {0};
uint8_t mjpeg_input_packet_queue_storage[sizeof(MjpegInputPacket)];
TaskHandle_t mjpeg_input_reader_task_handle = NULL;
StaticTask_t mjpeg_input_reader_task_state = {0};
StackType_t mjpeg_input_reader_task_stack[
    (kMjpegInputReaderStackBytes + sizeof(StackType_t) - 1U) /
    sizeof(StackType_t)] = {0};
volatile bool mjpeg_input_stop_requested = false;
volatile bool mjpeg_input_reader_finished = true;
#endif
typedef union {
    HLV1Frame hlv;
    plm_frame_t mpeg;
    Divx3Frame divx3;
    H2633gpFrame h263;
    BPV1Frame bpv;
} PendingFrameStorage;
PendingFrameStorage pending_frame_storage = {0};
#define pending_frame (pending_frame_storage.hlv)
#define pending_mpeg_frame (pending_frame_storage.mpeg)
#define pending_divx3_frame (pending_frame_storage.divx3)
#define pending_h263_frame (pending_frame_storage.h263)
#define pending_bpv_frame (pending_frame_storage.bpv)
bool pending_frame_valid = false;
bool pending_frame_keyframe = false;
bool pending_mpeg_frame_valid = false;
uint32_t pending_mpeg_decode_us = 0;
bool pending_divx3_frame_valid = false;
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
bool pending_bpv_frame_valid = false;
BPV1Packet ready_bpv_packet = {0};
bool ready_bpv_packet_valid = false;
bool bpv_stream_eof = false;
uint32_t ready_bpv_read_us = 0;
uint32_t pending_read_us = 0;
uint32_t pending_decode_us = 0;
long pending_hlv_packet_offset = -1;
uint32_t skipped_presentations = 0;
uint32_t consecutive_late_presentations = 0;
bool video_waiting_for_keyframe = false;
uint32_t keyframe_catchups = 0;
uint32_t compressed_predictive_skips = 0;
int upload_progress_pixels = -1;
int upload_progress_percent = -1;
int upload_progress_scale = kUploadPercentScale;
static bool uart_diagnostics_quiet = false;

void consumeMpegBFrameRows(
    plm_video_t *decoder, const plm_frame_t *rows,
    unsigned first_y, unsigned row_count, void *opaque);

static void quietUartDiagnostics(void) {
    if (uart_diagnostics_quiet) return;
    esp_log_level_set("*", ESP_LOG_NONE);
    uart_diagnostics_quiet = true;
}

static void restoreUartDiagnostics(void) {
    if (!uart_diagnostics_quiet) return;
    esp_log_level_set(
        "*", (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL);
    uart_diagnostics_quiet = false;
}

int64_t microsNow() { return esp_timer_get_time(); }

uint64_t bpvProfileNowMicros(void *opaque) {
    (void)opaque;
    return (uint64_t)microsNow();
}

int64_t millisNow() { return microsNow() / 1000; }

static void bootButtonTask(void *opaque) {
    (void)opaque;
    boot_button_state_t state = {0};
    boot_button_state_init(
        &state, gpio_get_level(BOARD_BOOT_BUTTON) == 0,
        (uint32_t)millisNow(), PLAYER_BOOT_BUTTON_DEBOUNCE_MS,
        PLAYER_BOOT_BUTTON_LONG_PRESS_MS);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PLAYER_BOOT_BUTTON_POLL_MS));
        const boot_button_event_t event = boot_button_state_update(
            &state, gpio_get_level(BOARD_BOOT_BUTTON) == 0,
            (uint32_t)millisNow());
        if (event != BOOT_BUTTON_EVENT_NONE) {
            (void)xQueueSend(boot_button_event_queue, &event, 0);
        }
    }
}

static bool initializeBootButton(void) {
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&config) != ESP_OK) return false;

    if (!PLAYER_USE_BOOT_BUTTON_TASK) {
        const uint32_t now_ms = (uint32_t)millisNow();
        boot_button_state_init(
            &cooperative_boot_button_state,
            gpio_get_level(BOARD_BOOT_BUTTON) == 0, now_ms,
            PLAYER_BOOT_BUTTON_DEBOUNCE_MS,
            PLAYER_BOOT_BUTTON_LONG_PRESS_MS);
        cooperative_boot_button_next_poll_ms =
            now_ms + PLAYER_BOOT_BUTTON_POLL_MS;
        return true;
    }

    boot_button_event_queue =
        xQueueCreate(4, sizeof(boot_button_event_t));
    if (!boot_button_event_queue) return false;
    if (xTaskCreatePinnedToCore(
            bootButtonTask, "boot-button", kBootButtonTaskStackBytes,
            NULL, tskIDLE_PRIORITY + 1, &boot_button_task_handle, 0) !=
        pdPASS) {
        vQueueDelete(boot_button_event_queue);
        boot_button_event_queue = NULL;
        return false;
    }
    return true;
}

static int h263VisibleSourceY(int source_width, int source_height) {
    if (source_width == kH263CifWidth &&
        source_height == kH263CifHeight) {
        return kH263CifVisibleY;
    }
    return (source_height - MIN(source_height, kScreenHeight)) / 2;
}

void waitForH263OutputRow(void *opaque, uint16_t first_y) {
    (void)opaque;
    if (!__atomic_load_n(&h263_row_pipeline_active, __ATOMIC_ACQUIRE))
        return;
    const int source_height = sequence_header.height;
    const int visible_height = MIN(source_height, kScreenHeight);
    const int first_visible_y = h263VisibleSourceY(
        sequence_header.width, source_height);
    const int visible_end_y = first_visible_y + visible_height;
    const int row_end_y = MIN(first_y + 16, visible_end_y);
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
        (uint32_t)(microsNow() - wait_start),
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

static size_t readDivx3File(
    void *context, uint8_t *buffer, size_t capacity) {
    FILE *file = (FILE *)(context);
    return file && buffer && capacity
               ? fread(buffer, 1, capacity, file)
               : 0;
}

void waitForHlvReferenceRows(void *opaque, int first_y, int rows) {
    (void)opaque;
    if (!__atomic_load_n(&hlv_row_pipeline_active, __ATOMIC_ACQUIRE))
        return;
    const int row_end_y =
        MIN(first_y + rows, (int)(sequence_header.height));
    if (first_y >= row_end_y)
        return;
    const int64_t wait_start = microsNow();
    while (__atomic_load_n(&hlv_row_pipeline_active, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(
               &hlv_rendered_source_rows, __ATOMIC_ACQUIRE) < row_end_y) {
        taskYIELD();
    }
    __atomic_fetch_add(
        &hlv_row_guard_wait_us,
        (uint32_t)(microsNow() - wait_start),
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

void decodeTask(void *opaque) {
    (void)opaque;
    DecodeRequest request = {0};
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    esp_rom_printf("HLVBARE 1 DECODER_CPU %d\n", xPortGetCoreID());
#endif
    for (;;) {
        if (xQueueReceive(decode_request_queue, &request,
                          portMAX_DELAY) != pdTRUE) {
            continue;
        }
        DecodeResult result = {0};
        result.codec = request.codec;
        const int64_t start = microsNow();
        if (request.codec == VIDEO_CODEC_kMpeg1) {
            unsigned skipped = 0;
            plm_frame_t *frame = mpeg_video
                                     ? (request.skip_predictive
                                            ? plm_decode_video_keyframe(
                                                  mpeg_video, &skipped)
                                            : plm_decode_video(mpeg_video))
                                     : NULL;
            result.compressed_skips = skipped;
            if (frame) {
                result.mpeg_frame = *frame;
                result.has_mpeg_frame = true;
            }
            result.result =
                !frame && video_file && ferror(video_file)
                    ? HLV1_ERR_IO
                    : HLV1_OK;
        } else if (request.codec == VIDEO_CODEC_kHlv) {
            HLV1Packet packet_info = {0};
            result.result = hlv_esp32_decoder_decode_next_catchup(
                &decoder, request.hlv_file, &result.hlv_frame, &packet_info,
#if HLV1_ENABLE_STAGE_PROFILE
                &result.hlv_profile,
#else
                NULL,
#endif
                request.skip_predictive, &result.skipped);
            result.keyframe =
                packet_info.frame_type == HLV1_FRAME_KEY;
        } else if (request.codec == VIDEO_CODEC_kBpv) {
            if (request.skip_predictive && request.bpv_packet &&
                !request.bpv_packet->info.keyframe) {
                result.result = BPV1_OK;
                result.skipped = true;
            } else {
                result.result = bpv_esp32_decoder_decode(
                    &bpv_decoder, request.bpv_packet, &result.bpv_frame);
            }
        } else if (request.codec == VIDEO_CODEC_kDivx3) {
            int intra = 0;
            Divx3ReplayReader stream = {
                .read = readDivx3File,
                .read_context = request.divx3_file,
            };
            if (request.skip_predictive && request.divx3_file) {
                uint8_t prefix = 0;
                if (fread(&prefix, 1, 1, request.divx3_file) == 1) {
                    stream.prefix = prefix;
                    stream.prefix_pending = 1;
                    if (divx3_packet_probe_intra(&prefix, 1, &intra) ==
                            DIVX3_OK &&
                        !intra) {
                        result.result = DIVX3_OK;
                        result.skipped = true;
                    }
                }
            }
            if (!result.skipped) {
                result.result =
                    divx3_decoder && request.divx3_file &&
                            request.divx3_packet_size &&
                            request.divx3_next_offset >= 0
                        ? divx3_decoder_decode_stream(
                              divx3_decoder, request.divx3_packet_size,
                              divx3_replay_read, &stream,
                              &result.divx3_frame)
                        : DIVX3_ERR_ARGUMENT;
            }
            if (request.divx3_file &&
                request.divx3_next_offset >= 0 &&
                divx3_avi_finish_video_packet(
                    request.divx3_file,
                    request.divx3_next_offset) != DIVX3_AVI_OK &&
                result.result == DIVX3_OK) {
                result.result = DIVX3_ERR_BITSTREAM;
            }
        } else if (isPacketVideoCodec(request.codec)) {
            int skipped = 0;
            result.result = h263_decoder && video_file
                                ? h263_3gp_decoder_decode_next_catchup(
                                      h263_decoder, video_file,
                                      &result.h263_frame,
                                      request.skip_predictive, &skipped)
                                : H263_3GP_ERR_ARGUMENT;
            result.skipped = skipped != 0;
        } else {
            result.result = HLV1_ERR_ARGUMENT;
        }
        result.decode_us = (uint32_t)(microsNow() - start);
#if PLM_MPEG_DECODE_PROFILE
        if (request.codec == VIDEO_CODEC_kMpeg1 &&
            result.has_mpeg_frame) {
            plm_video_decode_profile_t profile = {0};
            plm_get_video_decode_profile(mpeg_video, &profile);
            if (profile.frames != 0U && profile.frames % 60U == 0U) {
                esp_rom_printf(
                    "MDP,%u,%u,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u\n",
                    (unsigned)(profile.frames),
                    (unsigned)(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ),
                    (unsigned long long)(profile.total_cycles),
                    (unsigned long long)(profile.coefficient_cycles),
                    (unsigned long long)(profile.reconstruction_cycles),
                    (unsigned long long)(profile.motion_cycles),
                    (unsigned long long)(profile.compact_cycles),
                    (unsigned)(profile.blocks),
                    (unsigned)(profile.dc_only_blocks),
                    (unsigned)(profile.general_idct_blocks),
                    (unsigned)(profile.motion_macroblocks));
            }
        }
#endif
#if HLV1_ENABLE_STAGE_PROFILE
        if (request.codec == VIDEO_CODEC_kHlv) {
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
        if (request.codec == VIDEO_CODEC_kBpv &&
            result.result == BPV1_OK && request.bpv_prefetch &&
            request.bpv_file) {
            const int64_t read_start = microsNow();
            result.bpv_read_result = bpv_esp32_decoder_read_packet(
                &bpv_decoder, request.bpv_file, &result.bpv_next_packet);
            result.bpv_read_us =
                (uint32_t)(microsNow() - read_start);
        }
        xQueueSend(decode_result_queue, &result, portMAX_DELAY);
    }
}

bool startDecodeWorker() {
    if (!PLAYER_USE_DUAL_CORE_PIPELINE) {
        ESP_LOGI(kTag, "Playback pipeline: single-core sequential mode");
        return true;
    }
    if (decode_task_handle) return true;
    decode_request_queue = xQueueCreate(1, sizeof(DecodeRequest));
    decode_result_queue = xQueueCreate(1, sizeof(DecodeResult));
    if (!decode_request_queue || !decode_result_queue) {
        if (decode_request_queue) vQueueDelete(decode_request_queue);
        if (decode_result_queue) vQueueDelete(decode_result_queue);
        decode_request_queue = NULL;
        decode_result_queue = NULL;
        return false;
    }
    if (xTaskCreatePinnedToCore(decodeTask, "video-decode",
                                kDecodeWorkerStackBytes, NULL, 2,
                                &decode_task_handle, 1) != pdPASS) {
        vQueueDelete(decode_request_queue);
        vQueueDelete(decode_result_queue);
        decode_request_queue = NULL;
        decode_result_queue = NULL;
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
    DecodeRequest request = {0};
    request.codec = VIDEO_CODEC_kHlv;
    request.hlv_file = file;
    request.skip_predictive = video_waiting_for_keyframe;
    if (xQueueSend(decode_request_queue, &request, 0) != pdTRUE) return false;
    decode_in_flight = true;
    return true;
}

bool submitMpegDecode() {
    if (!decode_task_handle || decode_in_flight || !mpeg_video) return false;
    DecodeRequest request = {0};
    request.codec = VIDEO_CODEC_kMpeg1;
    request.skip_predictive = video_waiting_for_keyframe;
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
    DecodeRequest request = {0};
    request.codec = VIDEO_CODEC_kDivx3;
    request.divx3_file = file;
    request.divx3_packet_size = packet_size;
    request.divx3_next_offset = next_offset;
    request.skip_predictive = video_waiting_for_keyframe;
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
    DecodeRequest request = {0};
    request.codec = video_codec;
    request.skip_predictive = video_waiting_for_keyframe;
    if (xQueueSend(decode_request_queue, &request, 0) != pdTRUE) return false;
    decode_in_flight = true;
    return true;
}

bool submitBpvDecode(const BPV1Packet *packet, FILE *file,
                     bool prefetch) {
    if (!decode_task_handle || decode_in_flight || !packet) return false;
    DecodeRequest request = {0};
    request.codec = VIDEO_CODEC_kBpv;
    request.bpv_packet = packet;
    request.bpv_file = file;
    request.bpv_prefetch = prefetch;
    request.skip_predictive = video_waiting_for_keyframe;
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
        DecodeResult ignored = {0};
        waitDecode(&ignored);
    }
    if (decode_task_handle) {
        vTaskDelete(decode_task_handle);
        decode_task_handle = NULL;
    }
    if (decode_request_queue) {
        vQueueDelete(decode_request_queue);
        decode_request_queue = NULL;
    }
    if (decode_result_queue) {
        vQueueDelete(decode_result_queue);
        decode_result_queue = NULL;
    }
    decode_in_flight = false;
}

void bpvInputReaderTask(void *opaque) {
    int result = BPV1_OK;
    (void)opaque;
    while (!bpv_input_stop_requested) {
        const size_t count = fread(
            bpv_input_chunk, 1, kBpvInputChunkBytes, video_file);
        size_t sent = 0;
        while (sent < count && !bpv_input_stop_requested) {
            sent += xStreamBufferSend(
                bpv_input_stream, bpv_input_chunk + sent,
                count - sent, pdMS_TO_TICKS(20));
        }
        if (bpv_input_stop_requested) break;
        if (count != kBpvInputChunkBytes) {
            result = ferror(video_file) ? BPV1_ERR_IO : BPV1_EOF;
            break;
        }
    }
    bpv_input_reader_result = result;
    bpv_input_reader_done = true;
    bpv_input_reader_task_handle = NULL;
    vTaskDelete(NULL);
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
        bpv_input_reader_task_handle = NULL;
        bpv_input_reader_done = true;
    }
    if (bpv_input_stream) {
        vStreamBufferDelete(bpv_input_stream);
        bpv_input_stream = NULL;
    }
    heap_caps_free(bpv_input_chunk);
    bpv_input_chunk = NULL;
    bpv_input_reader_result = BPV1_OK;
}

BpvInputStartResult startBpvInputPrefetch() {
    size_t preroll_bytes;
    int64_t deadline;
    size_t prefetched;

    if (!video_file || bpv_input_stream ||
        bpv_input_reader_task_handle) {
        return BPV_INPUT_START_kInvalidState;
    }
    bpv_input_stream = xStreamBufferCreate(kBpvInputRingBytes, 1);
    bpv_input_chunk = (uint8_t *)heap_caps_malloc(
        kBpvInputChunkBytes, MALLOC_CAP_8BIT);
    if (!bpv_input_stream || !bpv_input_chunk) {
        stopBpvInputPrefetch();
        return BPV_INPUT_START_kNoMemory;
    }
    bpv_input_stop_requested = false;
    bpv_input_reader_done = false;
    bpv_input_reader_result = BPV1_OK;
    if (xTaskCreatePinnedToCore(
            bpvInputReaderTask, "bpv-input-read",
            kBpvInputReaderStackBytes, NULL, 2,
            &bpv_input_reader_task_handle, 1) != pdPASS) {
        bpv_input_reader_done = true;
        stopBpvInputPrefetch();
        return BPV_INPUT_START_kNoMemory;
    }
    preroll_bytes = kBpvInputRingBytes / 2;
    deadline = millisNow() + kBpvInputPrerollTimeoutMs;
    while (xStreamBufferBytesAvailable(bpv_input_stream) <
               preroll_bytes &&
           !bpv_input_reader_done && millisNow() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    prefetched = xStreamBufferBytesAvailable(bpv_input_stream);
    if (!prefetched &&
        (bpv_input_reader_done || millisNow() >= deadline)) {
        stopBpvInputPrefetch();
        return BPV_INPUT_START_kReadFailed;
    }
    ESP_LOGI(
        kTag,
        "BPV input: fixed %u-byte ring, %u-byte refill, "
        "%u bytes prefetched, CPU1 reader",
        (unsigned)kBpvInputRingBytes,
        (unsigned)kBpvInputChunkBytes,
        (unsigned)prefetched);
    return BPV_INPUT_START_kReady;
}

#if MJPEG_INPUT_PREFETCH
static int mapAviDemuxToMjpeg(int result) {
    switch (result) {
        case AVI_DEMUX_OK: return MJPEG_AVI_OK;
        case AVI_DEMUX_EOF: return MJPEG_AVI_EOF;
        case AVI_DEMUX_ERR_IO: return MJPEG_AVI_ERR_IO;
        case AVI_DEMUX_ERR_RANGE: return MJPEG_AVI_ERR_RANGE;
        default: return MJPEG_AVI_ERR_FORMAT;
    }
}

static bool sendMjpegInputPacket(const MjpegInputPacket *packet) {
    while (!mjpeg_input_stop_requested) {
        if (xQueueSend(mjpeg_input_packet_queue, packet,
                       pdMS_TO_TICKS(20)) == pdTRUE) {
            return true;
        }
    }
    return false;
}

static bool sendMjpegInputBytes(const uint8_t *bytes, size_t size) {
    size_t sent = 0U;
    while (sent < size && !mjpeg_input_stop_requested) {
        sent += xStreamBufferSend(
            mjpeg_input_stream, bytes + sent, size - sent,
            pdMS_TO_TICKS(20));
    }
    return sent == size;
}

static void mjpegInputReaderTask(void *opaque) {
    uint8_t chunk[kMjpegInputChunkBytes];
    AviDemuxInfo demux_info = {0};
    (void)opaque;
    demux_info.video.stream_index = mjpeg_info.video_stream;
    demux_info.audio.stream_index = mjpeg_info.audio_stream;
    demux_info.movi_start = mjpeg_info.movi_start;
    demux_info.movi_end = mjpeg_info.movi_end;

    while (!mjpeg_input_stop_requested) {
        AviDemuxPacket avi_packet = {0};
        MjpegInputPacket packet = {0};
        uint32_t command = 0U;
        size_t prefetch_remaining;
        size_t remaining;
        packet.result = mapAviDemuxToMjpeg(
            avi_demux_next_packet(video_file, &demux_info,
                                  AVI_DEMUX_PACKET_VIDEO, &avi_packet));
        if (packet.result != MJPEG_AVI_OK) {
            (void)sendMjpegInputPacket(&packet);
            break;
        }
        if (avi_packet.payload_size < 4U ||
            avi_packet.payload_size > kMjpegMaximumPacketBytes) {
            packet.result = MJPEG_AVI_ERR_RANGE;
            (void)sendMjpegInputPacket(&packet);
            break;
        }
        packet.jpeg_size = avi_packet.payload_size;
        if (!sendMjpegInputPacket(&packet)) break;

        remaining = packet.jpeg_size;
        prefetch_remaining = MIN(remaining,
                                 kMjpegInputSpeculativeBytes);
        while (prefetch_remaining != 0U &&
               !mjpeg_input_stop_requested) {
            const size_t bytes = MIN(prefetch_remaining, sizeof chunk);
            if (fread(chunk, 1, bytes, video_file) != bytes ||
                !sendMjpegInputBytes(chunk, bytes)) {
                remaining = SIZE_MAX;
                break;
            }
            remaining -= bytes;
            prefetch_remaining -= bytes;
        }
        if (remaining == SIZE_MAX) break;

        while (!mjpeg_input_stop_requested &&
               xTaskNotifyWait(0U, UINT32_MAX, &command,
                               pdMS_TO_TICKS(20)) != pdTRUE) {
        }
        if (mjpeg_input_stop_requested ||
            command == kMjpegInputCommandStop) {
            break;
        }
        if (command == kMjpegInputCommandSkip) {
            (void)xStreamBufferReset(mjpeg_input_stream);
            if (avi_demux_finish_packet(video_file, &avi_packet) !=
                AVI_DEMUX_OK) {
                break;
            }
            continue;
        }
        if (command != kMjpegInputCommandDecode) break;

        while (remaining != 0U && !mjpeg_input_stop_requested) {
            const size_t bytes = MIN(remaining, sizeof chunk);
            if (fread(chunk, 1, bytes, video_file) != bytes ||
                !sendMjpegInputBytes(chunk, bytes)) {
                remaining = SIZE_MAX;
                break;
            }
            remaining -= bytes;
        }
        if (remaining != 0U ||
            avi_demux_finish_packet(video_file, &avi_packet) !=
                AVI_DEMUX_OK) {
            break;
        }
    }
    mjpeg_input_reader_finished = true;
    vTaskSuspend(NULL);
}

static size_t readMjpegInput(void *opaque, uint8_t *destination,
                             size_t capacity) {
    (void)opaque;
    while (!mjpeg_input_stop_requested) {
        const size_t bytes = xStreamBufferReceive(
            mjpeg_input_stream, destination, capacity,
            pdMS_TO_TICKS(20));
        if (bytes != 0U) return bytes;
        if (mjpeg_input_reader_finished) return 0U;
    }
    return 0U;
}

static bool startMjpegInputPrefetch() {
    if (mjpeg_input_reader_task_handle != NULL || video_file == NULL) {
        return false;
    }
    mjpeg_input_stop_requested = false;
    mjpeg_input_reader_finished = false;
    mjpeg_input_stream = xStreamBufferCreateStatic(
        kMjpegInputRingBytes, 1U, mjpeg_input_stream_storage,
        &mjpeg_input_stream_state);
    mjpeg_input_packet_queue = xQueueCreateStatic(
        1U, sizeof(MjpegInputPacket), mjpeg_input_packet_queue_storage,
        &mjpeg_input_packet_queue_state);
    if (mjpeg_input_stream == NULL || mjpeg_input_packet_queue == NULL) {
        mjpeg_input_reader_finished = true;
        return false;
    }
    mjpeg_input_reader_task_handle = xTaskCreateStaticPinnedToCore(
        mjpegInputReaderTask, "mjpeg-input", kMjpegInputReaderStackBytes,
        NULL, 2, mjpeg_input_reader_task_stack,
        &mjpeg_input_reader_task_state, 1);
    if (mjpeg_input_reader_task_handle == NULL) {
        mjpeg_input_reader_finished = true;
        return false;
    }
    return true;
}

static bool signalMjpegInputPacket(bool decode) {
    if (mjpeg_input_reader_task_handle == NULL) return false;
    return xTaskNotify(mjpeg_input_reader_task_handle,
                       decode ? kMjpegInputCommandDecode
                              : kMjpegInputCommandSkip,
                       eSetValueWithOverwrite) == pdPASS;
}

static void stopMjpegInputPrefetch() {
    const int64_t deadline =
        millisNow() + kMjpegInputStopTimeoutMs;
    mjpeg_input_stop_requested = true;
    if (mjpeg_input_reader_task_handle != NULL) {
        (void)xTaskNotify(mjpeg_input_reader_task_handle,
                          kMjpegInputCommandStop,
                          eSetValueWithOverwrite);
    }
    while (mjpeg_input_reader_task_handle != NULL &&
           !mjpeg_input_reader_finished && millisNow() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (mjpeg_input_reader_task_handle != NULL) {
        vTaskDelete(mjpeg_input_reader_task_handle);
        mjpeg_input_reader_task_handle = NULL;
    }
    if (mjpeg_input_stream != NULL) {
        vStreamBufferDelete(mjpeg_input_stream);
        mjpeg_input_stream = NULL;
    }
    if (mjpeg_input_packet_queue != NULL) {
        vQueueDelete(mjpeg_input_packet_queue);
        mjpeg_input_packet_queue = NULL;
    }
    mjpeg_input_reader_finished = true;
}

static int nextMjpegInputPacket(mjpeg_avi_packet_t *packet) {
    MjpegInputPacket input_packet = {0};
    if (packet == NULL || mjpeg_input_packet_queue == NULL ||
        xQueueReceive(mjpeg_input_packet_queue, &input_packet,
                      pdMS_TO_TICKS(kMjpegInputWaitTimeoutMs)) != pdTRUE) {
        return MJPEG_AVI_ERR_IO;
    }
    if (input_packet.result != MJPEG_AVI_OK) {
        return input_packet.result;
    }
    memset(packet, 0, sizeof *packet);
    packet->input_read = readMjpegInput;
    packet->jpeg_size = input_packet.jpeg_size;
    return MJPEG_AVI_OK;
}
#else
static bool startMjpegInputPrefetch() { return true; }
static void stopMjpegInputPrefetch() {}
#endif

size_t readBpvPrefetchedInput(
    void *opaque, uint8_t *destination, size_t size) {
    size_t received = 0;
    (void)opaque;
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
    typedef struct Rate {
        double value;
        uint16_t numerator;
        uint16_t denominator;
    } Rate;
    static const Rate rates[] = {
        {24000.0 / 1001.0, 24000, 1001},
        {24.0, 24, 1},
        {25.0, 25, 1},
        {30000.0 / 1001.0, 30000, 1001},
        {30.0, 30, 1},
        {50.0, 50, 1},
        {60000.0 / 1001.0, 60000, 1001},
        {60.0, 60, 1},
    };
    for (size_t index = 0; index < sizeof rates / sizeof rates[0];
         ++index) {
        const Rate *rate = &rates[index];
        if (fabs(fps - rate->value) < 0.01) {
            *numerator = rate->numerator;
            *denominator = rate->denominator;
            return true;
        }
    }
    return false;
}

static inline uint16_t yuvToRgb565(
    int y, int red_add, int green_add, int blue_add) {
    const int luma = yuv_luma[(uint8_t)(y)];
    const int red = clamp8((luma + red_add) >> 8);
    const int green = clamp8((luma + green_add) >> 8);
    const int blue = clamp8((luma + blue_add) >> 8);
    return (uint16_t)(((red & 0xF8) << 8) |
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

static bool drawStatusTextAt(
    const char *text, int x, int y, int scale, bool centered) {
    size_t maximum_length;
    size_t length;
    const int glyph_advance = 6 * scale;
    int width;
    int height;
    uint16_t *pixels;
    int text_x;

    if (!text || !*text || scale < 1) return false;
    if (x < 0 || x >= kScreenWidth) return false;
    const int available_width = centered ? kScreenWidth : kScreenWidth - x;
    maximum_length =
        (size_t)((available_width + scale) / (6 * scale));
    length = MIN(strlen(text), maximum_length);
    width = (int)(length) * glyph_advance - scale;
    height = 7 * scale;
    pixels = cyd_display_acquire_buffer(&display);
    if (!pixels || width <= 0 || width > kScreenWidth ||
        height > cyd_display_rows_per_transfer(&display) ||
        y < 0 || y + height > kScreenHeight) {
        return false;
    }
    fillU16(pixels, kScreenWidth * height, 0x0000);

    text_x = centered ? (kScreenWidth - width) / 2 : x;
    for (size_t index = 0; index < length; ++index) {
        unsigned char character =
            (unsigned char)(text[index]);
        if (character < 0x20 || character > 0x7e) character = '?';
        const uint8_t *columns = kStatusFont[character - 0x20];
        const int glyph_x = (int)(index) * glyph_advance;
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
    return cyd_display_draw_bitmap(
               &display, 0, y, kScreenWidth, height, pixels) == ESP_OK;
}

bool drawStatusText(const char *text, int y, int scale) {
    return drawStatusTextAt(text, 0, y, scale, true);
}

static bool drawStatusTextLeft(const char *text, int y, int scale) {
    return drawStatusTextAt(text, 6, y, scale, false);
}

static void format_upload_value(
    char *output, size_t output_bytes,
    uint32_t bytes, uint32_t divisor) {
    const uint64_t hundredths =
        ((uint64_t)bytes * 100U + divisor / 2U) / divisor;
    if (hundredths >= 10000U) {
        const unsigned rounded = (unsigned)(
            ((uint64_t)bytes + divisor / 2U) / divisor);
        snprintf(output, output_bytes, "%u", rounded);
    } else if (hundredths >= 1000U) {
        const uint64_t tenths =
            ((uint64_t)bytes * 10U + divisor / 2U) / divisor;
        if (tenths >= 1000U) {
            const unsigned rounded = (unsigned)(
                ((uint64_t)bytes + divisor / 2U) / divisor);
            snprintf(output, output_bytes, "%u", rounded);
        } else {
            snprintf(output, output_bytes, "%u.%u",
                     (unsigned)(tenths / 10U),
                     (unsigned)(tenths % 10U));
        }
    } else {
        snprintf(output, output_bytes, "%u.%02u",
                 (unsigned)(hundredths / 100U),
                 (unsigned)(hundredths % 100U));
    }
}

void formatUploadProgress(
    char *text, size_t text_bytes, unsigned percent,
    uint32_t received, uint32_t total) {
    uint32_t divisor;
    const char *unit;
    char completed_value[24] = {0};
    char total_value[24] = {0};

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
    format_upload_value(
        completed_value, sizeof completed_value, received, divisor);
    format_upload_value(
        total_value, sizeof total_value, total, divisor);
    snprintf(text, text_bytes, "%u%% %s/%s%s", percent,
             completed_value, total_value, unit);
}

void drawStatusTitle(const char *title) {
    size_t length;
    int available_rows;
    int scale;
    int height;
    if (!title || !*title) return;
    length = MIN(strlen(title), 52);
    available_rows = cyd_display_rows_per_transfer(&display);
    scale =
        length * 12U <= kScreenWidth && 14 <= available_rows ? 2 : 1;
    height = 7 * scale;
    if (drawStatusText(
            title, (kScreenHeight - height) / 2, scale)) {
        cyd_display_flush(&display);
    }
}

void showStatus(const char *title, const char *detail) {
    esp_rom_printf("S,%s,%s\n", title, detail ? detail : "");
    if (detail) {
        ESP_LOGW(kTag, "%s: %s", title, detail);
    } else {
        ESP_LOGI(kTag, "%s", title);
    }
    const esp_err_t clear_result =
        cyd_display_clear(&display, 0x0000);
    if (clear_result != ESP_OK) {
        ESP_LOGE(kTag, "Could not clear status screen: %s",
                 esp_err_to_name(clear_result));
    } else {
        drawStatusTitle(title);
    }
}

static void showUartSession(const char *command) {
    if (cyd_display_clear(&display, 0x0000) != ESP_OK) return;
    drawStatusText("UART session", 88, 2);
    drawStatusText(command ? command : "TRANSFER", 126, 2);
    cyd_display_flush(&display);
}

static void showSeekStatus(uint32_t position_ms) {
    const uint32_t total_seconds = position_ms / 1000U;
    char position[16] = {0};
    snprintf(position, sizeof position, "%u:%02u",
             (unsigned)(total_seconds / 60U),
             (unsigned)(total_seconds % 60U));
    if (cyd_display_clear(&display, 0x0000) != ESP_OK) return;
    drawStatusText("Seeking to", 82, 2);
    drawStatusText(position, 122, 3);
    cyd_display_flush(&display);
}

void beginUploadProgress(const char *filename, uint32_t total) {
    char progress_text[48] = {0};
    if (cyd_display_set_double_buffered(&display, true) == ESP_OK) {
        upload_progress_scale = kUploadPercentScale;
    } else {
        upload_progress_scale = kUploadPercentFallbackScale;
        ESP_LOGW(
            kTag,
            "Could not allocate the second LCD DMA buffer for "
            "large upload progress text");
    }
    cyd_display_clear(&display, 0x0000);
    formatUploadProgress(
        progress_text, sizeof progress_text, 0U, 0U, total);
    drawStatusText(
        progress_text,
        kUploadBarY - 7 * upload_progress_scale - 12,
        upload_progress_scale);
    uint16_t *pixels = cyd_display_acquire_buffer(&display);
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
    if (cyd_display_draw_bitmap(
            &display, kUploadBarX, kUploadBarY, kUploadBarWidth,
            kUploadBarHeight, pixels) != ESP_OK) {
        ESP_LOGE(kTag, "Could not draw UART upload progress bar");
    }
    drawStatusText(
        filename, kUploadFilenameY, kUploadFilenameScale);
    cyd_display_flush(&display);
    upload_progress_pixels = 0;
    upload_progress_percent = 0;
}

void updateUploadProgress(uint32_t received, uint32_t total, void *opaque) {
    (void)opaque;
    if (!total) return;
    const int inner_width = kUploadBarWidth - 2 * kUploadBarBorder;
    const uint64_t proportional =
        ((uint64_t)(received) * inner_width) / total;
    const int filled = (int)MIN(
        (uint64_t)inner_width, proportional);
    if (filled > upload_progress_pixels) {
        const int changed = filled - upload_progress_pixels;
        const int x = kUploadBarX + kUploadBarBorder +
                      upload_progress_pixels;
        uint16_t *pixels = cyd_display_acquire_buffer(&display);
        if (pixels) {
            const int inner_height =
                kUploadBarHeight - 2 * kUploadBarBorder;
            fillU16(
                pixels, changed * inner_height,
                kUploadBarFillColor);
            if (cyd_display_draw_bitmap(
                    &display, x,
                    kUploadBarY + kUploadBarBorder,
                    changed, inner_height, pixels) == ESP_OK) {
                upload_progress_pixels = filled;
            }
        }
    }

    const unsigned percent = (unsigned)MIN(
        (uint64_t)100U,
        ((uint64_t)(received) * 100U) / total);
    if ((int)percent != upload_progress_percent) {
        char text[48] = {0};
        formatUploadProgress(
            text, sizeof text, percent, received, total);
        if (drawStatusText(
                text,
                kUploadBarY - 7 * upload_progress_scale - 12,
                upload_progress_scale)) {
            upload_progress_percent = (int)percent;
        }
    }
}

bool mountSdCard() {
    if (sd_mounted) return true;

    if (!sd_bus_initialized) {
        spi_bus_config_t bus = {0};
        bus.mosi_io_num = BOARD_SD_MOSI;
        bus.miso_io_num = BOARD_SD_MISO;
        bus.sclk_io_num = BOARD_SD_SCK;
        bus.quadwp_io_num = GPIO_NUM_NC;
        bus.quadhd_io_num = GPIO_NUM_NC;
        bus.data4_io_num = GPIO_NUM_NC;
        bus.data5_io_num = GPIO_NUM_NC;
        bus.data6_io_num = GPIO_NUM_NC;
        bus.data7_io_num = GPIO_NUM_NC;
        bus.max_transfer_sz = HLV_ESP32_STREAM_BUFFER_BYTES;
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
    /* Require CMD59 and CRC16 verification for SD SPI data transfers. */
    host.flags &= ~SDMMC_HOST_FLAG_SPI_IGNORE_DATA_CRC;
    host.slot = SPI3_HOST;
    host.max_freq_khz = PLAYER_SD_CLOCK_KHZ;
    if (sd_dma_aligned_buffer == NULL) {
        sd_dma_aligned_buffer =
            heap_caps_malloc(kSdDmaMinimumBlockBytes, MALLOC_CAP_DMA);
        if (sd_dma_aligned_buffer == NULL) {
            ESP_LOGE(kTag, "Cannot reserve %u-byte SD DMA buffer",
                     (unsigned)kSdDmaMinimumBlockBytes);
            return false;
        }
    }
    /*
     * FatFs can submit unaligned buffers. Keep one sector reserved so SDSPI
     * never has to allocate a temporary DMA bounce buffer while a decoder is
     * using nearly all internal RAM. The host is copied into sd_card.
     */
    host.dma_aligned_buffer = sd_dma_aligned_buffer;

    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SPI3_HOST;
    device.gpio_cs = BOARD_SD_CS;

    esp_vfs_fat_mount_config_t mount = {0};
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
    if (mkdir(PLAYER_VIDEO_DIRECTORY, 0775) != 0 &&
        errno != EEXIST) {
        ESP_LOGE(kTag, "Cannot create %s: errno=%d",
                 PLAYER_VIDEO_DIRECTORY, errno);
        esp_vfs_fat_sdcard_unmount("/sdcard", sd_card);
        sd_card = NULL;
        sd_mounted = false;
        return false;
    }
    ESP_LOGI(kTag, "microSD: SPI3 at %d kHz with DMA",
             PLAYER_SD_CLOCK_KHZ);
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    esp_rom_printf("HLVBARE 1 SD_CRC ENABLED\n");
#endif
    if (!PLAYER_LOG_FRAME_TIMINGS) {
        sdmmc_card_print_info(stdout, sd_card);
    }
    return true;
}

uint32_t readLe32(const uint8_t *bytes) {
    return (uint32_t)(bytes[0]) |
           ((uint32_t)(bytes[1]) << 8) |
           ((uint32_t)(bytes[2]) << 16) |
           ((uint32_t)(bytes[3]) << 24);
}

int queueAudioBytes(const uint8_t *bytes, size_t size) {
    size_t sent = 0;
    while (sent < size && !audio_reader_stop_requested) {
        sent += xStreamBufferSend(
            audio_stream, bytes + sent, size - sent, pdMS_TO_TICKS(20));
        if (audio_rebuffering &&
            xStreamBufferBytesAvailable(audio_stream) >= audio_preroll_bytes) {
            audio_rebuffering = false;
        }
    }
    return HLV1_OK;
}

int prefetchAudioBytes(size_t remaining) {
    while (remaining && !audio_reader_stop_requested) {
        const size_t chunk = MIN(remaining, sizeof audio_read_chunk);
        if (fread(audio_read_chunk, 1, chunk, audio_file) != chunk) {
            return HLV1_ERR_IO;
        }
        queueAudioBytes(audio_read_chunk, chunk);
        remaining -= chunk;
    }
    return HLV1_OK;
}

/* IMA packets are independently decodable but can exceed the 512-byte stdio
   refill. Only a 128-byte compressed chunk is retained while decoded PCM is
   emitted incrementally into the fixed-capacity audio stream. */
int prefetchImaAudioBlock(size_t block_bytes) {
    uint8_t header[IMA_ADPCM_BLOCK_HEADER_SIZE];
    uint8_t compressed[128];
    IMAADPCMState state;
    uint16_t sample_count = 0;
    size_t remaining_samples;
    size_t remaining_bytes;
    uint8_t first_sample[2];
    if (block_bytes < sizeof header ||
        fread(header, 1, sizeof header, audio_file) != sizeof header ||
        ima_adpcm_block_header_read(header, sizeof header, &state,
                                    &sample_count) ||
        ima_adpcm_block_size(sample_count) != block_bytes) {
        return HLV1_ERR_FORMAT;
    }
    first_sample[0] = (uint8_t)state.predictor;
    first_sample[1] = (uint8_t)((uint16_t)(int16_t)state.predictor >> 8);
    queueAudioBytes(first_sample, sizeof first_sample);
    remaining_samples = (size_t)sample_count - 1U;
    remaining_bytes = block_bytes - sizeof header;
    while (remaining_bytes) {
        const size_t chunk = MIN(remaining_bytes, sizeof compressed);
        size_t input;
        size_t output = 0;
        if (fread(compressed, 1, chunk, audio_file) != chunk)
            return HLV1_ERR_IO;
        for (input = 0; input < chunk && remaining_samples; ++input) {
            int16_t decoded = ima_adpcm_decode_nibble(
                &state, compressed[input] & 15U);
            audio_read_chunk[output++] = (uint8_t)decoded;
            audio_read_chunk[output++] =
                (uint8_t)((uint16_t)decoded >> 8);
            --remaining_samples;
            if (remaining_samples) {
                decoded = ima_adpcm_decode_nibble(
                    &state, compressed[input] >> 4);
                audio_read_chunk[output++] = (uint8_t)decoded;
                audio_read_chunk[output++] =
                    (uint8_t)((uint16_t)decoded >> 8);
                --remaining_samples;
            }
        }
        queueAudioBytes(audio_read_chunk, output);
        remaining_bytes -= chunk;
    }
    return remaining_samples ? HLV1_ERR_FORMAT : HLV1_OK;
}

/* Standard mono WAVE_FORMAT_IMA_ADPCM blocks in AVI are 1024 bytes with the
   production FFmpeg profile, larger than both the 512-byte stdio refill and
   the 128-byte compressed working chunk. Decode directly into the bounded
   PCM16 queue instead of retaining a complete AVI audio packet. */
int prefetchImaWavAudioBlock(size_t block_bytes) {
    uint8_t header[IMA_ADPCM_WAV_BLOCK_HEADER_SIZE];
    uint8_t compressed[128];
    IMAADPCMState state;
    const size_t sample_count =
        ima_adpcm_wav_mono_sample_count(block_bytes);
    size_t remaining_samples;
    size_t remaining_bytes;
    uint8_t first_sample[2];
    if (!sample_count ||
        fread(header, 1, sizeof header, audio_file) != sizeof header ||
        ima_adpcm_wav_block_header_read(header, sizeof header, &state)) {
        return HLV1_ERR_FORMAT;
    }
    first_sample[0] = (uint8_t)state.predictor;
    first_sample[1] = (uint8_t)((uint16_t)(int16_t)state.predictor >> 8);
    queueAudioBytes(first_sample, sizeof first_sample);
    remaining_samples = sample_count - 1U;
    remaining_bytes = block_bytes - sizeof header;
    while (remaining_bytes) {
        const size_t chunk = MIN(remaining_bytes, sizeof compressed);
        size_t input;
        size_t output = 0;
        if (fread(compressed, 1, chunk, audio_file) != chunk)
            return HLV1_ERR_IO;
        for (input = 0; input < chunk; ++input) {
            int16_t decoded = ima_adpcm_decode_nibble(
                &state, compressed[input] & 15U);
            audio_read_chunk[output++] = (uint8_t)decoded;
            audio_read_chunk[output++] = (uint8_t)((uint16_t)decoded >> 8);
            decoded = ima_adpcm_decode_nibble(
                &state, compressed[input] >> 4);
            audio_read_chunk[output++] = (uint8_t)decoded;
            audio_read_chunk[output++] = (uint8_t)((uint16_t)decoded >> 8);
            remaining_samples -= 2U;
        }
        queueAudioBytes(audio_read_chunk, output);
        remaining_bytes -= chunk;
    }
    return remaining_samples ? HLV1_ERR_FORMAT : HLV1_OK;
}

int prefetchImaWavAudioChunk(size_t payload_size, size_t block_align) {
    if (!block_align || payload_size % block_align)
        return HLV1_ERR_FORMAT;
    while (payload_size) {
        const int result = prefetchImaWavAudioBlock(block_align);
        if (result != HLV1_OK) return result;
        payload_size -= block_align;
    }
    return HLV1_OK;
}

int prefetchHlvAudioPacket() {
    uint8_t header[HLV1_FRAME_HEADER_SIZE];
    const size_t header_bytes = fread(header, 1, sizeof header, audio_file);
    if (!header_bytes && feof(audio_file)) return HLV1_EOF;
    if (header_bytes != sizeof header) return HLV1_ERR_IO;
    if (memcmp(header, "FRM1", 4)) return HLV1_ERR_FORMAT;

    const uint8_t frame_type = header[4];
    const uint8_t q_y = header[5];
    const uint8_t q_uv = header[6];
    const uint8_t q_shift = header[7];
    const uint32_t bit_length = readLe32(header + 8);
    const uint32_t payload_size = readLe32(header + 12);
    if (frame_type > HLV1_FRAME_REPEAT || !q_y || !q_uv || q_shift > 3 ||
        bit_length > (uint64_t)(payload_size) * 8U) {
        return HLV1_ERR_FORMAT;
    }

    const uint32_t video_bytes = (uint32_t)(
        ((uint64_t)(bit_length) + 7U) / 8U);
    if (video_bytes > payload_size || video_bytes > LONG_MAX ||
        fseek(audio_file, (long)(video_bytes), SEEK_CUR)) {
        return HLV1_ERR_IO;
    }

    const size_t audio_bytes = payload_size - video_bytes;
    return audio_output_codec == HLV1_AUDIO_IMA_ADPCM
        ? prefetchImaAudioBlock(audio_bytes)
        : prefetchAudioBytes(audio_bytes);
}

int prefetchMjpegAudioChunk() {
    uint32_t payload_size = 0;
    const int result =
        mjpeg_avi_next_audio_chunk(audio_file, &mjpeg_info,
                                   &payload_size);
    if (result != MJPEG_AVI_OK) return result;
    const int audio_result = audio_output_codec == HLV1_AUDIO_IMA_ADPCM
        ? prefetchImaWavAudioChunk(
              payload_size, mjpeg_info.audio_block_align)
        : prefetchAudioBytes(payload_size);
    if (audio_result != HLV1_OK) return audio_result;
    if ((payload_size & 1U) && fseek(audio_file, 1, SEEK_CUR)) {
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
    const int audio_result = audio_output_codec == HLV1_AUDIO_IMA_ADPCM
        ? prefetchImaWavAudioChunk(
              payload_size, divx3_info.audio_block_align)
        : prefetchAudioBytes(payload_size);
    if (audio_result != HLV1_OK) return audio_result;
    if ((payload_size & 1U) && fseek(audio_file, 1, SEEK_CUR)) {
        return DIVX3_AVI_ERR_IO;
    }
    return DIVX3_AVI_OK;
}

int prefetchBpvAudioPacket() {
    BPV1FrameInfo info = {0};
    const int result =
        bpv1_frame_info_read(audio_file, &bpv_header, &info);
    if (result != BPV1_OK) return result;
    if (info.frame_bytes > LONG_MAX ||
        fseek(audio_file, (long)(info.frame_bytes),
                   SEEK_CUR)) {
        return BPV1_ERR_IO;
    }
    return audio_output_codec == HLV1_AUDIO_IMA_ADPCM
        ? prefetchImaAudioBlock(info.audio_bytes)
        : prefetchAudioBytes(info.audio_bytes);
}

int prefetchMpegAudioFrame() {
    if (!mpeg_audio) return HLV1_ERR_FORMAT;
    const int64_t decode_start = microsNow();
    plm_samples_t *samples = plm_decode_audio(mpeg_audio);
    const uint32_t decode_us =
        (uint32_t)(microsNow() - decode_start);
    if (!samples) {
        return audio_file && ferror(audio_file)
                   ? HLV1_ERR_IO
                   : HLV1_EOF;
    }
    mpeg_audio_decode_frames = mpeg_audio_decode_frames + 1;
    mpeg_audio_decode_us = mpeg_audio_decode_us + decode_us;
    const uint8_t *pcm = (const uint8_t *)(samples->mono_s16);
    const size_t pcm_bytes = samples->count * sizeof(samples->mono_s16[0]);
    size_t sent = 0;
    while (sent < pcm_bytes && !audio_reader_stop_requested) {
        sent += xStreamBufferSend(
            audio_stream, pcm + sent, pcm_bytes - sent,
            pdMS_TO_TICKS(20));
        if (audio_rebuffering &&
            xStreamBufferBytesAvailable(audio_stream) >=
                audio_preroll_bytes) {
            audio_rebuffering = false;
        }
    }
    if (!plm_prefetch_audio(
            mpeg_audio, kMpegAudioPrefetchThresholdBytes)) {
        return HLV1_ERR_MEMORY;
    }
    return HLV1_OK;
}

int prefetchAmrNbAudioFrame() {
    if (!amrnb_audio_decoder) return AMRNB_3GP_ERR_FORMAT;
    AmrNb3gpFrame frame = {0};
    const int64_t decode_start = microsNow();
    const int result = amrnb_3gp_decoder_decode_next(
        amrnb_audio_decoder, audio_file, &frame);
    const uint32_t decode_us =
        (uint32_t)(microsNow() - decode_start);
    if (result != AMRNB_3GP_OK) return result;
    amrnb_audio_decode_frames = amrnb_audio_decode_frames + 1;
    amrnb_audio_decode_us = amrnb_audio_decode_us + decode_us;

    const int64_t convert_start = microsNow();
    for (uint16_t index = 0; index < frame.sample_count; ++index) {
        amrnb_audio_pcm[index] = (uint8_t)(
            ((int32_t)(frame.samples[index]) + 32768) >> 8);
    }
    amrnb_audio_convert_us =
        amrnb_audio_convert_us +
        (uint32_t)(microsNow() - convert_start);

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
    if (audio_output_codec == HLV1_AUDIO_IMA_ADPCM) {
        uint32_t payload_size = 0;
        const int result = h263_avi_audio_reader_next_chunk(
            h263_avi_audio_reader, audio_file, &payload_size);
        if (result != H263_3GP_OK) return result;
        const int audio_result = prefetchImaWavAudioChunk(
            payload_size, h263_info.audio_block_align);
        if (audio_result != HLV1_OK) return audio_result;
        if ((payload_size & 1U) && fseek(audio_file, 1, SEEK_CUR))
            return H263_3GP_ERR_IO;
        return H263_3GP_OK;
    }
    H263AviPcmFrame frame = {0};
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
    if (video_codec == VIDEO_CODEC_kMpeg1)
        return prefetchMpegAudioFrame();
    if (isPacketVideoCodec(video_codec)) {
        return h263_info.container == H263_CONTAINER_AVI
                   ? prefetchH263AviPcmFrame()
                   : prefetchAmrNbAudioFrame();
    }
    if (video_codec == VIDEO_CODEC_kMjpeg)
        return prefetchMjpegAudioChunk();
    if (video_codec == VIDEO_CODEC_kDivx3)
        return prefetchDivx3AudioChunk();
    if (video_codec == VIDEO_CODEC_kBpv)
        return prefetchBpvAudioPacket();
    return prefetchHlvAudioPacket();
}

void audioReaderTask(void *opaque) {
    (void)opaque;
    int result = HLV1_OK;
    while (!audio_reader_stop_requested) {
        result = prefetchAudioPacket();
        if (result != HLV1_OK) break;
        /* The reader shares core 1 with video decode at higher priority.
           Yield after each refill so sustained slow storage cannot starve
           the video decoder. */
        vTaskDelay(1);
    }
    audio_reader_result = result;
    audio_prefetch_eof = result == HLV1_EOF;
    ESP_LOGI(kTag,
             "Audio reader stopped: result=%d MP2 frames=%u, "
             "stack headroom=%u bytes",
             result, (unsigned)mpeg_audio_decode_frames,
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) *
                        sizeof(StackType_t)));
    audio_reader_task_finished = true;
    vTaskSuspend(NULL);
    vTaskDelete(NULL);
}

static uint32_t audioBiasRampSampleCount(void) {
    const uint64_t scaled =
        (uint64_t)audio_output_sample_rate * kAudioBiasRampMs;
    const uint32_t samples = (uint32_t)((scaled + 999U) / 1000U);
    return MAX(samples, 2U);
}

static void beginAudioBiasRamp(AudioBiasState state) {
    const uint32_t samples = audioBiasRampSampleCount();
    const uint32_t maximum_phase_q16 = UINT32_C(0x80000000);
    audio_bias_ramp_remaining = samples;
    audio_bias_step_q16 =
        (maximum_phase_q16 + samples - 2U) / (samples - 1U);
    audio_bias_phase_q16 =
        state == AUDIO_BIAS_kRampUp ? 0U : maximum_phase_q16;
    /* Publish the state last because the DMA callback consumes these fields. */
    audio_bias_state = state;
}

static void fillAudioBiasSamples(int16_t *pcm, size_t sample_count) {
    const uint32_t maximum_phase_q16 = UINT32_C(0x80000000);
    for (size_t sample = 0; sample < sample_count; ++sample) {
        const AudioBiasState state = audio_bias_state;
        if (state == AUDIO_BIAS_kCentered) {
            memset(pcm + sample, 0,
                   (sample_count - sample) * sizeof(*pcm));
            return;
        }
        if (state == AUDIO_BIAS_kLow) {
            for (; sample < sample_count; ++sample) pcm[sample] = INT16_MIN;
            return;
        }

        const uint32_t phase_q16 = audio_bias_phase_q16;
        pcm[sample] = (int16_t)(INT16_MIN + (int32_t)(phase_q16 >> 16));
        if (audio_bias_ramp_remaining <= 1U) {
            audio_bias_ramp_remaining = 0;
            audio_bias_phase_q16 =
                state == AUDIO_BIAS_kRampUp ? maximum_phase_q16 : 0U;
            audio_bias_state = state == AUDIO_BIAS_kRampUp
                                   ? AUDIO_BIAS_kCentered
                                   : AUDIO_BIAS_kLow;
            continue;
        }

        --audio_bias_ramp_remaining;
        if (state == AUDIO_BIAS_kRampUp) {
            const uint32_t next = phase_q16 + audio_bias_step_q16;
            audio_bias_phase_q16 =
                next < phase_q16 || next > maximum_phase_q16
                    ? maximum_phase_q16
                    : next;
        } else {
            audio_bias_phase_q16 =
                phase_q16 > audio_bias_step_q16
                    ? phase_q16 - audio_bias_step_q16
                    : 0U;
        }
    }
}

bool onAudioPdmSent(i2s_chan_handle_t handle,
                    i2s_event_data_t *event, void *opaque) {
    (void)handle;
    (void)opaque;
    size_t dma_slot = kAudioDmaDescriptors;
    for (size_t slot = 0; slot < kAudioDmaDescriptors; ++slot) {
        if (audio_dma_buffer_keys[slot] == event->dma_buf) {
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
        audio_dma_buffer_keys[dma_slot] = event->dma_buf;
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
    const bool bias_only = audio_bias_state != AUDIO_BIAS_kCentered;
    const size_t sample_bytes =
        audio_output_codec == HLV1_AUDIO_IMA_ADPCM ||
                audio_output_signed_pcm16
            ? 2U
            : 1U;
    const size_t requested_bytes = kAudioDmaSamples * sample_bytes;
    if (!bias_only && audio_started && !audio_rebuffering && audio_stream) {
        received = xStreamBufferReceiveFromISR(
            audio_stream, audio_dma_samples, requested_bytes,
            &task_woken);
        if (!received && !audio_prefetch_eof &&
            audio_reader_result >= HLV1_OK) {
            audio_rebuffering = true;
            audio_rebuffers = audio_rebuffers + 1;
        }
    }
    if (!bias_only && received < requested_bytes) {
        memset(audio_dma_samples + received,
               sample_bytes == 2U ? 0 : 128,
               requested_bytes - received);
        if (audio_started) {
            audio_silence_chunks = audio_silence_chunks + 1;
            if (!audio_prefetch_eof) {
                audio_underrun_samples =
                    audio_underrun_samples +
                    (uint32_t)(
                        (requested_bytes - received) / sample_bytes);
            }
        }
    }

    if (event->size != kAudioDmaBufferBytes || !event->dma_buf) {
        audio_output_failed = true;
    } else {
        int16_t *pcm = (int16_t *)(event->dma_buf);
        if (bias_only) {
            fillAudioBiasSamples(pcm, kAudioDmaSamples);
        } else if (sample_bytes == 2U) {
            memcpy(pcm, audio_dma_samples, kAudioDmaBufferBytes);
        } else {
            for (size_t sample = 0; sample < kAudioDmaSamples; ++sample) {
                pcm[sample] = (int16_t)(
                    ((int32_t)audio_dma_samples[sample] - 128) * 256);
            }
        }
    }
    if (dma_slot < kAudioDmaDescriptors) {
        audio_dma_valid_samples[dma_slot] =
            (uint16_t)(received / sample_bytes);
        audio_pending_samples =
            audio_pending_samples + (uint32_t)(received / sample_bytes);
    }
    return task_woken == pdTRUE;
}

void stopAudio() {
    audio_started = false;
    int64_t bias_shutdown_deadline = 0;
    if (audio_pdm_enabled && !audio_output_failed &&
        audio_output_sample_rate &&
        audio_bias_state == AUDIO_BIAS_kCentered) {
        beginAudioBiasRamp(AUDIO_BIAS_kRampDown);
        const uint32_t queued_samples =
            (kAudioDmaDescriptors + 1U) * kAudioDmaSamples;
        const uint32_t drain_ms =
            (uint32_t)(((uint64_t)queued_samples * 1000U +
                        audio_output_sample_rate - 1U) /
                       audio_output_sample_rate);
        bias_shutdown_deadline =
            millisNow() + kAudioBiasRampMs + drain_ms +
            kAudioBiasRampSettleMs;
    }
    if (audio_reader_task_handle) {
        audio_reader_stop_requested = true;
        const int64_t deadline =
            millisNow() + kAudioReaderStopTimeoutMs;
        while (!audio_reader_task_finished && millisNow() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (!audio_reader_task_finished) {
            ESP_LOGW(kTag, "Audio reader did not stop; deleting it");
        }
        TaskHandle_t task = audio_reader_task_handle;
        audio_reader_task_handle = NULL;
        vTaskDelete(task);
    }
    audio_reader_task_finished = true;
    if (audio_pdm) {
        if (audio_pdm_enabled) {
            while (bias_shutdown_deadline &&
                   millisNow() < bias_shutdown_deadline) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            i2s_channel_disable(audio_pdm);
            audio_pdm_enabled = false;
        }
        i2s_del_channel(audio_pdm);
        audio_pdm = NULL;
    }
    gpio_reset_pin(BOARD_AUDIO_PDM_DATA);
    gpio_set_direction(BOARD_AUDIO_PDM_DATA, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_AUDIO_PDM_DATA, 0);
    if (audio_file) {
        if (mpeg_audio) {
            plm_destroy(mpeg_audio);
            mpeg_audio = NULL;
        }
        if (amrnb_audio_decoder) {
            amrnb_3gp_decoder_destroy(amrnb_audio_decoder);
            amrnb_audio_decoder = NULL;
        }
        fclose(audio_file);
        audio_file = NULL;
    }
    if (h263_avi_audio_reader) {
        h263_avi_pcm_reader_destroy(h263_avi_audio_reader);
        h263_avi_audio_reader = NULL;
    }
    if (audio_stream) {
        vStreamBufferDelete(audio_stream);
        audio_stream = NULL;
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
    mpeg_audio_decode_frames = 0;
    mpeg_audio_decode_us = 0;
    mpeg_audio_convert_us = 0;
    amrnb_audio_decode_frames = 0;
    amrnb_audio_decode_us = 0;
    amrnb_audio_convert_us = 0;
    amrnb_audio_info = (AmrNb3gpInfo){0};
    audio_preroll_bytes = 0;
    audio_output_codec = HLV1_AUDIO_NONE;
    audio_output_signed_pcm16 = false;
    audio_output_sample_rate = 0;
    audio_bias_state = AUDIO_BIAS_kLow;
    audio_bias_phase_q16 = 0;
    audio_bias_step_q16 = 0;
    audio_bias_ramp_remaining = 0;
    consecutive_late_presentations = 0;
    memset(audio_dma_buffer_keys, 0, sizeof audio_dma_buffer_keys);
    memset(audio_dma_valid_samples, 0,
                sizeof audio_dma_valid_samples);
}

bool prepareAudio(HLV1Header header) {
    stopAudio();
    if (!(header.flags & HLV1_FLAG_AUDIO)) {
        ESP_LOGI(kTag, "Audio clock unavailable: video has no audio track");
        return true;
    }
    if (!PLAYER_ENABLE_AUDIO) {
        ESP_LOGI(kTag,
                 "Audio output disabled; using the ESP timer video clock");
        return true;
    }

    if (header.audio_sample_rate < 8000U ||
        header.audio_sample_rate > 48000U) {
        ESP_LOGE(kTag,
                 "Unsupported audio sample rate: %u Hz; expected 8000..48000",
                 (unsigned)header.audio_sample_rate);
        return false;
    }

    audio_output_codec = header.audio_codec;
    audio_output_signed_pcm16 = video_codec == VIDEO_CODEC_kMpeg1;
    audio_output_sample_rate = header.audio_sample_rate;
    const size_t audio_sample_bytes =
        audio_output_codec == HLV1_AUDIO_IMA_ADPCM ||
                audio_output_signed_pcm16
            ? 2U
            : 1U;
    audio_stream = xStreamBufferCreateStatic(
        sizeof audio_stream_storage, kAudioDmaSamples * audio_sample_bytes,
        audio_stream_storage, &audio_stream_state);
    if (!audio_stream) return false;

    audio_file = fopen(active_video_path, "rb");
    if (!audio_file ||
        setvbuf(audio_file,
                     (char *)(audio_read_ahead),
                     _IOFBF, sizeof audio_read_ahead)) {
        stopAudio();
        return false;
    }
    if (video_codec == VIDEO_CODEC_kMpeg1) {
        mpeg_audio = plm_create_with_file(audio_file, 0);
        if (!mpeg_audio) {
            ESP_LOGE(kTag,
                     "MPEG audio decoder allocation failed: heap=%u "
                     "largest=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(
                         MALLOC_CAP_8BIT));
            stopAudio();
            return false;
        }
        plm_set_video_enabled(mpeg_audio, 0);
        const int audio_streams = plm_get_num_audio_streams(mpeg_audio);
        const int audio_rate = plm_get_samplerate(mpeg_audio);
        if (audio_streams < 1 || audio_rate == 0) {
            ESP_LOGE(kTag,
                     "MPEG audio decoder initialization failed: "
                     "streams=%d rate=%d, heap=%u largest=%u",
                     audio_streams, audio_rate,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(
                         MALLOC_CAP_8BIT));
            stopAudio();
            return false;
        }
        if (audio_rate != header.audio_sample_rate) {
            ESP_LOGE(kTag,
                     "MPEG audio metadata mismatch: streams=%d rate=%d, "
                     "expected rate=%u",
                     audio_streams, audio_rate,
                     (unsigned)header.audio_sample_rate);
            stopAudio();
            return false;
        }
    } else if (isPacketVideoCodec(video_codec)) {
        if (h263_info.container == H263_CONTAINER_AVI) {
            H2633gpInfo audio_info = {0};
            h263_avi_audio_reader = h263_avi_pcm_reader_create();
            const int result =
                h263_avi_audio_reader
                    ? h263_avi_pcm_reader_open_from_decoder(
                          h263_avi_audio_reader, h263_decoder,
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
                audio_info.audio_format_tag !=
                    h263_info.audio_format_tag ||
                audio_info.audio_block_align !=
                    h263_info.audio_block_align ||
                audio_info.audio_samples_per_block !=
                    h263_info.audio_samples_per_block ||
                audio_info.audio_channels != 1 ||
                (audio_info.audio_format_tag == 0x11
                     ? header.audio_codec != HLV1_AUDIO_IMA_ADPCM ||
                           audio_info.audio_bits_per_sample != 4
                     : header.audio_codec != HLV1_AUDIO_PCM_U8 ||
                           (audio_info.audio_bits_per_sample != 8 &&
                            audio_info.audio_bits_per_sample != 16))) {
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
    } else if (video_codec == VIDEO_CODEC_kMjpeg) {
        mjpeg_avi_info_t audio_info = {0};
        if (mjpeg_avi_read_info(audio_file, &audio_info) != MJPEG_AVI_OK ||
            audio_info.width != header.width ||
            audio_info.height != header.height ||
            audio_info.fps_num != header.fps_num ||
            audio_info.fps_den != header.fps_den ||
            audio_info.frame_count != header.frame_count ||
            audio_info.audio_sample_rate != header.audio_sample_rate ||
            audio_info.audio_format_tag != mjpeg_info.audio_format_tag ||
            audio_info.audio_block_align != mjpeg_info.audio_block_align ||
            audio_info.audio_samples_per_block !=
                mjpeg_info.audio_samples_per_block ||
            audio_info.audio_channels != 1 ||
            (audio_info.audio_format_tag == 0x11
                 ? header.audio_codec != HLV1_AUDIO_IMA_ADPCM ||
                       audio_info.audio_bits_per_sample != 4
                 : header.audio_codec != HLV1_AUDIO_PCM_U8 ||
                       audio_info.audio_bits_per_sample != 8)) {
            stopAudio();
            return false;
        }
    } else if (video_codec == VIDEO_CODEC_kDivx3) {
        Divx3AviInfo audio_info = {0};
        if (divx3_avi_read_info(audio_file, &audio_info) !=
                DIVX3_AVI_OK ||
            audio_info.width != header.width ||
            audio_info.height != header.height ||
            audio_info.fps_num != header.fps_num ||
            audio_info.fps_den != header.fps_den ||
            audio_info.frame_count != header.frame_count ||
            audio_info.audio_sample_rate != header.audio_sample_rate ||
            audio_info.audio_format_tag != divx3_info.audio_format_tag ||
            audio_info.audio_block_align != divx3_info.audio_block_align ||
            audio_info.audio_samples_per_block !=
                divx3_info.audio_samples_per_block ||
            audio_info.audio_channels != 1 ||
            (audio_info.audio_format_tag == 0x11
                 ? header.audio_codec != HLV1_AUDIO_IMA_ADPCM ||
                       audio_info.audio_bits_per_sample != 4
                 : header.audio_codec != HLV1_AUDIO_PCM_U8 ||
                       audio_info.audio_bits_per_sample != 8)) {
            stopAudio();
            return false;
        }
    } else if (video_codec == VIDEO_CODEC_kBpv) {
        BPV1Header audio_header = {0};
        if (bpv1_header_read(audio_file, &audio_header) != BPV1_OK ||
            audio_header.width != header.width ||
            audio_header.height != header.height ||
            audio_header.fps_num != header.fps_num ||
            audio_header.fps_den != header.fps_den ||
            audio_header.frame_count != header.frame_count ||
            audio_header.audio_sample_rate != header.audio_sample_rate ||
            audio_header.audio_codec != bpv_header.audio_codec ||
            (audio_header.audio_codec != BPV1_AUDIO_PCM_U8 &&
             audio_header.audio_codec != BPV1_AUDIO_IMA_ADPCM) ||
            audio_header.audio_channels != 1) {
            stopAudio();
            return false;
        }
    } else {
        HLV1Header audio_header = {0};
        if (hlv1_header_read(audio_file, &audio_header) != HLV1_OK ||
            audio_header.width != header.width ||
            audio_header.height != header.height ||
            audio_header.fps_num != header.fps_num ||
            audio_header.fps_den != header.fps_den ||
            audio_header.frame_count != header.frame_count ||
            audio_header.audio_sample_rate != header.audio_sample_rate ||
            audio_header.audio_codec != header.audio_codec ||
            (audio_header.audio_codec != HLV1_AUDIO_PCM_U8 &&
             audio_header.audio_codec != HLV1_AUDIO_IMA_ADPCM) ||
            audio_header.audio_channels != 1) {
            stopAudio();
            return false;
        }
    }

    const size_t dma_free =
        heap_caps_get_free_size(MALLOC_CAP_DMA);
    const size_t dma_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    if (dma_free < kAudioDmaMinimumFreeBytes ||
        dma_largest < kAudioDmaBufferBytes) {
        ESP_LOGW(
            kTag,
            "Audio DMA unavailable: free=%u largest=%u, need free>=%u "
            "largest>=%u; using timer clock",
            (unsigned)dma_free, (unsigned)dma_largest,
            (unsigned)kAudioDmaMinimumFreeBytes,
            (unsigned)kAudioDmaBufferBytes);
        stopAudio();
        return false;
    }
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = kAudioDmaDescriptors;
    channel_config.dma_frame_num = kAudioDmaSamples;
    channel_config.auto_clear_after_cb = false;
    channel_config.auto_clear_before_cb = false;
    esp_err_t audio_result =
        i2s_new_channel(&channel_config, &audio_pdm, NULL);
    if (audio_result != ESP_OK) {
        ESP_LOGE(kTag, "I2S PDM channel allocation failed: %s",
                 esp_err_to_name(audio_result));
        stopAudio();
        return false;
    }
    i2s_pdm_tx_config_t pdm_config = {
        .clk_cfg =
            I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(header.audio_sample_rate),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = I2S_GPIO_UNUSED,
            .dout = BOARD_AUDIO_PDM_DATA,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    audio_result = i2s_channel_init_pdm_tx_mode(audio_pdm, &pdm_config);
    if (audio_result != ESP_OK) {
        ESP_LOGE(kTag, "I2S PDM mode initialization failed: %s",
                 esp_err_to_name(audio_result));
        stopAudio();
        return false;
    }
    i2s_event_callbacks_t callbacks = {0};
    callbacks.on_sent = onAudioPdmSent;
    if (i2s_channel_register_event_callback(
            audio_pdm, &callbacks, NULL) != ESP_OK) {
        stopAudio();
        return false;
    }

    beginAudioBiasRamp(AUDIO_BIAS_kRampUp);
    for (size_t descriptor = 0;
         descriptor < kAudioDmaDescriptors;
         ++descriptor) {
        fillAudioBiasSamples((int16_t *)audio_read_chunk,
                             kAudioDmaSamples);
        size_t loaded = 0;
        if (i2s_channel_preload_data(
                audio_pdm, audio_read_chunk, kAudioDmaBufferBytes,
                &loaded) != ESP_OK ||
            loaded != kAudioDmaBufferBytes) {
            stopAudio();
            return false;
        }
    }
    if (i2s_channel_enable(audio_pdm) != ESP_OK) {
        stopAudio();
        return false;
    }
    audio_pdm_enabled = true;
    /* The preloaded DMA stream begins at 0 V and reaches the PDM midpoint
       before any media sample is allowed onto GPIO26. */
    vTaskDelay(pdMS_TO_TICKS(kAudioBiasRampMs +
                            kAudioBiasRampSettleMs));
    audio_enabled = true;

    /* Audio is the master clock. Fill the complete bounded PCM queue before
       starting, and require the same full preroll after an underrun. */
    audio_preroll_bytes = kAudioStreamBytes;

    audio_reader_stop_requested = false;
    audio_prefetch_eof = false;
    audio_reader_result = HLV1_OK;
    const uint32_t audio_reader_stack_bytes =
        video_codec == VIDEO_CODEC_kHlv
            ? kHlvAudioReaderStackBytes
            : video_codec == VIDEO_CODEC_kMpeg1
                  ? kMpegAudioReaderStackBytes
                  : kAudioReaderStackBytes;
    audio_reader_task_finished = false;
    audio_reader_task_handle = xTaskCreateStaticPinnedToCore(
        audioReaderTask, "video-audio-read", audio_reader_stack_bytes,
        NULL, 3, audio_reader_task_stack, &audio_reader_task_state, 1);
    if (!audio_reader_task_handle) {
        ESP_LOGE(kTag,
                 "Audio reader static task creation failed: stack=%u, "
                 "heap=%u largest=%u",
                 (unsigned)audio_reader_stack_bytes,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
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

    i2s_chan_info_t channel_info = {0};
    i2s_channel_get_info(audio_pdm, &channel_info);
    const uint32_t queued_ms =
        (uint32_t)(((uint64_t)kAudioStreamBytes * 1000U) /
                   ((uint64_t)header.audio_sample_rate *
                    audio_sample_bytes));
    ESP_LOGI(kTag,
             "Audio: %s mono %u Hz via I2S PDM GPIO%d data, clock unpinned, "
             "%u Hz carrier, high-SNR divider 13, 100-ms bias ramps, "
             "static %u-byte/%u-ms queue, "
             "%u x %u-sample DMA ring, %u-byte preroll",
             audio_output_signed_pcm16
                 ? "PCM_S16"
                 : audio_output_codec == HLV1_AUDIO_IMA_ADPCM
                       ? "IMA_ADPCM"
                       : "PCM_U8",
             header.audio_sample_rate, BOARD_AUDIO_PDM_DATA,
             (unsigned)(channel_info.bclk_hz),
             (unsigned)(kAudioStreamBytes), (unsigned)queued_ms,
             (unsigned)(kAudioDmaDescriptors),
             (unsigned)(kAudioDmaSamples),
             (unsigned)(prefetched));
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
    stopMjpegInputPrefetch();
    pending_frame_valid = false;
    pending_frame_keyframe = false;
    pending_mpeg_frame_valid = false;
    pending_divx3_frame = (Divx3Frame){0};
    pending_divx3_frame_valid = false;
    pending_h263_frame_valid = false;
    pending_h263_frame = (H2633gpFrame){0};
    pending_h263_decode_us = 0;
    h263_dual_buffered = false;
    h263_row_pipelined = false;
    pending_bpv_frame_valid = false;
    bpv_rgb565_palette_valid = false;
    ready_bpv_packet = (BPV1Packet){0};
    ready_bpv_packet_valid = false;
    bpv_stream_eof = false;
    ready_bpv_read_us = 0;
    pending_read_us = 0;
    pending_decode_us = 0;
    video_waiting_for_keyframe = false;
    keyframe_catchups = 0;
    compressed_predictive_skips = 0;
    stopAudio();
    hlv_esp32_decoder_end(&decoder);
    mjpeg_avi_decoder_end(&mjpeg_decoder);
    mjpeg_info = (mjpeg_avi_info_t){0};
    divx3_decoder_destroy(divx3_decoder);
    divx3_decoder = NULL;
    divx3_info = (Divx3AviInfo){0};
    if (bpv_runtime) {
        bpv_esp32_decoder_end(&bpv_decoder);
        heap_caps_free(bpv_runtime);
        bpv_runtime = NULL;
    }
    if (mpeg_video) {
        plm_destroy(mpeg_video);
        mpeg_video = NULL;
    }
    if (h263_decoder) {
        h263_3gp_decoder_destroy(h263_decoder);
        h263_decoder = NULL;
    }
    h263_info = (H2633gpInfo){0};
    if (video_file) {
        fclose(video_file);
        video_file = NULL;
    }
    heap_caps_free(video_read_ahead);
    video_read_ahead = NULL;
    video_read_ahead_size = 0;
    video_codec = VIDEO_CODEC_kNone;
    active_video_path = NULL;
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
    sd_card = NULL;
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
             (unsigned)(
                 heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             (unsigned)(
                 heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA)),
             (unsigned)(
                 heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
}

static bool isPacketVideoMemoryError(int result) {
    return result == H263_3GP_ERR_MEMORY ||
           result == H263_3GP_ERR_FRAME_MEMORY ||
           result == H263_3GP_ERR_DECODER_MEMORY ||
           result == H263_3GP_ERR_PACKET_MEMORY;
}

static void showVideoOpenFailure(const char *invalid_title,
                                 const char *detail,
                                 bool out_of_memory) {
    const char *title = out_of_memory ? "Not enough RAM" : invalid_title;
    ESP_LOGE(kTag, "%s: %s", title, detail);
    showStatus(title, detail);
    if (out_of_memory) reportHeap(detail);
}

bool isSafeVideoFilename(const char *name) {
    if (!name || !*name || !strcmp(name, ".") ||
        !strcmp(name, "..") || strstr(name, "..")) {
        return false;
    }
    for (const unsigned char *cursor =
             (const unsigned char *)(name);
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
        fopen(PLAYER_VIDEO_SELECTION_PATH, "rb");
    if (!selection) {
        return errno == ENOENT
                   ? SELECTION_READ_kMissingOrInvalid
                   : SELECTION_READ_kIoError;
    }

    char filename[112] = {0};
    const bool read =
        fgets(filename, sizeof filename, selection) != NULL;
    const bool io_error = ferror(selection) != 0;
    const bool close_error = fclose(selection) != 0;
    if (io_error || close_error) return SELECTION_READ_kIoError;
    if (!read) return SELECTION_READ_kMissingOrInvalid;

    char *start = filename;
    while (*start == ' ' || *start == '\t') ++start;
    char *end = start + strlen(start);
    while (end > start &&
           (end[-1] == '\r' || end[-1] == '\n' ||
            end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    *end = '\0';
    if (!isSafeVideoFilename(start)) {
        return SELECTION_READ_kMissingOrInvalid;
    }

    const int written = snprintf(
        selected_video_path, sizeof selected_video_path, "%s/%s",
        PLAYER_VIDEO_DIRECTORY, start);
    return written > 0 &&
                   (size_t)(written) <
                       sizeof selected_video_path
               ? SELECTION_READ_kReady
               : SELECTION_READ_kMissingOrInvalid;
}

static unsigned char asciiLower(unsigned char character) {
    return character >= 'A' && character <= 'Z'
               ? (unsigned char)(character + ('a' - 'A'))
               : character;
}

static int compareFilenames(const char *left, const char *right) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (*a && *b) {
        const unsigned char lower_a = asciiLower(*a);
        const unsigned char lower_b = asciiLower(*b);
        if (lower_a != lower_b) {
            return lower_a < lower_b ? -1 : 1;
        }
        ++a;
        ++b;
    }
    if (*a != *b) return *a ? 1 : -1;
    return strcmp(left, right);
}

static bool filenameHasExtension(const char *name, const char *extension) {
    const size_t name_length = strlen(name);
    const size_t extension_length = strlen(extension);
    if (name_length < extension_length) return false;
    const char *suffix = name + name_length - extension_length;
    for (size_t index = 0; index < extension_length; ++index) {
        if (asciiLower((unsigned char)suffix[index]) !=
            asciiLower((unsigned char)extension[index])) {
            return false;
        }
    }
    return true;
}

static bool isSupportedVideoFilename(const char *name) {
    return isSafeVideoFilename(name) &&
           (filenameHasExtension(name, ".hlv") ||
            filenameHasExtension(name, ".bpv1") ||
            filenameHasExtension(name, ".avi") ||
            filenameHasExtension(name, ".mpg") ||
            filenameHasExtension(name, ".mpeg") ||
            filenameHasExtension(name, ".3gp") ||
            filenameHasExtension(name, ".3gpp"));
}

static bool copyFilename(
    char *destination, size_t destination_bytes, const char *source) {
    const int written =
        snprintf(destination, destination_bytes, "%s", source);
    return written >= 0 && (size_t)written < destination_bytes;
}

static bool insertSortedFilename(
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
        memcpy(filenames[index], filenames[index - 1],
               kBrowserFilenameBytes);
    }
    if (!copyFilename(filenames[position], kBrowserFilenameBytes,
                      filename)) {
        return false;
    }
    *count = new_count;
    return true;
}

static bool insertSortedFilenameTail(
    char filenames[][kBrowserFilenameBytes], size_t *count,
    size_t capacity, const char *filename) {
    for (size_t index = 0; index < *count; ++index) {
        if (compareFilenames(filenames[index], filename) == 0) return true;
    }
    if (*count == capacity) {
        if (compareFilenames(filename, filenames[0]) <= 0) return true;
        for (size_t index = 1; index < *count; ++index) {
            memcpy(filenames[index - 1], filenames[index],
                   kBrowserFilenameBytes);
        }
        --*count;
    }
    return insertSortedFilename(
        filenames, count, capacity, filename);
}

static bool appendBrowserVisible(const char *filename) {
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

static BrowserScanResult scanBrowserFile(bool advance) {
    DIR *directory = opendir(PLAYER_VIDEO_DIRECTORY);
    if (!directory) return BROWSER_SCAN_kIoError;

    char first[kBrowserVisibleFiles][kBrowserFilenameBytes] = {{0}};
    size_t first_count = 0;
    char preceding[kBrowserVisibleFiles - 1][kBrowserFilenameBytes] = {{0}};
    size_t preceding_count = 0;
    char exact[kBrowserFilenameBytes] = {0};
    char following[kBrowserVisibleFiles][kBrowserFilenameBytes] = {{0}};
    size_t following_count = 0;
    bool io_error = false;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno != 0) io_error = true;
            break;
        }
        if (strlen(entry->d_name) >= kBrowserFilenameBytes ||
            !isSupportedVideoFilename(entry->d_name)) {
            continue;
        }

        char path[sizeof selected_video_path] = {0};
        const int written = snprintf(
            path, sizeof path, "%s/%s",
            PLAYER_VIDEO_DIRECTORY, entry->d_name);
        struct stat info = {0};
        if (written < 0 || (size_t)written >= sizeof path ||
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
    if (io_error) return BROWSER_SCAN_kIoError;
    if (!first_count) {
        browser_filename[0] = '\0';
        browser_visible_count = 0;
        return BROWSER_SCAN_kEmpty;
    }

    const bool chose_exact = !advance && exact[0];
    const bool chose_following = !chose_exact && following_count;
    const bool chose_first = !chose_exact && !chose_following;
    const char *chosen = chose_exact
                             ? exact
                             : (chose_following ? following[0] : first[0]);
    if (!copyFilename(
            browser_filename, sizeof browser_filename, chosen)) {
        return BROWSER_SCAN_kIoError;
    }

    browser_visible_count = 0;
    browser_selected_visible_index = 0;
    if (chose_first) {
        for (size_t index = 0; index < first_count; ++index) {
            if (!appendBrowserVisible(first[index])) {
                return BROWSER_SCAN_kIoError;
            }
        }
        return BROWSER_SCAN_kFound;
    }

    char before[kBrowserVisibleFiles - 1][kBrowserFilenameBytes] = {{0}};
    size_t before_count = 0;
    for (size_t index = 0; index < preceding_count; ++index) {
        if (!copyFilename(before[before_count], kBrowserFilenameBytes,
                          preceding[index])) {
            return BROWSER_SCAN_kIoError;
        }
        ++before_count;
    }
    if (chose_following && exact[0] && !insertSortedFilenameTail(
                                            before, &before_count,
                                            kBrowserVisibleFiles - 1,
                                            exact)) {
        return BROWSER_SCAN_kIoError;
    }

    const size_t after_start = chose_following ? 1 : 0;
    const size_t after_count = following_count - after_start;
    size_t after_to_show = MIN((size_t)2, after_count);
    size_t before_to_show = MIN(
        kBrowserVisibleFiles - 1 - after_to_show, before_count);
    if (before_to_show < 2) {
        after_to_show = MIN(
            kBrowserVisibleFiles - 1 - before_to_show, after_count);
    }
    const size_t before_start = before_count - before_to_show;
    for (size_t index = before_start; index < before_count; ++index) {
        if (!appendBrowserVisible(before[index])) {
            return BROWSER_SCAN_kIoError;
        }
    }
    browser_selected_visible_index = browser_visible_count;
    if (!appendBrowserVisible(chosen)) return BROWSER_SCAN_kIoError;
    for (size_t index = 0; index < after_to_show; ++index) {
        if (!appendBrowserVisible(following[after_start + index])) {
            return BROWSER_SCAN_kIoError;
        }
    }
    return BROWSER_SCAN_kFound;
}

static void drawFileBrowser(void) {
    const bool has_file = browser_filename[0] != '\0';
    esp_rom_printf(
        "B,%s\n", has_file ? browser_filename : "NO VIDEO FILES");
    if (cyd_display_clear(&display, 0x0000) != ESP_OK) {
        ESP_LOGE(kTag, "Could not clear file browser");
        return;
    }
    drawStatusText("SD VIDEO FILES", 14, 2);
    if (has_file) {
        for (size_t index = 0; index < browser_visible_count; ++index) {
            char line[kBrowserFilenameBytes + 3] = {0};
            snprintf(line, sizeof line, "%s%.111s",
                     index == browser_selected_visible_index ? "> " : "  ",
                     browser_visible_filenames[index]);
            esp_rom_printf("BF,%u,%s\n", (unsigned)index,
                           browser_visible_filenames[index]);
            drawStatusTextLeft(line, 48 + (int)index * 28, 1);
        }
        drawStatusText("SHORT: NEXT   HOLD: PLAY", 211, 1);
    } else {
        drawStatusText("NO VIDEO FILES", 104, 1);
        drawStatusText("SHORT: RESCAN", 211, 1);
    }
    cyd_display_flush(&display);
}

static bool writeBrowserSelection(void) {
    if (!browser_filename[0]) return false;
    FILE *selection = fopen(PLAYER_VIDEO_SELECTION_PATH, "wb");
    if (!selection) return false;
    const size_t length = strlen(browser_filename);
    bool written =
        fwrite(browser_filename, 1, length, selection) == length &&
        fputc('\n', selection) != EOF && fflush(selection) == 0;
    if (fclose(selection) != 0) written = false;
    return written;
}

static void enterFileBrowser(void) {
    if (!sd_mounted && !mountSdCard()) {
        showStatus("microSD failed", "cannot browse /HLV");
        return;
    }
    closeVideo();
    file_browser_active = true;
    (void)cyd_display_set_double_buffered(&display, false);

    const char *selected_name = strrchr(selected_video_path, '/');
    if (selected_name && selected_name[1]) {
        (void)copyFilename(
            browser_filename, sizeof browser_filename,
            selected_name + 1);
    } else {
        browser_filename[0] = '\0';
    }
    const BrowserScanResult result = scanBrowserFile(false);
    if (result == BROWSER_SCAN_kIoError) {
        showStatus("SD CARD READ ERROR", "cannot list /HLV");
        return;
    }
    drawFileBrowser();
}

static void advanceFileBrowser(void) {
    const BrowserScanResult result = scanBrowserFile(true);
    if (result == BROWSER_SCAN_kIoError) {
        showStatus("SD CARD READ ERROR", "cannot list /HLV");
        return;
    }
    drawFileBrowser();
}

static uint32_t aviFourcc(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a |
           ((uint32_t)(uint8_t)b << 8) |
           ((uint32_t)(uint8_t)c << 16) |
           ((uint32_t)(uint8_t)d << 24);
}

static bool readAviU32(FILE *file, uint32_t *value) {
    uint8_t bytes[4];
    if (fread(bytes, 1, sizeof bytes, file) != sizeof bytes) return false;
    *value = (uint32_t)bytes[0] |
             ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) |
             ((uint32_t)bytes[3] << 24);
    return true;
}

static bool seekPastAviChunk(FILE *file, long data_start, uint32_t size,
                             long limit) {
    const uint64_t next = (uint64_t)(data_start) + size + (size & 1U);
    return data_start >= 0 && limit >= 0 &&
           next <= (uint64_t)limit && next <= LONG_MAX &&
           fseek(file, (long)next, SEEK_SET) == 0;
}

static VideoCodec videoCodecFromAviFourcc(uint32_t handler) {
    if (handler == aviFourcc('H', '2', '6', '3') ||
        handler == aviFourcc('U', '2', '6', '3') ||
        handler == aviFourcc('I', '2', '6', '3')) {
        return VIDEO_CODEC_kH263;
    }
    if (handler == aviFourcc('M', '4', 'S', '2'))
        return VIDEO_CODEC_kMpeg4Simple;
    if (divx3_avi_is_v3_fourcc(handler)) return VIDEO_CODEC_kDivx3;
    if (handler == aviFourcc('M', 'J', 'P', 'G') ||
        handler == aviFourcc('m', 'j', 'p', 'g') ||
        handler == aviFourcc('J', 'P', 'E', 'G')) {
        return VIDEO_CODEC_kMjpeg;
    }
    return VIDEO_CODEC_kNone;
}

static VideoCodec probeAviStreamList(FILE *file, long end,
                                      bool *io_error) {
    while (ftell(file) >= 0 && ftell(file) + 8 <= end) {
        uint32_t id = 0;
        uint32_t size = 0;
        if (!readAviU32(file, &id) || !readAviU32(file, &size)) {
            *io_error = true;
            return VIDEO_CODEC_kNone;
        }
        const long data_start = ftell(file);
        if (data_start < 0 ||
            (uint64_t)data_start + size > (uint64_t)end) {
            return VIDEO_CODEC_kNone;
        }
        if (id == aviFourcc('s', 't', 'r', 'h') && size >= 8) {
            uint32_t type = 0;
            uint32_t handler = 0;
            if (!readAviU32(file, &type) ||
                !readAviU32(file, &handler)) {
                *io_error = true;
                return VIDEO_CODEC_kNone;
            }
            if (type == aviFourcc('v', 'i', 'd', 's')) {
                return videoCodecFromAviFourcc(handler);
            }
        }
        if (!seekPastAviChunk(file, data_start, size, end)) {
            *io_error = true;
            return VIDEO_CODEC_kNone;
        }
    }
    return VIDEO_CODEC_kNone;
}

static VideoCodec probeAviHeaderList(FILE *file, long end,
                                      bool *io_error) {
    while (ftell(file) >= 0 && ftell(file) + 8 <= end) {
        uint32_t id = 0;
        uint32_t size = 0;
        if (!readAviU32(file, &id) || !readAviU32(file, &size)) {
            *io_error = true;
            return VIDEO_CODEC_kNone;
        }
        const long data_start = ftell(file);
        if (data_start < 0 ||
            (uint64_t)data_start + size > (uint64_t)end) {
            return VIDEO_CODEC_kNone;
        }
        if (id == aviFourcc('L', 'I', 'S', 'T') && size >= 4) {
            uint32_t type = 0;
            if (!readAviU32(file, &type)) {
                *io_error = true;
                return VIDEO_CODEC_kNone;
            }
            if (type == aviFourcc('s', 't', 'r', 'l')) {
                const VideoCodec codec = probeAviStreamList(
                    file, data_start + (long)size, io_error);
                if (codec != VIDEO_CODEC_kNone || *io_error) return codec;
            }
        }
        if (!seekPastAviChunk(file, data_start, size, end)) {
            *io_error = true;
            return VIDEO_CODEC_kNone;
        }
    }
    return VIDEO_CODEC_kNone;
}

static VideoCodec probeAviCodec(FILE *file, bool *io_error) {
    uint32_t riff = 0;
    uint32_t riff_size = 0;
    uint32_t type = 0;
    *io_error = false;
    if (fseek(file, 0, SEEK_SET) ||
        !readAviU32(file, &riff) ||
        !readAviU32(file, &riff_size) ||
        !readAviU32(file, &type)) {
        *io_error = true;
        return VIDEO_CODEC_kNone;
    }
    const uint64_t riff_end_u64 = 8ULL + riff_size;
    if (riff != aviFourcc('R', 'I', 'F', 'F') ||
        type != aviFourcc('A', 'V', 'I', ' ') ||
        riff_end_u64 > LONG_MAX) {
        return VIDEO_CODEC_kNone;
    }
    const long riff_end = (long)riff_end_u64;
    while (ftell(file) >= 0 && ftell(file) + 8 <= riff_end) {
        uint32_t id = 0;
        uint32_t size = 0;
        if (!readAviU32(file, &id) || !readAviU32(file, &size)) {
            *io_error = true;
            return VIDEO_CODEC_kNone;
        }
        const long data_start = ftell(file);
        if (data_start < 0 ||
            (uint64_t)data_start + size > riff_end_u64) {
            return VIDEO_CODEC_kNone;
        }
        if (id == aviFourcc('L', 'I', 'S', 'T') && size >= 4) {
            uint32_t list_type = 0;
            if (!readAviU32(file, &list_type)) {
                *io_error = true;
                return VIDEO_CODEC_kNone;
            }
            if (list_type == aviFourcc('h', 'd', 'r', 'l')) {
                const VideoCodec codec = probeAviHeaderList(
                    file, data_start + (long)size, io_error);
                if (codec != VIDEO_CODEC_kNone || *io_error) return codec;
            } else if (list_type == aviFourcc('m', 'o', 'v', 'i')) {
                return VIDEO_CODEC_kNone;
            }
        }
        if (!seekPastAviChunk(file, data_start, size, riff_end)) {
            *io_error = true;
            return VIDEO_CODEC_kNone;
        }
    }
    return VIDEO_CODEC_kNone;
}

VideoOpenResult openVideoCandidate(const char *path) {
    bpv_file_version = 0;
    errno = 0;
    video_file = fopen(path, "rb");
    if (!video_file) {
        return errno == ENOENT
                   ? VIDEO_OPEN_kMissingOrUnsupported
                   : VIDEO_OPEN_kIoError;
    }
    uint8_t signature[12] = {0};
    const size_t signature_size =
        fread(signature, 1, sizeof signature, video_file);
    const bool io_error = ferror(video_file) != 0 ||
                          fseek(video_file, 0, SEEK_SET) != 0;
    if (io_error) {
        fclose(video_file);
        video_file = NULL;
        return VIDEO_OPEN_kIoError;
    }
    if (signature_size >= 4 && !memcmp(signature, "HLV1", 4)) {
        video_codec = VIDEO_CODEC_kHlv;
    } else if (signature_size >= 4 &&
               !memcmp(signature, "BPV1", 4)) {
        video_codec = VIDEO_CODEC_kBpv;
        if (signature_size >= 5) bpv_file_version = signature[4];
    } else if (signature_size == sizeof signature &&
               !memcmp(signature, "RIFF", 4) &&
               !memcmp(signature + 8, "AVI ", 4)) {
        bool avi_io_error = false;
        video_codec = probeAviCodec(video_file, &avi_io_error);
        clearerr(video_file);
        if (fseek(video_file, 0, SEEK_SET)) avi_io_error = true;
        if (avi_io_error) {
            fclose(video_file);
            video_file = NULL;
            return VIDEO_OPEN_kIoError;
        }
        if (video_codec == VIDEO_CODEC_kNone) {
            fclose(video_file);
            video_file = NULL;
            return VIDEO_OPEN_kMissingOrUnsupported;
        }
    } else if (signature_size >= 4 &&
               signature[0] == 0x00 && signature[1] == 0x00 &&
               signature[2] == 0x01 && signature[3] == 0xba) {
        video_codec = VIDEO_CODEC_kMpeg1;
    } else if (signature_size == sizeof signature &&
               !memcmp(signature + 4, "ftyp", 4)) {
        video_codec = VIDEO_CODEC_kH263;
    } else {
        fclose(video_file);
        video_file = NULL;
        return VIDEO_OPEN_kMissingOrUnsupported;
    }
    active_video_path = path;
    return VIDEO_OPEN_kReady;
}

static bool probeMpegDimensions(FILE *file, uint16_t *width,
                                uint16_t *height) {
    if (!file || !width || !height) return false;

    long original_position = ftell(file);
    if (original_position < 0) original_position = 0;
    clearerr(file);
    if (fseek(file, 0, SEEK_SET) != 0) return false;

    uint32_t start_code = UINT32_MAX;
    bool found = false;
    for (size_t offset = 0; offset < kMpegDimensionProbeBytes; ++offset) {
        const int value = fgetc(file);
        if (value == EOF) break;
        start_code = (start_code << 8) | (uint8_t)value;
        if (start_code == 0x000001b3U) {
            uint8_t dimensions[3];
            if (fread(dimensions, 1, sizeof dimensions, file) ==
                sizeof dimensions) {
                const uint16_t parsed_width =
                    (uint16_t)(((uint16_t)dimensions[0] << 4) |
                               (dimensions[1] >> 4));
                const uint16_t parsed_height =
                    (uint16_t)(((uint16_t)(dimensions[1] & 0x0f) << 8) |
                               dimensions[2]);
                if (parsed_width && parsed_height) {
                    *width = parsed_width;
                    *height = parsed_height;
                    found = true;
                }
            }
            break;
        }
    }

    const bool io_error = ferror(file) != 0;
    clearerr(file);
    const bool restored = fseek(file, original_position, SEEK_SET) == 0;
    return found && !io_error && restored;
}

int probeMpegAudioSampleRate(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    plm_t *mpeg = plm_create_with_file(file, 0);
    int sample_rate = 0;
    if (mpeg) {
        plm_set_video_enabled(mpeg, 0);
        if (plm_get_num_audio_streams(mpeg) > 0)
            sample_rate = plm_get_samplerate(mpeg);
        plm_destroy(mpeg);
    }
    fclose(file);
    return sample_rate;
}

int probeAmrNbAudio(const char *path, AmrNb3gpInfo *info) {
    FILE *file = fopen(path, "rb");
    if (!file) return AMRNB_3GP_ERR_IO;
    AmrNb3gpDecoder *probe = amrnb_3gp_decoder_create();
    const int result =
        probe
            ? amrnb_3gp_decoder_open(probe, file, info)
            : AMRNB_3GP_ERR_MEMORY;
    amrnb_3gp_decoder_destroy(probe);
    fclose(file);
    return result;
}

bool reopenVideoAt(long offset) {
    if (!active_video_path || !video_read_ahead ||
        !video_read_ahead_size || offset < 0) {
        return false;
    }
    if (video_file) fclose(video_file);
    video_file = fopen(active_video_path, "rb");
    if (!video_file) return false;
    if (setvbuf(video_file,
                     (char *)(video_read_ahead),
                     _IOFBF, video_read_ahead_size) ||
        fseek(video_file, offset, SEEK_SET)) {
        fclose(video_file);
        video_file = NULL;
        return false;
    }
    return true;
}

bool openVideo() {
    closeVideo();
    const SelectionReadResult selection_result = readSelectedVideoPath();
    if (selection_result == SELECTION_READ_kIoError) {
        showStatus("SD CARD READ ERROR", "cannot read /HLV/play.txt");
        return false;
    }
    if (selection_result != SELECTION_READ_kReady) {
        showStatus("NO SELECTED FILE.",
                   "create /HLV/play.txt");
        return false;
    }
    const VideoOpenResult open_result =
        openVideoCandidate(selected_video_path);
    if (open_result == VIDEO_OPEN_kIoError) {
        showStatus("SD CARD READ ERROR", "cannot open selected video");
        return false;
    }
    if (open_result != VIDEO_OPEN_kReady) {
        showStatus("SELECTED FILE ERROR",
                   "missing or unsupported video");
        return false;
    }
    bool compact_mpeg_display = video_codec == VIDEO_CODEC_kMpeg1;
    if (compact_mpeg_display) {
        uint16_t mpeg_width = 0;
        uint16_t mpeg_height = 0;
        if (probeMpegDimensions(video_file, &mpeg_width, &mpeg_height)) {
            compact_mpeg_display =
                mpeg_width == kScreenWidth && mpeg_height == kScreenHeight;
        } else {
            ESP_LOGW(kTag,
                     "Could not probe MPEG dimensions before allocation; "
                     "using the compact display buffers");
        }
    }
    const bool use_double_display_buffer =
        video_codec != VIDEO_CODEC_kDivx3 &&
        !compact_mpeg_display &&
        !(video_codec == VIDEO_CODEC_kBpv &&
          bpv_file_version >= BPV1_PIXEL_MOTION_VERSION);
    if (cyd_display_set_double_buffered(
            &display, use_double_display_buffer) != ESP_OK) {
        showStatus("Not enough RAM", "display buffer allocation failed");
        closeVideo();
        return false;
    }
    video_read_ahead_size =
        video_codec == VIDEO_CODEC_kMpeg1
            ? kMpegVideoReadAheadBytes
            : (video_codec == VIDEO_CODEC_kDivx3
                   ? kDivx3VideoReadAheadBytes
                   : (isPacketVideoCodec(video_codec)
                          ? kH263VideoReadAheadBytes
                          : (video_codec == VIDEO_CODEC_kBpv &&
                                     bpv_file_version >=
                                         BPV1_PIXEL_MOTION_VERSION
                                 ? kBpvVideoReadAheadBytes
                                 : kVideoReadAheadBytes)));
    video_read_ahead = (uint8_t *)(
        heap_caps_malloc(video_read_ahead_size, MALLOC_CAP_8BIT));
    if (!video_read_ahead) {
        showStatus("Not enough RAM", "read-ahead allocation failed");
        closeVideo();
        return false;
    }
    if (setvbuf(video_file,
                     (char *)(video_read_ahead),
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
            FILE *audio_slot = fopen(active_video_path, "rb");
            if (audio_slot) {
                fclose(audio_slot);
                audio_file_pool_primed = true;
            }
        }
        heap_caps_free(luma_reservations[1]);
        heap_caps_free(luma_reservations[0]);
    }

    sequence_header = (HLV1Header){0};
    reportHeap("before decoder");
    if (video_codec == VIDEO_CODEC_kMpeg1) {
        const int audio_sample_rate =
            probeMpegAudioSampleRate(active_video_path);
        clearerr(video_file);
        errno = 0;
        mpeg_video = plm_create_with_file(video_file, 0);
        if (!mpeg_video) {
            showVideoOpenFailure("Invalid video.mpg",
                                 "MPEG-1 demux allocation failed", true);
            closeVideo();
            return false;
        }
        plm_set_audio_enabled(mpeg_video, 0);
#if PLM_MPEG_STREAM_B_ROWS
        plm_set_video_b_frame_row_callback(
            mpeg_video, consumeMpegBFrameRows, NULL);
#endif
        const int width = plm_get_width(mpeg_video);
        const int height = plm_get_height(mpeg_video);
        const double fps = plm_get_framerate(mpeg_video);
        const int probe_errno = errno;
        const bool probe_io_error = ferror(video_file) != 0;
        const long probe_position = ftell(video_file);
        if (width <= 0 || width > UINT16_MAX ||
            height <= 0 || height > UINT16_MAX ||
            !mpegFpsRational(
                fps, &sequence_header.fps_num,
                &sequence_header.fps_den)) {
            ESP_LOGE(kTag,
                     "MPEG probe failed: %dx%d fps=%.6f "
                     "pos=%ld ferror=%d errno=%d heap=%u largest=%u",
                     width, height, fps, probe_position,
                     probe_io_error ? 1 : 0, probe_errno,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(
                         MALLOC_CAP_8BIT));
            showVideoOpenFailure(
                "Invalid video.mpg",
                probe_errno == ENOMEM
                    ? "MPEG-1 frame allocation failed"
                    : "unsupported MPEG-1 stream",
                probe_errno == ENOMEM);
            closeVideo();
            return false;
        }
        sequence_header.width = (uint16_t)(width);
        sequence_header.height = (uint16_t)(height);
        sequence_header.frame_count = 0;
        if (audio_sample_rate > 0 && audio_sample_rate <= UINT16_MAX) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            /* MPEG PS audio is MP2; this legacy field only marks audio present. */
            sequence_header.audio_codec = HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                (uint16_t)(audio_sample_rate);
            sequence_header.audio_channels = 1;
        }
        plm_rewind(mpeg_video);
        ESP_LOGI(kTag,
                 "MPEG-1/PS: %ux%u, %u/%u fps, streaming frame count, "
                 "MP2 audio=%u Hz, full I/P/B two-reference decoder",
                 sequence_header.width, sequence_header.height,
                 sequence_header.fps_num, sequence_header.fps_den,
                 sequence_header.audio_sample_rate);
        if (!startDecodeWorker()) {
            showVideoOpenFailure("Dual-core init failed",
                                 "CPU1 decoder task allocation failed",
                                 true);
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
            showVideoOpenFailure(
                video_codec == VIDEO_CODEC_kMpeg4Simple
                    ? "Invalid MPEG-4 SP"
                    : "Invalid H.263",
                h263_3gp_codec_strerror(library_codec, result),
                isPacketVideoMemoryError(result));
            closeVideo();
            return false;
        }
        video_codec =
            h263_info.video_codec == H263_VIDEO_CODEC_MPEG4_SIMPLE
                ? VIDEO_CODEC_kMpeg4Simple
                : VIDEO_CODEC_kH263;
        sequence_header.width = h263_info.width;
        sequence_header.height = h263_info.height;
        sequence_header.fps_num =
            (uint16_t)(h263_info.fps_num);
        sequence_header.fps_den =
            (uint16_t)(h263_info.fps_den);
        sequence_header.frame_count = h263_info.frame_count;
        if (h263_info.container == H263_CONTAINER_AVI &&
            h263_info.audio_sample_rate) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            sequence_header.audio_codec = HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                (uint16_t)(h263_info.audio_sample_rate);
            sequence_header.audio_channels = h263_info.audio_channels;
        } else if (h263_info.container == H263_CONTAINER_3GP) {
            AmrNb3gpInfo audio_info = {0};
            const int audio_result =
                probeAmrNbAudio(active_video_path, &audio_info);
            if (audio_result == AMRNB_3GP_OK) {
                sequence_header.flags = HLV1_FLAG_AUDIO;
                sequence_header.audio_codec = HLV1_AUDIO_PCM_U8;
                sequence_header.audio_sample_rate =
                    audio_info.sample_rate;
                sequence_header.audio_channels = audio_info.channels;
            } else if (audio_result != AMRNB_3GP_ERR_UNSUPPORTED) {
                showVideoOpenFailure(
                    "Invalid audio.3gp", amrnb_3gp_strerror(audio_result),
                    audio_result == AMRNB_3GP_ERR_MEMORY);
                closeVideo();
                return false;
            }
        }
        ESP_LOGI(kTag,
                 "%s/%s: %ux%u, %u/%u fps, %u frames, "
                 "profile=%u level=%u, audio=%s %u Hz/%u-bit, "
                 "decoder=%u bytes",
                 packetVideoCodecName(video_codec),
                 h263_info.container == H263_CONTAINER_AVI
                     ? "AVI"
                     : "3GP",
                 sequence_header.width, sequence_header.height,
                 sequence_header.fps_num, sequence_header.fps_den,
                 (unsigned)(sequence_header.frame_count),
                 h263_info.profile, h263_info.level,
                 sequence_header.audio_codec == HLV1_AUDIO_IMA_ADPCM
                     ? "IMA_ADPCM" : "PCM",
                 sequence_header.audio_sample_rate,
                 h263_info.container == H263_CONTAINER_AVI
                     ? h263_info.audio_bits_per_sample
                     : 0,
                 (unsigned)(
                     h263_3gp_decoder_memory_bytes(h263_decoder)));
        h263_dual_buffered =
            h263_3gp_decoder_output_buffer_count(h263_decoder) == 2;
        h263_row_pipelined =
            h263_info.container == H263_CONTAINER_AVI &&
            h263_3gp_decoder_output_buffer_count(h263_decoder) == 1;
        if (h263_row_pipelined) {
            h263_3gp_decoder_set_output_row_guard(
                h263_decoder, waitForH263OutputRow, NULL);
        }
        if ((h263_dual_buffered || h263_row_pipelined) &&
            !startDecodeWorker()) {
            showVideoOpenFailure("Dual-core init failed",
                                 "CPU1 decoder task allocation failed",
                                 true);
            closeVideo();
            return false;
        }
    } else if (video_codec == VIDEO_CODEC_kMjpeg) {
        int result = mjpeg_avi_decoder_begin(
            &mjpeg_decoder, video_file, &mjpeg_info,
            PLAYER_SCALE_VIDEO_TO_DISPLAY);
        if (result == MJPEG_AVI_OK &&
            (mjpeg_info.fps_num > UINT16_MAX ||
             mjpeg_info.fps_den > UINT16_MAX ||
             mjpeg_info.audio_sample_rate > UINT16_MAX)) {
            result = MJPEG_AVI_ERR_RANGE;
        }
        if (result != MJPEG_AVI_OK) {
            showVideoOpenFailure("Invalid video.avi",
                                 mjpeg_avi_strerror(result),
                                 result == MJPEG_AVI_ERR_MEMORY);
            closeVideo();
            return false;
        }
        sequence_header.width = mjpeg_info.width;
        sequence_header.height = mjpeg_info.height;
        sequence_header.fps_num =
            (uint16_t)(mjpeg_info.fps_num);
        sequence_header.fps_den =
            (uint16_t)(mjpeg_info.fps_den);
        sequence_header.frame_count = mjpeg_info.frame_count;
        if (mjpeg_info.audio_stream != 0xff) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            sequence_header.audio_codec =
                mjpeg_info.audio_format_tag == 0x11
                    ? HLV1_AUDIO_IMA_ADPCM : HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                (uint16_t)(mjpeg_info.audio_sample_rate);
            sequence_header.audio_channels = 1;
        }
        ESP_LOGI(kTag,
                 "MJPEG/AVI: %ux%u, %u/%u fps, %u frames, "
                 "%s audio=%u Hz, max JPEG=%u",
                 sequence_header.width, sequence_header.height,
                 sequence_header.fps_num, sequence_header.fps_den,
                 (unsigned)(sequence_header.frame_count),
                 sequence_header.audio_codec == HLV1_AUDIO_IMA_ADPCM
                     ? "IMA_ADPCM" : "PCM_U8",
                 sequence_header.audio_sample_rate,
                 (unsigned)(
                     mjpeg_avi_decoder_compressed_capacity(
                         &mjpeg_decoder)));
    } else if (video_codec == VIDEO_CODEC_kDivx3) {
        int result = divx3_avi_read_info(video_file, &divx3_info);
        const uint32_t macroblocks =
            (((uint32_t)(divx3_info.width) + 15U) / 16U) *
            (((uint32_t)(divx3_info.height) + 15U) / 16U);
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
            showVideoOpenFailure("Invalid DivX 3 AVI",
                                 "DivX 3 decoder allocation failed", true);
            closeVideo();
            return false;
        }
        sequence_header.width = divx3_info.width;
        sequence_header.height = divx3_info.height;
        sequence_header.fps_num =
            (uint16_t)(divx3_info.fps_num);
        sequence_header.fps_den =
            (uint16_t)(divx3_info.fps_den);
        sequence_header.frame_count = divx3_info.frame_count;
        if (divx3_info.audio_stream != 0xff) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            sequence_header.audio_codec =
                divx3_info.audio_format_tag == 0x11
                    ? HLV1_AUDIO_IMA_ADPCM : HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                (uint16_t)(divx3_info.audio_sample_rate);
            sequence_header.audio_channels = 1;
        }
        ESP_LOGI(kTag,
                 "DivX 3/AVI: %ux%u, %u/%u fps, %u frames, "
                 "%s audio=%u Hz, compact decoder=%u bytes, "
                 "4 KB stream buffer, max packet=%u bytes",
                 sequence_header.width, sequence_header.height,
                 sequence_header.fps_num, sequence_header.fps_den,
                 (unsigned)(sequence_header.frame_count),
                 sequence_header.audio_codec == HLV1_AUDIO_IMA_ADPCM
                     ? "IMA_ADPCM" : "PCM_U8",
                 sequence_header.audio_sample_rate,
                 (unsigned)(
                     divx3_decoder_memory_bytes(divx3_decoder)),
                 (unsigned)(
                     divx3_info.max_video_packet_size));
        if (!startDecodeWorker()) {
            showVideoOpenFailure("Dual-core init failed",
                                 "CPU1 decoder task allocation failed",
                                 true);
            closeVideo();
            return false;
        }
    } else if (video_codec == VIDEO_CODEC_kBpv) {
        bpv_runtime = (BpvRuntime *)heap_caps_calloc(
            1, sizeof *bpv_runtime, MALLOC_CAP_8BIT);
        if (!bpv_runtime) {
            showVideoOpenFailure("Invalid video.bpv1",
                                 "BPV runtime allocation failed", true);
            closeVideo();
            return false;
        }
        const int result =
            bpv_esp32_decoder_begin(&bpv_decoder, video_file, &bpv_header);
        if (result != BPV1_OK) {
            showVideoOpenFailure("Invalid video.bpv1",
                                 bpv1_strerror(result),
                                 result == BPV1_ERR_MEMORY);
            closeVideo();
            return false;
        }
        bpv_esp32_decoder_set_profile_clock(
            &bpv_decoder, bpvProfileNowMicros, NULL);
        if (bpv_header.version >= BPV1_PIXEL_MOTION_VERSION &&
            cyd_display_rows_per_transfer(&display) != 8) {
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
        if (bpv_header.audio_codec == BPV1_AUDIO_PCM_U8 ||
            bpv_header.audio_codec == BPV1_AUDIO_IMA_ADPCM) {
            sequence_header.flags = HLV1_FLAG_AUDIO;
            sequence_header.audio_codec =
                bpv_header.audio_codec == BPV1_AUDIO_IMA_ADPCM
                    ? HLV1_AUDIO_IMA_ADPCM : HLV1_AUDIO_PCM_U8;
            sequence_header.audio_sample_rate =
                bpv_header.audio_sample_rate;
            sequence_header.audio_channels = bpv_header.audio_channels;
        }
        ESP_LOGI(kTag,
                 "BPV1 v%u: %ux%u, %u/%u fps, %u frames, "
                 "audio=%u Hz, decoder=%u bytes, packet=%u bytes",
                 bpv_header.version, bpv_header.width, bpv_header.height,
                 bpv_header.fps_num, bpv_header.fps_den,
                 (unsigned)(bpv_header.frame_count),
                 bpv_header.audio_sample_rate,
                 (unsigned)(
                     bpv_esp32_decoder_memory_bytes(&bpv_decoder)),
                 (unsigned)(
                     bpv_esp32_decoder_packet_capacity(&bpv_decoder)));
        if (bpv_header.version >= BPV1_PIXEL_MOTION_VERSION &&
            !PLAYER_ENABLE_BPV_V7_STREAMING_TASK) {
            showStatus("BPV version unsupported",
                       "v7 requires the streaming worker");
            closeVideo();
            return false;
        }
        if (bpv_header.version >= BPV1_PIXEL_MOTION_VERSION) {
            const BpvInputStartResult input_result =
                startBpvInputPrefetch();
            if (input_result != BPV_INPUT_START_kReady) {
                const bool out_of_memory =
                    input_result == BPV_INPUT_START_kNoMemory;
                showVideoOpenFailure(
                    "BPV input init failed",
                    out_of_memory
                        ? "BPV input buffer/task allocation failed"
                        : "cannot prime CPU1 stream buffer",
                    out_of_memory);
                closeVideo();
                return false;
            }
        }
        if (bpv_header.version < BPV1_PIXEL_MOTION_VERSION &&
            !startDecodeWorker()) {
            showVideoOpenFailure("Dual-core init failed",
                                 "CPU1 decoder task allocation failed",
                                 true);
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
                 (unsigned)(sequence_header.frame_count),
                 sequence_header.audio_sample_rate);
        const int decoder_result = hlv_esp32_decoder_begin(
            &decoder, &sequence_header,
            PLAYER_USE_COMPACT_HLV_REFERENCE);
        if (decoder_result != HLV1_OK) {
            showVideoOpenFailure("Invalid video.hlv",
                                 hlv1_strerror(decoder_result),
                                 decoder_result == HLV1_ERR_MEMORY);
            closeVideo();
            return false;
        }
        hlv_esp32_decoder_set_reference_row_guard(
            &decoder, waitForHlvReferenceRows, NULL);
        ESP_LOGI(kTag, "Packet stream buffer: %u bytes, DMA-capable=%u",
                 (unsigned)(
                     hlv_esp32_decoder_stream_buffer_bytes(&decoder)),
                 (unsigned)(
                     hlv_esp32_decoder_dma_buffer(&decoder)));
        // Allocate the large predictive planes and stream buffer before the
        // worker stack, preserving the largest contiguous heap regions.
        if (!startDecodeWorker()) {
            showVideoOpenFailure("Dual-core init failed",
                                 "CPU1 decoder task allocation failed",
                                 true);
            closeVideo();
            return false;
        }
    }
    const bool is_cif_h263 =
        video_codec == VIDEO_CODEC_kH263 &&
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
    // smaller PDM descriptors and audio task stack. This keeps the large
    // internal-RAM allocations immune to audio heap fragmentation.
    if (!prepareAudio(sequence_header)) {
        ESP_LOGW(kTag,
                 "Audio initialization failed; continuing with timer clock");
        stopAudio();
    }
#if MJPEG_INPUT_PREFETCH
    if (video_codec == VIDEO_CODEC_kMjpeg &&
        !startMjpegInputPrefetch()) {
        showVideoOpenFailure("MJPEG input init failed",
                             "cannot create bounded input pipeline", true);
        closeVideo();
        return false;
    }
#endif

    const uint64_t frame_period_numerator =
        1000000ULL * sequence_header.fps_den;
    frame_period_us = (int64_t)(
        frame_period_numerator / sequence_header.fps_num);
    frame_period_remainder = (uint32_t)(
        frame_period_numerator % sequence_header.fps_num);
    frame_period_phase = 0;
    next_present_us = microsNow();
    decoded_frames = 0;
#if MPEG1_RENDER_PROFILE
    memset(&mpeg_render_profile, 0, sizeof(mpeg_render_profile));
#endif
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    bare_benchmark_frames = 0;
    bare_benchmark_read_us = 0;
    bare_benchmark_decode_us = 0;
    bare_benchmark_render_us = 0;
#endif
    dropped_deadlines = 0;
    skipped_presentations = 0;
    consecutive_late_presentations = 0;
    ESP_ERROR_CHECK(cyd_display_clear(&display, 0x0000));

    if (PLAYER_SCALE_VIDEO_TO_DISPLAY) {
        for (int x = 0; x < kScreenWidth; ++x) {
            scaled_x_map[x] = (uint16_t)(
                (x * sequence_header.width) / kScreenWidth);
        }
        for (int y = 0; y < kScreenHeight; ++y) {
            scaled_y_map[y] = (uint16_t)(
                (y * sequence_header.height) / kScreenHeight);
        }
    }

    if (video_codec == VIDEO_CODEC_kMpeg1) {
        ESP_LOGI(kTag,
                 "Playing MPEG-1 in %s mode, frame storage=two YCbCr "
                 "reference frames",
                 PLAYER_SCALE_VIDEO_TO_DISPLAY
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
    } else if (video_codec == VIDEO_CODEC_kMjpeg) {
        ESP_LOGI(kTag,
                 "Playing MJPEG in %s mode, frame storage=RGB565 strip",
                 PLAYER_SCALE_VIDEO_TO_DISPLAY
                     ? "scale-to-320x240"
                     : "native-centred");
    } else if (video_codec == VIDEO_CODEC_kDivx3) {
        ESP_LOGI(kTag,
                 "Playing DivX 3 in %s mode, frame storage=two compact "
                 "Y6/U5/V5 reference frames",
                 PLAYER_SCALE_VIDEO_TO_DISPLAY
                     ? "scale-to-320x240"
                     : "native-centred");
    } else if (video_codec == VIDEO_CODEC_kBpv) {
        ESP_LOGI(kTag,
                 "Playing BPV1 v%u in %s mode, frame storage=%s",
                 bpv_header.version,
                 PLAYER_SCALE_VIDEO_TO_DISPLAY
                     ? "scale-to-320x240"
                     : "native-centred",
                 bpv_header.version >= BPV1_PIXEL_MOTION_VERSION
                     ? "previous RGB565 frame + two 8-row SPI buffers"
                     : "two 4x4-record frames");
    } else {
        ESP_LOGI(kTag, "Playing HLV v%u in %s mode, frame storage=%s%s",
                 sequence_header.version,
                 PLAYER_SCALE_VIDEO_TO_DISPLAY
                     ? "scale-to-320x240"
                     : "native-centred",
                 hlv_esp32_decoder_compact_yuv(&decoder)
                     ? "packed Y7/U6/V6 + per-plane Q4 corrections"
                     : "8-bit YUV 4:2:0",
                 hlv_esp32_decoder_single_reference(&decoder)
                     ? ", one reference + rolling rows"
                     : "");
    }
    reportHeap("decoder ready");
    if (PLAYER_LOG_FRAME_TIMINGS) {
        esp_rom_printf(
            "V,%u,%u,%u,%u,%u,%u\n",
            sequence_header.width, sequence_header.height,
            sequence_header.fps_num, sequence_header.fps_den,
            sequence_header.audio_sample_rate,
            (unsigned)(sequence_header.frame_count));
        esp_rom_printf(
            "#frame,sd_us,decode_us,render_us,work_us,present_us"
            "[,bpv_input_us,bpv_block_us,bpv_reference_us,"
            "bpv_input_calls,bpv_input_bytes]\n");
    }
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    esp_rom_printf(
        "HLVBARE 1 OPEN %u %ux%u %u/%u %u\n",
        (unsigned)(video_codec), sequence_header.width,
        sequence_header.height, sequence_header.fps_num,
        sequence_header.fps_den, (unsigned)(sequence_header.frame_count));
#endif
    return true;
}

static void handleBootButtonEvent(boot_button_event_t event) {
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

static void processBootButtonEvents(void) {
    if (!PLAYER_USE_BOOT_BUTTON_TASK) {
        const uint32_t now_ms = (uint32_t)millisNow();
        if ((int32_t)(now_ms - cooperative_boot_button_next_poll_ms) < 0) {
            return;
        }
        cooperative_boot_button_next_poll_ms =
            now_ms + PLAYER_BOOT_BUTTON_POLL_MS;
        const boot_button_event_t event = boot_button_state_update(
            &cooperative_boot_button_state,
            gpio_get_level(BOARD_BOOT_BUTTON) == 0, now_ms);
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
            esp_rom_delay_us((uint32_t)(remaining));
        }
    }
}

bool renderFrame(const HLV1Frame *frame) {
    const int rows_per_transfer =
        cyd_display_rows_per_transfer(&display);
    if (PLAYER_SCALE_VIDEO_TO_DISPLAY) {
        int cached_source_y = -1;
        for (int y0 = 0; y0 < kScreenHeight; y0 += rows_per_transfer) {
            const int rows =
                MIN(rows_per_transfer, kScreenHeight - y0);
            uint16_t *pixels = cyd_display_acquire_buffer(&display);
            if (!pixels) return false;
            for (int row = 0; row < rows; ++row) {
                const int source_y = scaled_y_map[y0 + row];
                if (source_y != cached_source_y) {
                    convertScaledRow(frame, source_y, scaled_rgb_row);
                    cached_source_y = source_y;
                }
                memcpy(pixels + row * kScreenWidth, scaled_rgb_row,
                            sizeof(uint16_t) * kScreenWidth);
            }
            if (cyd_display_draw_bitmap(
                    &display, 0, y0, kScreenWidth, rows, pixels) != ESP_OK) {
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
        const int rows = MIN(rows_per_transfer, frame->height - y0);
        uint16_t *pixels = cyd_display_acquire_buffer(&display);
        if (!pixels) return false;
        for (int row = 0; row < rows; ++row) {
            convertNativeRow(frame, y0 + row,
                             pixels + row * frame->width);
        }
        if (cyd_display_draw_bitmap(
                &display, x_offset, y_offset + y0, frame->width, rows,
                pixels) != ESP_OK) {
            return false;
        }
        publishHlvRenderedRows(y0 + rows);
    }
    return true;
}

#if COMPACT_YUV_RGB565_FAST_PATH
static y6u5v5_frame_t makeY6U5V5Frame(const plm_frame_t *frame) {
    const y6u5v5_frame_t adapted = {
        {frame->y.width, frame->y.height, frame->y.stride, frame->y.data,
         frame->y.correction_stride, frame->y.correction},
        {frame->cb.width, frame->cb.height, frame->cb.stride, frame->cb.data,
         frame->cb.correction_stride, frame->cb.correction},
        {frame->cr.width, frame->cr.height, frame->cr.stride, frame->cr.data,
         frame->cr.correction_stride, frame->cr.correction}
    };
    return adapted;
}

static bool canConvertCompactYuvRows2(
    const plm_frame_t *frame, int source_y, int first_source_x,
    int output_width) {
    bool supported = false;
    if (frame && frame->storage_mode == PLM_FRAME_STORAGE_Y6_U5_V5) {
        const y6u5v5_frame_t adapted = makeY6U5V5Frame(frame);
        supported = y6u5v5_rgb565_can_convert_rows2(
            &adapted, source_y, first_source_x, output_width) != 0;
    }
#if COMPACT_YUV_RGB565_VERIFY
    static bool reported_geometry = false;
    if (!reported_geometry && frame) {
        esp_rom_printf(
            "CRG,%d,%d,%d,%u,%u,%u,%u,%u,%u,%d\n",
            frame->storage_mode, source_y, first_source_x,
            frame->y.width, frame->y.height,
            frame->cb.width, frame->cb.height,
            frame->cr.width, frame->cr.height, supported);
        reported_geometry = true;
    }
#endif
    return supported;
}

static void convertCompactYuvRows2(
    const plm_frame_t *frame, int source_y, int first_source_x,
    uint16_t *output0, uint16_t *output1, int output_width) {
    const y6u5v5_frame_t adapted = makeY6U5V5Frame(frame);
    y6u5v5_rgb565_convert_rows2(
        &adapted, &y6u5v5_color_tables,
        source_y, first_source_x, output0, output1, output_width);
}
#endif

void convertMpegRow(const plm_frame_t *frame, int source_y,
                    bool scaled, int first_source_x,
                    uint16_t *output, int output_width) {
#if MPEG1_RENDER_PROFILE
    uint32_t profile_start;
#endif
    const uint8_t *y_row =
        frame->y.data + (size_t)(source_y) * frame->y.stride;
    const int chroma_y = source_y >> 1;
    const uint8_t *cb_row =
        frame->cb.data +
        (size_t)(chroma_y) * frame->cb.stride;
    const uint8_t *cr_row =
        frame->cr.data +
        (size_t)(chroma_y) * frame->cr.stride;
    const int chroma_width =
        ((int)(frame->width) + 1) >> 1;
    if (frame->storage_mode == PLM_FRAME_STORAGE_Y6_U5_V5) {
#if MPEG1_RENDER_PROFILE
        profile_start = esp_cpu_get_cycle_count();
#endif
        plm_plane_unpack_compact_samples(
            &frame->y, 0, source_y, 6, native_y_row,
            (int)(frame->width));
#if MPEG1_RENDER_PROFILE
        mpeg_render_profile.y_unpack_cycles +=
            (uint32_t)(esp_cpu_get_cycle_count() - profile_start);
#endif
        y_row = native_y_row;
    }

    if (chroma_y != mpeg_cached_chroma_y) {
        if (frame->storage_mode == PLM_FRAME_STORAGE_Y6_U5_V5) {
#if MPEG1_RENDER_PROFILE
            profile_start = esp_cpu_get_cycle_count();
#endif
            plm_plane_unpack_compact_samples(
                &frame->cb, 0, chroma_y, 5,
                native_u_row, chroma_width);
            plm_plane_unpack_compact_samples(
                &frame->cr, 0, chroma_y, 5,
                native_v_row, chroma_width);
#if MPEG1_RENDER_PROFILE
            mpeg_render_profile.uv_unpack_cycles +=
                (uint32_t)(esp_cpu_get_cycle_count() - profile_start);
#endif
            cb_row = native_u_row;
            cr_row = native_v_row;
        }
#if MPEG1_RENDER_PROFILE
        profile_start = esp_cpu_get_cycle_count();
#endif
        for (int chroma_x = 0; chroma_x < chroma_width; ++chroma_x) {
            const uint8_t cb = cb_row[chroma_x];
            const uint8_t cr = cr_row[chroma_x];
            mpeg_red_add[chroma_x] = yuv_red_add[cr];
            mpeg_green_add[chroma_x] =
                yuv_green_u_add[cb] + yuv_green_v_add[cr];
            mpeg_blue_add[chroma_x] = yuv_blue_add[cb];
        }
#if MPEG1_RENDER_PROFILE
        mpeg_render_profile.chroma_cycles +=
            (uint32_t)(esp_cpu_get_cycle_count() - profile_start);
#endif
        mpeg_cached_chroma_y = chroma_y;
    }

    if (!scaled) {
#if MPEG1_RENDER_PROFILE
        profile_start = esp_cpu_get_cycle_count();
#endif
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
#if MPEG1_RENDER_PROFILE
        mpeg_render_profile.rgb565_cycles +=
            (uint32_t)(esp_cpu_get_cycle_count() - profile_start);
#endif
        return;
    }

#if MPEG1_RENDER_PROFILE
    profile_start = esp_cpu_get_cycle_count();
#endif
    for (int destination_x = 0;
         destination_x < output_width; ++destination_x) {
        const int source_x = scaled_x_map[destination_x];
        const int chroma_x = source_x >> 1;
        output[destination_x] = yuvToRgb565(
            y_row[source_x], mpeg_red_add[chroma_x],
            mpeg_green_add[chroma_x], mpeg_blue_add[chroma_x]);
    }
#if MPEG1_RENDER_PROFILE
    mpeg_render_profile.rgb565_cycles +=
        (uint32_t)(esp_cpu_get_cycle_count() - profile_start);
#endif
}

bool renderMpegFrame(const plm_frame_t *frame) {
    if (!frame) return false;
#if MPEG1_RENDER_PROFILE
    const uint32_t whole_start = esp_cpu_get_cycle_count();
#endif
    const int rows_per_transfer =
        cyd_display_rows_per_transfer(&display);
#if COMPACT_YUV_RGB565_VERIFY
    static bool reported_render_path = false;
    if (!reported_render_path) {
        esp_rom_printf(
            "CRF,%d,%d,%u,%u\n", rows_per_transfer,
            frame->storage_mode, frame->width, frame->height);
        reported_render_path = true;
    }
#endif
    mpeg_cached_chroma_y = -1;
    if (PLAYER_SCALE_VIDEO_TO_DISPLAY) {
        int cached_source_y = -1;
        for (int y0 = 0; y0 < kScreenHeight;
             y0 += rows_per_transfer) {
            const int rows =
                MIN(rows_per_transfer, kScreenHeight - y0);
#if MPEG1_RENDER_PROFILE
            uint32_t profile_start = esp_cpu_get_cycle_count();
#endif
            uint16_t *pixels = cyd_display_acquire_buffer(&display);
#if MPEG1_RENDER_PROFILE
            mpeg_render_profile.acquire_cycles +=
                (uint32_t)(esp_cpu_get_cycle_count() - profile_start);
#endif
            if (!pixels) return false;
            for (int row = 0; row < rows; ++row) {
                const int source_y = scaled_y_map[y0 + row];
                if (source_y != cached_source_y) {
                    convertMpegRow(frame, source_y, true, 0,
                                   scaled_rgb_row, kScreenWidth);
                    cached_source_y = source_y;
                }
                memcpy(
                    pixels + row * kScreenWidth, scaled_rgb_row,
                    sizeof(uint16_t) * kScreenWidth);
            }
#if MPEG1_RENDER_PROFILE
            profile_start = esp_cpu_get_cycle_count();
#endif
            if (cyd_display_draw_bitmap(
                    &display, 0, y0, kScreenWidth, rows, pixels) != ESP_OK) {
                return false;
            }
#if MPEG1_RENDER_PROFILE
            mpeg_render_profile.submit_cycles +=
                (uint32_t)(esp_cpu_get_cycle_count() - profile_start);
            ++mpeg_render_profile.transfers;
#endif
        }
#if MPEG1_RENDER_PROFILE
        mpeg_render_profile.whole_cycles +=
            (uint32_t)(esp_cpu_get_cycle_count() - whole_start);
        ++mpeg_render_profile.frames;
#endif
        return true;
    }

    const int source_width = (int)(frame->width);
    const int source_height = (int)(frame->height);
    const int width = MIN(source_width, kScreenWidth);
    const int height = MIN(source_height, kScreenHeight);
    const int source_x = (source_width - width) / 2;
    const int source_y = (source_height - height) / 2;
    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    for (int y0 = 0; y0 < height; y0 += rows_per_transfer) {
        const int rows = MIN(rows_per_transfer, height - y0);
#if MPEG1_RENDER_PROFILE
        uint32_t profile_start = esp_cpu_get_cycle_count();
#endif
        uint16_t *pixels = cyd_display_acquire_buffer(&display);
#if MPEG1_RENDER_PROFILE
        mpeg_render_profile.acquire_cycles +=
            (uint32_t)(esp_cpu_get_cycle_count() - profile_start);
#endif
        if (!pixels) return false;
        int row = 0;
#if COMPACT_YUV_RGB565_FAST_PATH
        for (; row + 1 < rows; row += 2) {
            const int row_source_y = source_y + y0 + row;
            if (!canConvertCompactYuvRows2(
                    frame, row_source_y, source_x, width)) {
                break;
            }
#if COMPACT_YUV_RGB565_VERIFY
            ++compact_yuv_attempted_pairs;
#endif
#if MPEG1_RENDER_PROFILE
            profile_start = esp_cpu_get_cycle_count();
#endif
            convertCompactYuvRows2(
                frame, row_source_y, source_x,
                pixels + row * width,
                pixels + (row + 1) * width, width);
#if COMPACT_YUV_RGB565_VERIFY
            convertMpegRow(
                frame, row_source_y, false, source_x,
                scaled_rgb_row, width);
            if (memcmp(
                    pixels + row * width, scaled_rgb_row,
                    (size_t)width * sizeof(uint16_t)) != 0) {
                ESP_LOGE(
                    kTag, "compact RGB565 mismatch at source row %d",
                    row_source_y);
                return false;
            }
            convertMpegRow(
                frame, row_source_y + 1, false, source_x,
                scaled_rgb_row, width);
            if (memcmp(
                    pixels + (row + 1) * width, scaled_rgb_row,
                    (size_t)width * sizeof(uint16_t)) != 0) {
                ESP_LOGE(
                    kTag, "compact RGB565 mismatch at source row %d",
                    row_source_y);
                return false;
            }
            ++compact_yuv_verified_pairs;
#endif
#if MPEG1_RENDER_PROFILE
            mpeg_render_profile.fused_rgb565_cycles +=
                (uint32_t)(esp_cpu_get_cycle_count() - profile_start);
#endif
        }
#endif
        for (; row < rows; ++row) {
            convertMpegRow(frame, source_y + y0 + row, false, source_x,
                           pixels + row * width, width);
        }
#if MPEG1_RENDER_PROFILE
        profile_start = esp_cpu_get_cycle_count();
#endif
        if (cyd_display_draw_bitmap(
                &display, x_offset, y_offset + y0, width, rows,
                pixels) != ESP_OK) {
            return false;
        }
#if MPEG1_RENDER_PROFILE
        mpeg_render_profile.submit_cycles +=
            (uint32_t)(esp_cpu_get_cycle_count() - profile_start);
        ++mpeg_render_profile.transfers;
#endif
    }
#if MPEG1_RENDER_PROFILE
    mpeg_render_profile.whole_cycles +=
        (uint32_t)(esp_cpu_get_cycle_count() - whole_start);
    ++mpeg_render_profile.frames;
#endif
    return true;
}

bool renderMpegBRows(
    const plm_frame_t *frame, unsigned first_y, unsigned row_count) {
    if (!frame || !row_count) return false;
    mpeg_cached_chroma_y = -1;
    if (PLAYER_SCALE_VIDEO_TO_DISPLAY) {
        for (int output_y = 0; output_y < kScreenHeight; ++output_y) {
            const int source_y = scaled_y_map[output_y];
            if (source_y < (int)first_y ||
                source_y >= (int)(first_y + row_count)) {
                continue;
            }
            uint16_t *pixels = cyd_display_acquire_buffer(&display);
            if (!pixels) return false;
            convertMpegRow(
                frame, source_y - (int)first_y, true, 0,
                pixels, kScreenWidth);
            if (cyd_display_draw_bitmap(
                    &display, 0, output_y, kScreenWidth, 1,
                    pixels) != ESP_OK) {
                return false;
            }
        }
        return true;
    }

    const int source_width = (int)frame->width;
    const int source_height = (int)frame->height;
    const int width = MIN(source_width, kScreenWidth);
    const int height = MIN(source_height, kScreenHeight);
    const int source_x = (source_width - width) / 2;
    const int source_y = (source_height - height) / 2;
    const int visible_end_y = source_y + height;
    const int absolute_begin =
        (int)first_y > source_y ? (int)first_y : source_y;
    const int absolute_end = MIN(
        (int)(first_y + row_count), visible_end_y);
    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    const int rows_per_transfer =
        cyd_display_rows_per_transfer(&display);
    for (int absolute_y = absolute_begin;
         absolute_y < absolute_end;) {
        const int rows = MIN(
            rows_per_transfer, absolute_end - absolute_y);
        uint16_t *pixels = cyd_display_acquire_buffer(&display);
        if (!pixels) return false;
        for (int row = 0; row < rows; ++row) {
            convertMpegRow(
                frame, absolute_y + row - (int)first_y,
                false, source_x, pixels + row * width, width);
        }
        if (cyd_display_draw_bitmap(
                &display, x_offset,
                y_offset + absolute_y - source_y,
                width, rows, pixels) != ESP_OK) {
            return false;
        }
        absolute_y += rows;
    }
    return true;
}

bool renderH263Frame(const H2633gpFrame *frame) {
    if (!frame) return false;
    plm_frame_t adapted = {0};
    adapted.width = frame->width;
    adapted.height = frame->height;
    if (frame->storage_mode == H263_FRAME_STORAGE_Y6_U5_V5) {
        adapted.storage_mode = PLM_FRAME_STORAGE_Y6_U5_V5;
        adapted.y = (plm_plane_t){
            frame->compact.y.width, frame->compact.y.height,
            (unsigned)(frame->compact.y.stride),
            frame->compact.y.data,
            (unsigned)(frame->compact.y.correction_stride),
            frame->compact.y.correction};
        adapted.cb = (plm_plane_t){
            frame->compact.u.width, frame->compact.u.height,
            (unsigned)(frame->compact.u.stride),
            frame->compact.u.data,
            (unsigned)(frame->compact.u.correction_stride),
            frame->compact.u.correction};
        adapted.cr = (plm_plane_t){
            frame->compact.v.width, frame->compact.v.height,
            (unsigned)(frame->compact.v.stride),
            frame->compact.v.data,
            (unsigned)(frame->compact.v.correction_stride),
            frame->compact.v.correction};
    } else {
        adapted.storage_mode = PLM_FRAME_STORAGE_YUV420;
        adapted.y = (plm_plane_t){
            frame->width, frame->height, frame->y_stride,
            (uint8_t *)(frame->y), 0, NULL};
        adapted.cb = (plm_plane_t){
            (unsigned)(frame->width / 2),
            (unsigned)(frame->height / 2),
            frame->chroma_stride, (uint8_t *)(frame->u), 0, NULL};
        adapted.cr = (plm_plane_t){
            (unsigned)(frame->width / 2),
            (unsigned)(frame->height / 2),
            frame->chroma_stride, (uint8_t *)(frame->v), 0, NULL};
    }
    const int rows_per_transfer =
        cyd_display_rows_per_transfer(&display);
    mpeg_cached_chroma_y = -1;
    const int source_width = (int)(adapted.width);
    const int source_height = (int)(adapted.height);
    const int width = MIN(source_width, kScreenWidth);
    const int height = MIN(source_height, kScreenHeight);
    const int source_x = (source_width - width) / 2;
    const int source_y =
        h263VisibleSourceY(source_width, source_height);
    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    for (int y0 = 0; y0 < height; y0 += rows_per_transfer) {
        const int rows = MIN(rows_per_transfer, height - y0);
        uint16_t *pixels = cyd_display_acquire_buffer(&display);
        if (!pixels) {
            endH263RowPipeline();
            return false;
        }
        int row = 0;
#if COMPACT_YUV_RGB565_FAST_PATH
        for (; row + 1 < rows; row += 2) {
            const int row_source_y = source_y + y0 + row;
            if (!canConvertCompactYuvRows2(
                    &adapted, row_source_y, source_x, width)) {
                break;
            }
#if COMPACT_YUV_RGB565_VERIFY
            ++compact_yuv_attempted_pairs;
#endif
            convertCompactYuvRows2(
                &adapted, row_source_y, source_x,
                pixels + row * width,
                pixels + (row + 1) * width, width);
#if COMPACT_YUV_RGB565_VERIFY
            convertMpegRow(
                &adapted, row_source_y, false, source_x,
                scaled_rgb_row, width);
            if (memcmp(
                    pixels + row * width, scaled_rgb_row,
                    (size_t)width * sizeof(uint16_t)) != 0) {
                ESP_LOGE(
                    kTag, "compact RGB565 mismatch at source row %d",
                    row_source_y);
                return false;
            }
            convertMpegRow(
                &adapted, row_source_y + 1, false, source_x,
                scaled_rgb_row, width);
            if (memcmp(
                    pixels + (row + 1) * width, scaled_rgb_row,
                    (size_t)width * sizeof(uint16_t)) != 0) {
                ESP_LOGE(
                    kTag, "compact RGB565 mismatch at source row %d",
                    row_source_y + 1);
                return false;
            }
            ++compact_yuv_verified_pairs;
#endif
        }
#endif
        for (; row < rows; ++row) {
            convertMpegRow(
                &adapted, source_y + y0 + row, false, source_x,
                pixels + row * width, width);
        }
        publishH263RenderedRows(source_y + y0 + rows);
        if (cyd_display_draw_bitmap(
                &display, x_offset, y_offset + y0, width, rows,
                pixels) != ESP_OK) {
            endH263RowPipeline();
            return false;
        }
    }
    return true;
}

typedef struct MjpegRenderContext {
    uint32_t render_us;
    int next_scaled_y;
    bool display_failed;
} MjpegRenderContext;

uint16_t *acquireMjpegDmaStrip(void *opaque, uint16_t source_y,
                               uint16_t source_rows) {
    MjpegRenderContext *context = (MjpegRenderContext *)(opaque);
    if (!context || !source_rows ||
        source_y + source_rows > mjpeg_info.height) {
        return NULL;
    }
    const int64_t render_start = microsNow();
    uint16_t *pixels = cyd_display_acquire_buffer(&display);
    context->render_us +=
        (uint32_t)(microsNow() - render_start);
    if (!pixels) context->display_failed = true;
    return pixels;
}

bool submitMjpegDmaStrip(void *opaque, const uint16_t *pixels,
                         uint16_t source_y, uint16_t source_rows) {
    MjpegRenderContext *context = (MjpegRenderContext *)(opaque);
    if (!context || !pixels || !source_rows) return false;
    const int64_t render_start = microsNow();
    const int width = mjpeg_info.width;
    const int height = mjpeg_info.height;
    if (source_y + source_rows > height) return false;
    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    if (cyd_display_draw_bitmap(
            &display, x_offset, y_offset + source_y, width,
            source_rows, pixels) != ESP_OK) {
        context->display_failed = true;
        return false;
    }
    context->render_us +=
        (uint32_t)(microsNow() - render_start);
    return true;
}

bool renderMjpegStrip(void *opaque, const uint16_t *strip,
                      uint16_t source_y, uint16_t source_rows) {
    MjpegRenderContext *context = (MjpegRenderContext *)(opaque);
    if (!context || !strip || !source_rows) return false;
    const int64_t render_start = microsNow();
    const int width = mjpeg_info.width;
    const int height = mjpeg_info.height;
    const int rows_per_transfer =
        cyd_display_rows_per_transfer(&display);
    const int source_end = source_y + source_rows;
    if (source_end > height) return false;

    if (PLAYER_SCALE_VIDEO_TO_DISPLAY) {
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
            uint16_t *pixels = cyd_display_acquire_buffer(&display);
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
            if (cyd_display_draw_bitmap(
                    &display, 0, destination_y, kScreenWidth, rows,
                    pixels) != ESP_OK) {
                context->display_failed = true;
                return false;
            }
            context->next_scaled_y += rows;
        }
        context->render_us +=
            (uint32_t)(microsNow() - render_start);
        return true;
    }

    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    uint16_t *pixels = cyd_display_acquire_buffer(&display);
    if (!pixels) {
        context->display_failed = true;
        return false;
    }
    memcpy(pixels, strip,
                (size_t)(width) * source_rows *
                    sizeof(uint16_t));
    if (cyd_display_draw_bitmap(
            &display, x_offset, y_offset + source_y, width,
            source_rows, pixels) != ESP_OK) {
        context->display_failed = true;
        return false;
    }
    context->render_us +=
        (uint32_t)(microsNow() - render_start);
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
    const int rows_per_transfer =
        cyd_display_rows_per_transfer(&display);
    if (PLAYER_SCALE_VIDEO_TO_DISPLAY) {
        int cached_source_y = -1;
        for (int y0 = 0; y0 < kScreenHeight; y0 += rows_per_transfer) {
            const int rows =
                MIN(rows_per_transfer, kScreenHeight - y0);
            uint16_t *pixels = cyd_display_acquire_buffer(&display);
            if (!pixels) return false;
            for (int row = 0; row < rows; ++row) {
                const int source_y = scaled_y_map[y0 + row];
                if (source_y != cached_source_y) {
                    if (bpv1_frame_render_rgb565_row_cached(
                            &bpv_header, frame,
                            (uint16_t)(source_y),
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
            if (cyd_display_draw_bitmap(
                    &display, 0, y0, kScreenWidth, rows, pixels) != ESP_OK) {
                return false;
            }
        }
        return true;
    }

    const int x_offset = (kScreenWidth - width) / 2;
    const int y_offset = (kScreenHeight - height) / 2;
    for (int y0 = 0; y0 < height; y0 += rows_per_transfer) {
        const int rows = MIN(rows_per_transfer, height - y0);
        uint16_t *pixels = cyd_display_acquire_buffer(&display);
        if (!pixels) return false;
        if (bpv1_frame_render_rgb565_rows_cached(
                &bpv_header, frame, (uint16_t)(y0),
                (uint16_t)(rows), bpv_rgb565_palette,
                BPV1_MAX_PALETTE_COLORS, pixels, width,
                (size_t)(width) * rows) != BPV1_OK) {
            return false;
        }
        if (cyd_display_draw_bitmap(
                &display, x_offset, y_offset + y0, width, rows,
                pixels) != ESP_OK) {
            return false;
        }
    }
    return true;
}

void failPlayback(const char *title, int result) {
    const char *detail =
        video_codec == VIDEO_CODEC_kMpeg1
            ? "invalid, truncated or unsupported MPEG-1 stream"
            : isPacketVideoCodec(video_codec)
            ? h263_3gp_codec_strerror(
                  packetVideoLibraryCodec(video_codec), result)
            : video_codec == VIDEO_CODEC_kMjpeg
            ? mjpeg_avi_strerror(result)
            : video_codec == VIDEO_CODEC_kDivx3
            ? (result <= DIVX3_AVI_ERR_ARGUMENT
                   ? divx3_avi_strerror(result)
                   : divx3_strerror(result))
            : video_codec == VIDEO_CODEC_kBpv
                ? bpv1_strerror(result)
                : hlv1_strerror(result);
    ESP_LOGE(kTag, "%s: %s", title, detail);
    showStatus(title, detail);
    closeVideo();
    last_retry_ms = millisNow();
}

void failSdCardRead(const char *detail) {
    const size_t dma_free =
        heap_caps_get_free_size(MALLOC_CAP_DMA);
    const size_t dma_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    if (sd_dma_aligned_buffer == NULL &&
        dma_largest < kSdDmaMinimumBlockBytes) {
        ESP_LOGE(
            kTag,
            "SD read could not allocate DMA memory: free=%u largest=%u: %s",
            (unsigned)dma_free, (unsigned)dma_largest, detail);
        showStatus("Not enough RAM", "SD DMA buffer unavailable");
        closeVideo();
        last_retry_ms = millisNow();
        return;
    }
    if (consecutive_sd_read_failures < UINT32_MAX) {
        ++consecutive_sd_read_failures;
    }
    const bool reinitialize =
        consecutive_sd_read_failures >= kSdReadFailuresBeforeReinit;
    ESP_LOGE(kTag, "SD card read failed (%u/%u): %s",
             (unsigned)(consecutive_sd_read_failures),
             (unsigned)(kSdReadFailuresBeforeReinit), detail);
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
    return ((uint64_t)(frame_index) *
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
            (uint64_t)(audio_played_samples) +
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

typedef bool (*RenderFunction)(const void *);

bool renderHlvOpaque(const void *frame) {
    return renderFrame((const HLV1Frame *)(frame));
}

bool renderBpvOpaque(const void *frame) {
    return renderBpvFrame((const BPV1Frame *)(frame));
}

bool renderMpegOpaque(const void *frame) {
    return renderMpegFrame((const plm_frame_t *)(frame));
}

typedef struct PresentationState {
    int64_t start_us;
    bool render;
    bool seeking;
    bool late;
} PresentationState;

static PresentationState mpeg_b_presentation = {0};
static bool mpeg_b_presentation_active = false;
static bool mpeg_b_render_ok = true;
static uint32_t mpeg_b_render_us = 0;
static uint32_t mpeg_b_callback_us = 0;

typedef struct BpvDecodeBreakdown {
    uint32_t input_us;
    uint32_t block_us;
    uint32_t reference_us;
    uint32_t input_calls;
    uint32_t input_bytes;
} BpvDecodeBreakdown;

PresentationState beginPresentation() {
    PresentationState state = {microsNow(), true, false, false};
    if (seek_fast_forward && decoded_frames < seek_target_frame) {
        state.render = false;
        state.seeking = true;
        return state;
    }
    if (audio_enabled) {
        if (!audio_started) {
            startAudio();
            const uint64_t lead_us =
                ((uint64_t)(kAudioDmaDescriptors - 1) *
                 kAudioDmaSamples * 1000000ULL) /
                sequence_header.audio_sample_rate;
            waitUntil(microsNow() + (int64_t)(lead_us));
        }

        const uint64_t target_samples = frameAudioTarget(decoded_frames);
        const uint64_t frame_samples =
            ((uint64_t)(sequence_header.audio_sample_rate) *
                 sequence_header.fps_den +
             sequence_header.fps_num - 1U) /
            sequence_header.fps_num;
        if (audio_output_failed || audio_reader_result < HLV1_OK) {
            fallBackToTimerClock("Audio clock stopped");
        } else {
            if (!waitForAudioTarget(target_samples)) {
                fallBackToTimerClock("Audio clock stopped");
            } else if (audio_enabled) {
                const uint64_t estimated_position =
                    (uint64_t)(audio_played_samples) +
                    kAudioDmaSamples;
                const uint64_t latest_on_time_sample =
                    target_samples + frame_samples;
                if (estimated_position > latest_on_time_sample)
                    state.late = true;
            }
        }
    }

    if (!audio_enabled) waitUntil(next_present_us);
    return state;
}

void consumeMpegBFrameRows(
    plm_video_t *decoder, const plm_frame_t *rows,
    unsigned first_y, unsigned row_count, void *opaque) {
    (void)decoder;
    (void)opaque;
    const int64_t callback_start = microsNow();
    if (!mpeg_b_presentation_active) {
        mpeg_b_presentation = beginPresentation();
        mpeg_b_presentation_active = true;
        mpeg_b_render_ok = true;
        mpeg_b_render_us = 0;
    }
    if (mpeg_b_presentation.render && mpeg_b_render_ok) {
        const int64_t render_start = microsNow();
        mpeg_b_render_ok = renderMpegBRows(
            rows, first_y, row_count);
        mpeg_b_render_us += (uint32_t)(microsNow() - render_start);
    }
    mpeg_b_callback_us += (uint32_t)(microsNow() - callback_start);
}

void applyKeyframeCatchupWithLookahead(
    PresentationState *state, bool keyframe, bool next_keyframe) {
    if (!state || state->seeking) return;

    if (state->late && !keyframe && next_keyframe) {
        /* BPV exposes the next packet's keyframe flag before decode.
           Prefer spending the remaining display time on that I-frame. */
        state->render = false;
        video_waiting_for_keyframe = true;
        consecutive_late_presentations = 0;
        ++skipped_presentations;
        return;
    }

    if (video_waiting_for_keyframe) {
        if (keyframe) {
            video_waiting_for_keyframe = false;
            consecutive_late_presentations = 0;
            ++keyframe_catchups;
        }
        return;
    }

    if (state->late) {
        if (keyframe) {
            consecutive_late_presentations = 0;
        } else {
            const uint32_t late = consecutive_late_presentations + 1U;
            consecutive_late_presentations = late;
            if (late >= PLAYER_KEYFRAME_CATCHUP_LATE_FRAMES)
                video_waiting_for_keyframe = true;
        }
    } else {
        consecutive_late_presentations = 0;
    }
}

void applyKeyframeCatchup(PresentationState *state, bool keyframe) {
    applyKeyframeCatchupWithLookahead(state, keyframe, false);
}

void applyMjpegCatchup(PresentationState *state) {
    if (!state || state->seeking) return;

    bool skip_decode = state->late;
    if (!audio_enabled) {
        skip_decode = microsNow() - next_present_us > frame_period_us;
    }
    if (!skip_decode) return;

    /* Every MJPEG picture is independently decodable. Drop a late compressed
       picture before JPEG reconstruction; never discard decoded display
       output or wait for a later keyframe. */
    state->render = false;
    ++skipped_presentations;
}

void finishPresentationDetailed(
    PresentationState state, uint32_t read_us,
    uint32_t decode_us, uint32_t render_us,
    const BpvDecodeBreakdown *bpv_breakdown) {
    ++decoded_frames;
    consecutive_sd_read_failures = 0;
#if COMPACT_YUV_RGB565_VERIFY
    if (decoded_frames == 1U || decoded_frames == 60U) {
        esp_rom_printf(
            "CRV,%u,%u,%u\n", (unsigned)decoded_frames,
            (unsigned)compact_yuv_attempted_pairs,
            (unsigned)compact_yuv_verified_pairs);
    }
#endif
#if MPEG1_RENDER_PROFILE
    if (video_codec == VIDEO_CODEC_kMpeg1 && render_us != 0U) {
        mpeg_render_profile.wall_us += render_us;
        ++mpeg_render_profile.wall_frames;
    }
#endif
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    if (!state.seeking) {
        ++bare_benchmark_frames;
        bare_benchmark_read_us += read_us;
        bare_benchmark_decode_us += decode_us;
        bare_benchmark_render_us += render_us;
    }
    if (decoded_frames == 1U && !state.seeking) {
        esp_rom_printf("HLVBARE 1 FIRST_FRAME %u\n",
                       (unsigned)(decoded_frames));
    } else if (decoded_frames == 300U && !state.seeking) {
        esp_rom_printf("HLVBARE 1 FRAME %u\n",
                       (unsigned)(decoded_frames));
        const uint32_t heap_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        esp_rom_printf(
            "HLVBARE 1 RAM free=%u minimum=%u largest=%u "
            "control_stack_free=%u decoder_stack_free=%u\n",
            (unsigned)heap_caps_get_free_size(heap_caps),
            (unsigned)heap_caps_get_minimum_free_size(heap_caps),
            (unsigned)heap_caps_get_largest_free_block(heap_caps),
            (unsigned)uxTaskGetStackHighWaterMark(NULL),
            decode_task_handle
                ? (unsigned)uxTaskGetStackHighWaterMark(decode_task_handle)
                : 0U);
        const uint64_t total_work_us =
            bare_benchmark_read_us + bare_benchmark_decode_us +
            bare_benchmark_render_us;
        esp_rom_printf(
            "HLVBARE 1 SPEED frames=%u read_avg_us=%u "
            "decode_avg_us=%u render_avg_us=%u "
            "decoder_fps_milli=%u work_fps_milli=%u\n",
            (unsigned)bare_benchmark_frames,
            bare_benchmark_frames
                ? (unsigned)(bare_benchmark_read_us /
                             bare_benchmark_frames)
                : 0U,
            bare_benchmark_frames
                ? (unsigned)(bare_benchmark_decode_us /
                             bare_benchmark_frames)
                : 0U,
            bare_benchmark_frames
                ? (unsigned)(bare_benchmark_render_us /
                             bare_benchmark_frames)
                : 0U,
            bare_benchmark_decode_us
                ? (unsigned)(((uint64_t)bare_benchmark_frames *
                              1000000000ULL) /
                             bare_benchmark_decode_us)
                : 0U,
            total_work_us
                ? (unsigned)(((uint64_t)bare_benchmark_frames *
                              1000000000ULL) /
                             total_work_us)
                : 0U);
    }
#endif

    if (state.seeking && audio_enabled && audio_stream) {
        const uint64_t target_samples = frameAudioTarget(decoded_frames);
        uint8_t discard[256];
        while (seek_discarded_audio_samples < target_samples) {
            size_t wanted = (size_t)MIN(
                (uint64_t)(sizeof discard),
                target_samples - seek_discarded_audio_samples);
            size_t received = xStreamBufferReceive(
                audio_stream, discard, wanted, pdMS_TO_TICKS(20));
            if (received == 0U) {
                if (audio_output_failed || audio_reader_result < HLV1_OK ||
                    (audio_prefetch_eof &&
                     xStreamBufferBytesAvailable(audio_stream) == 0U)) {
                    ESP_LOGW(kTag,
                             "Audio seek stopped at %llu/%llu samples",
                             (unsigned long long)(
                                 seek_discarded_audio_samples),
                             (unsigned long long)(target_samples));
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
            audio_played_samples = (uint32_t)MIN(
                seek_discarded_audio_samples, (uint64_t)UINT32_MAX);
        }
        const uint64_t actual_ms =
            ((uint64_t)(decoded_frames) * 1000ULL *
             sequence_header.fps_den) /
            sequence_header.fps_num;
        esp_rom_printf(
            "HLVSEEKDONE 1 %u %llu %u\n",
            (unsigned)(seek_requested_ms),
            (unsigned long long)(actual_ms),
            (unsigned)(decoded_frames));
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
        (uint32_t)(microsNow() - state.start_us);
    const uint32_t work_us = read_us + decode_us + render_us;
    if (PLAYER_LOG_FRAME_TIMINGS && !state.seeking) {
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
                (unsigned)(
                    xStreamBufferBytesAvailable(audio_stream)),
                (unsigned)(audio_pending_samples),
                (unsigned)(audio_played_samples),
                (unsigned)(audio_rebuffers),
                (unsigned)(audio_underrun_samples),
                (unsigned)(audio_silence_chunks),
                0U,
                0U,
                (unsigned)(mpeg_audio_decode_frames),
                (unsigned)(mpeg_audio_decode_us),
                (unsigned)(mpeg_audio_convert_us));
        }
#if MPEG1_RENDER_PROFILE
        if (video_codec == VIDEO_CODEC_kMpeg1 &&
            mpeg_render_profile.frames >= 60U &&
            !mpeg_render_profile.printed) {
            const uint32_t frames = mpeg_render_profile.frames;
            esp_rom_printf(
                "MRP,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u\n",
                (unsigned)frames,
                (unsigned)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
                (unsigned long long)(
                    mpeg_render_profile.wall_us /
                    mpeg_render_profile.wall_frames),
                (unsigned long long)(mpeg_render_profile.whole_cycles /
                                     frames),
                (unsigned long long)(mpeg_render_profile.acquire_cycles /
                                     frames),
                (unsigned long long)(mpeg_render_profile.y_unpack_cycles /
                                     frames),
                (unsigned long long)(mpeg_render_profile.uv_unpack_cycles /
                                     frames),
                (unsigned long long)(mpeg_render_profile.chroma_cycles /
                                     frames),
                (unsigned long long)(mpeg_render_profile.rgb565_cycles /
                                     frames),
                (unsigned long long)(
                    mpeg_render_profile.fused_rgb565_cycles / frames),
                (unsigned long long)(mpeg_render_profile.submit_cycles /
                                     frames),
                (unsigned)(mpeg_render_profile.transfers / frames));
            mpeg_render_profile.printed = true;
        }
#endif
    }
}

void finishPresentation(PresentationState state, uint32_t read_us,
                        uint32_t decode_us, uint32_t render_us) {
    finishPresentationDetailed(
        state, read_us, decode_us, render_us, NULL);
}

bool presentDecodedFrameWithLookahead(
    const void *frame, RenderFunction render_function,
    bool keyframe, bool next_keyframe, uint32_t read_us,
    uint32_t decode_us) {
    PresentationState state = beginPresentation();
    applyKeyframeCatchupWithLookahead(
        &state, keyframe, next_keyframe);
    uint32_t render_us = 0;
    if (state.render) {
        const int64_t render_start = microsNow();
        if (!render_function(frame)) return false;
        render_us = (uint32_t)(microsNow() - render_start);
    }
    finishPresentation(state, read_us, decode_us, render_us);
    return true;
}

bool presentDecodedFrame(const void *frame, RenderFunction render_function,
                         bool keyframe, uint32_t read_us,
                         uint32_t decode_us) {
    return presentDecodedFrameWithLookahead(
        frame, render_function, keyframe, false, read_us, decode_us);
}

bool presentFrame(const HLV1Frame *frame, bool keyframe,
                  uint32_t read_us, uint32_t decode_us) {
    const bool result =
        presentDecodedFrame(frame, renderHlvOpaque, keyframe,
                            read_us, decode_us);
    endHlvRowPipeline();
    return result;
}

bool presentBpvFrame(const BPV1Frame *frame, uint32_t read_us,
                     uint32_t decode_us) {
    return presentDecodedFrame(frame, renderBpvOpaque, frame->keyframe != 0,
                               read_us, decode_us);
}

bool presentBpvFrameWithLookahead(
    const BPV1Frame *frame, bool next_keyframe,
    uint32_t read_us, uint32_t decode_us) {
    return presentDecodedFrameWithLookahead(
        frame, renderBpvOpaque, frame->keyframe != 0,
        next_keyframe, read_us, decode_us);
}

void finishCompressedPredictiveSkip(uint32_t read_us, uint32_t skip_us) {
    PresentationState state = {microsNow(), false, false, true};
    ++skipped_presentations;
    ++compressed_predictive_skips;
    finishPresentation(state, read_us, skip_us, 0);
}

void finishCompressedPredictiveSkips(uint32_t count, uint32_t skip_us) {
    for (uint32_t index = 0; index < count; ++index) {
        finishCompressedPredictiveSkip(0, index == 0 ? skip_us : 0);
    }
}

bool presentMpegFrame(const plm_frame_t *frame, uint32_t decode_us) {
    return presentDecodedFrame(
        frame, renderMpegOpaque,
        frame->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA,
        0, decode_us);
}

bool presentH263Frame(const H2633gpFrame *frame, uint32_t decode_us) {
    PresentationState state = beginPresentation();
    applyKeyframeCatchup(&state, frame->intra != 0);
    uint32_t render_us = 0;
    if (state.render) {
        const int64_t render_start = microsNow();
        if (!renderH263Frame(frame)) return false;
        render_us = (uint32_t)(microsNow() - render_start);
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
            audio_played_samples = (uint32_t)MIN(
                seek_discarded_audio_samples, (uint64_t)UINT32_MAX);
        }
        const uint64_t actual_ms =
            ((uint64_t)(decoded_frames) * 1000ULL *
             sequence_header.fps_den) /
            sequence_header.fps_num;
        esp_rom_printf(
            "HLVSEEKDONE 1 %u %llu %u\n",
            (unsigned)(seek_requested_ms),
            (unsigned long long)(actual_ms),
            (unsigned)(decoded_frames));
    }
    if (audio_enabled) {
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
             "%u keyframe catch-ups, %u compressed P skips, "
             "audio DMA repeats disabled",
             decoded_frames, dropped_deadlines, skipped_presentations,
             (unsigned)(audio_rebuffers),
             (unsigned)(audio_underrun_samples),
             (unsigned)(audio_silence_chunks),
             (unsigned)(keyframe_catchups),
             (unsigned)(compressed_predictive_skips));
    if (!openVideo()) last_retry_ms = millisNow();
}

void playOneFrameSequential() {
    const HLV1Frame *frame = NULL;
    HLV1Packet packet_info = {0};
    bool skipped = false;
    const int64_t decode_start = microsNow();
    const int decode_result = hlv_esp32_decoder_decode_next_catchup(
        &decoder, video_file, &frame, &packet_info, NULL,
        video_waiting_for_keyframe, &skipped);
    const uint32_t decode_us =
        (uint32_t)(microsNow() - decode_start);
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
    if (skipped) {
        finishCompressedPredictiveSkip(0, decode_us);
        return;
    }

    if (!presentFrame(frame,
                      packet_info.frame_type == HLV1_FRAME_KEY,
                      0, decode_us)) {
        failPlayback("Display DMA error", HLV1_ERR_IO);
    }
}

void playOneMpegFrameSequential() {
    const int64_t decode_start = microsNow();
    unsigned compressed_skips = 0;
    plm_frame_t *frame = video_waiting_for_keyframe
                             ? plm_decode_video_keyframe(
                                   mpeg_video, &compressed_skips)
                             : plm_decode_video(mpeg_video);
    const uint32_t decode_us =
        (uint32_t)(microsNow() - decode_start);
    finishCompressedPredictiveSkips(compressed_skips, decode_us);
    if (!frame) {
        if (video_file && ferror(video_file)) {
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
    H2633gpFrame frame = {0};
    int skipped = 0;
    const int64_t decode_start = microsNow();
    const int result = h263_3gp_decoder_decode_next_catchup(
        h263_decoder, video_file, &frame,
        video_waiting_for_keyframe, &skipped);
    const uint32_t decode_us =
        (uint32_t)(microsNow() - decode_start);
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
    if (skipped) {
        finishCompressedPredictiveSkip(0, decode_us);
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
        DecodeResult first = {0};
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
        if (first.skipped) {
            finishCompressedPredictiveSkip(0, first.decode_us);
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
    DecodeResult next = {0};
    const bool received = waitDecode(&next);
    if (h263_row_pipelined) {
        endH263RowPipeline();
        const uint32_t wait_us = __atomic_load_n(
            &h263_row_guard_wait_us, __ATOMIC_RELAXED);
        next.decode_us -= MIN(next.decode_us, wait_us);
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
    if (next.result == H263_3GP_OK && next.skipped) {
        finishCompressedPredictiveSkip(0, next.decode_us);
    } else if (next.result == H263_3GP_OK) {
        pending_h263_frame = next.h263_frame;
        pending_h263_decode_us = next.decode_us;
        pending_h263_frame_valid = true;
    } else {
        finishVideoLoop();
    }
}

void playOneMpegFrameStreamedRows() {
    mpeg_b_presentation_active = false;
    mpeg_b_render_ok = true;
    mpeg_b_render_us = 0;
    mpeg_b_callback_us = 0;
    if (!submitMpegDecode()) {
        failPlayback("Decode pipeline error", HLV1_ERR_IO);
        return;
    }
    DecodeResult decoded = {0};
    if (!waitDecode(&decoded)) {
        failPlayback("MPEG-1 decode error", HLV1_ERR_BITSTREAM);
        return;
    }
    if (decoded.result == HLV1_ERR_IO) {
        failSdCardRead("cannot read MPEG video");
        return;
    }
    if (decoded.result != HLV1_OK) {
        failPlayback("MPEG-1 decode error", decoded.result);
        return;
    }
    finishCompressedPredictiveSkips(
        decoded.compressed_skips, decoded.decode_us);
    if (!decoded.has_mpeg_frame) {
        finishVideoLoop();
        return;
    }
    if (decoded.mpeg_frame.storage_mode ==
        PLM_FRAME_STORAGE_YUV420_ROWS) {
        if (!mpeg_b_presentation_active || !mpeg_b_render_ok) {
            failPlayback("Display DMA error", HLV1_ERR_IO);
            return;
        }
        const uint32_t decode_us = decoded.decode_us - MIN(
            decoded.decode_us, mpeg_b_callback_us);
        finishPresentation(
            mpeg_b_presentation, 0, decode_us,
            mpeg_b_render_us);
        mpeg_b_presentation_active = false;
        return;
    }
    if (!presentMpegFrame(&decoded.mpeg_frame, decoded.decode_us)) {
        failPlayback("Display DMA error", HLV1_ERR_IO);
    }
}

void playOneMpegFramePipelined() {
    if (!pending_mpeg_frame_valid) {
        if (!submitMpegDecode()) {
            failPlayback("Decode pipeline error", HLV1_ERR_IO);
            return;
        }
        DecodeResult first = {0};
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
        finishCompressedPredictiveSkips(
            first.compressed_skips, first.decode_us);
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
    DecodeResult next = {0};
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
    finishCompressedPredictiveSkips(
        next.compressed_skips, next.decode_us);
    if (next.has_mpeg_frame) {
        pending_mpeg_frame = next.mpeg_frame;
        pending_mpeg_decode_us = next.decode_us;
        pending_mpeg_frame_valid = true;
    } else {
        finishVideoLoop();
    }
}

void playOneMjpegFrame() {
    mjpeg_avi_packet_t packet = {0};
    const int64_t read_start = microsNow();
#if MJPEG_INPUT_PREFETCH
    int packet_result = nextMjpegInputPacket(&packet);
#else
    int packet_result = mjpeg_avi_decoder_read_packet(
        &mjpeg_decoder, video_file, &packet);
    if (packet_result == MJPEG_AVI_ERR_IO) {
        const long retry_offset =
            mjpeg_avi_decoder_last_packet_offset(&mjpeg_decoder);
        for (unsigned attempt = 1; attempt <= 2; ++attempt) {
            ESP_LOGW(kTag,
                     "Recovering MJPEG packet at %ld, attempt %u/2",
                     retry_offset, attempt);
            if (!reopenVideoAt(retry_offset)) break;
            packet_result = mjpeg_avi_decoder_read_packet(
                &mjpeg_decoder, video_file, &packet);
            if (packet_result == MJPEG_AVI_OK) {
                ESP_LOGI(kTag, "MJPEG packet recovered at %ld",
                         retry_offset);
                break;
            }
            if (packet_result != MJPEG_AVI_ERR_IO) break;
        }
    }
#endif
    const uint32_t read_us =
        (uint32_t)(microsNow() - read_start);
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

    PresentationState presentation = beginPresentation();
    applyMjpegCatchup(&presentation);
    uint32_t decode_us = 0;
    uint32_t render_us = 0;
    if (presentation.render) {
#if MJPEG_INPUT_PREFETCH
        if (!signalMjpegInputPacket(true)) {
            failSdCardRead("cannot start MJPEG packet");
            return;
        }
#endif
        MjpegRenderContext render_context = {0};
        const int64_t decode_start = microsNow();
        const int decode_result =
            PLAYER_SCALE_VIDEO_TO_DISPLAY
                ? mjpeg_avi_decoder_decode(
                      &mjpeg_decoder, &packet, renderMjpegStrip,
                      &render_context)
                : mjpeg_avi_decoder_decode_direct(
                      &mjpeg_decoder, &packet, acquireMjpegDmaStrip,
                      submitMjpegDmaStrip, &render_context);
        const uint32_t combined_us =
            (uint32_t)(microsNow() - decode_start);
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
        if (PLAYER_SCALE_VIDEO_TO_DISPLAY &&
            render_context.next_scaled_y != kScreenHeight) {
            failPlayback("JPEG output error", MJPEG_AVI_ERR_DECODE);
            return;
        }
    }
#if MJPEG_INPUT_PREFETCH
    else if (!signalMjpegInputPacket(false)) {
#else
    else if (mjpeg_avi_decoder_skip_packet(
                 &mjpeg_decoder, &packet) != MJPEG_AVI_OK) {
#endif
        failSdCardRead("cannot skip MJPEG video");
        return;
    }
    finishPresentation(presentation, read_us, decode_us, render_us);
}

plm_frame_t makeDivx3RenderFrame(Divx3Frame source) {
    plm_frame_t frame = {0};
    frame.width = source.width;
    frame.height = source.height;
    frame.storage_mode =
        source.storage_mode == DIVX3_FRAME_STORAGE_Y6_U5_V5
            ? PLM_FRAME_STORAGE_Y6_U5_V5
            : PLM_FRAME_STORAGE_YUV420;
    frame.y.width = source.width;
    frame.y.height = source.height;
    frame.y.stride = source.y_stride;
    frame.y.data = (uint8_t *)(source.y);
    frame.y.correction_stride = source.correction_stride_y;
    frame.y.correction =
        (int8_t *)(source.correction_y);
    frame.cb.width = (source.width + 1U) / 2U;
    frame.cb.height = (source.height + 1U) / 2U;
    frame.cb.stride = source.c_stride;
    frame.cb.data = (uint8_t *)(source.cb);
    frame.cb.correction_stride = source.correction_stride_c;
    frame.cb.correction =
        (int8_t *)(source.correction_cb);
    frame.cr.width = frame.cb.width;
    frame.cr.height = frame.cb.height;
    frame.cr.stride = source.c_stride;
    frame.cr.data = (uint8_t *)(source.cr);
    frame.cr.correction_stride = source.correction_stride_c;
    frame.cr.correction =
        (int8_t *)(source.correction_cr);
    return frame;
}

void playOneDivx3Frame() {
    uint32_t packet_size = 0;
    long next_offset = -1;
    const long retry_offset = ftell(video_file);
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
        (uint32_t)(microsNow() - read_start);
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

    Divx3ReplayReader stream = {
        .read = readDivx3File,
        .read_context = video_file,
    };
    if (video_waiting_for_keyframe) {
        uint8_t prefix = 0;
        int intra = 0;
        const int64_t skip_start = microsNow();
        if (fread(&prefix, 1, 1, video_file) == 1) {
            stream.prefix = prefix;
            stream.prefix_pending = 1;
            if (divx3_packet_probe_intra(&prefix, 1, &intra) == DIVX3_OK &&
                !intra) {
                if (divx3_avi_finish_video_packet(
                        video_file, next_offset) != DIVX3_AVI_OK) {
                    failSdCardRead("cannot skip DivX 3 video packet");
                    return;
                }
                finishCompressedPredictiveSkip(
                    read_us, (uint32_t)(microsNow() - skip_start));
                return;
            }
        }
    }

    Divx3Frame decoded = {0};
    const int64_t decode_start = microsNow();
    int decode_result = divx3_decoder_decode_stream(
        divx3_decoder, packet_size, divx3_replay_read,
        &stream, &decoded);
    if (divx3_avi_finish_video_packet(
            video_file, next_offset) != DIVX3_AVI_OK &&
        decode_result == DIVX3_OK) {
        decode_result = DIVX3_ERR_BITSTREAM;
    }
    const uint32_t decode_us =
        (uint32_t)(microsNow() - decode_start);
    if (decode_result != DIVX3_OK) {
        failPlayback("DivX 3 decode error", decode_result);
        return;
    }
    const plm_frame_t render_frame = makeDivx3RenderFrame(decoded);
    if (!presentDecodedFrame(
            &render_frame, renderMpegOpaque, decoded.intra != 0,
            read_us, decode_us)) {
        failPlayback("Display DMA error", DIVX3_ERR_BITSTREAM);
    }
}

void playOneDivx3FramePipelined() {
    uint32_t packet_size = 0;
    long next_offset = -1;
    const long retry_offset = ftell(video_file);
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
        (uint32_t)(microsNow() - read_start);
    if (packet_result == DIVX3_AVI_EOF) {
        if (pending_divx3_frame_valid) {
            const plm_frame_t render_frame =
                makeDivx3RenderFrame(pending_divx3_frame);
            const bool rendered = presentDecodedFrame(
                &render_frame, renderMpegOpaque,
                pending_divx3_frame.intra != 0,
                pending_read_us, pending_decode_us);
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
    if (!submitDivx3Decode(
            video_file, packet_size, next_offset)) {
        failPlayback("DivX 3 decode pipeline error",
                     DIVX3_ERR_BITSTREAM);
        return;
    }

    bool rendered = true;
    if (pending_divx3_frame_valid) {
        const plm_frame_t render_frame =
            makeDivx3RenderFrame(pending_divx3_frame);
        rendered = presentDecodedFrame(
            &render_frame, renderMpegOpaque,
            pending_divx3_frame.intra != 0,
            pending_read_us, pending_decode_us);
        pending_divx3_frame_valid = false;
    }

    DecodeResult result = {0};
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
    if (result.codec != VIDEO_CODEC_kDivx3 ||
        result.result != DIVX3_OK) {
        failPlayback("DivX 3 decode error", result.result);
        return;
    }
    if (result.skipped) {
        finishCompressedPredictiveSkip(read_us, result.decode_us);
        return;
    }
    pending_divx3_frame = result.divx3_frame;
    pending_read_us = read_us;
    pending_decode_us = result.decode_us;
    pending_divx3_frame_valid = true;
}

typedef struct BpvDirectRenderContext {
    int x_offset;
    int y_offset;
    uint32_t render_us;
    bool display_failed;
} BpvDirectRenderContext;

uint16_t *acquireBpvDmaStrip(
    void *opaque, uint16_t y, uint16_t rows) {
    BpvDirectRenderContext *context =
        (BpvDirectRenderContext *)(opaque);
    int64_t render_start;
    uint16_t *pixels;
    (void)y;
    (void)rows;
    if (!context) return NULL;
    render_start = microsNow();
    pixels = cyd_display_acquire_buffer(&display);
    context->render_us +=
        (uint32_t)(microsNow() - render_start);
    if (!pixels) context->display_failed = true;
    return pixels;
}

int submitBpvDmaStrip(
    void *opaque, const uint16_t *pixels,
    uint16_t y, uint16_t rows) {
    BpvDirectRenderContext *context =
        (BpvDirectRenderContext *)(opaque);
    int64_t render_start;
    esp_err_t result;
    if (!context || !pixels) return 1;
    render_start = microsNow();
    result = cyd_display_draw_bitmap(
        &display, context->x_offset, context->y_offset + y,
        bpv_header.width, rows, pixels);
    context->render_us +=
        (uint32_t)(microsNow() - render_start);
    if (result != ESP_OK) context->display_failed = true;
    return result == ESP_OK ? 0 : 1;
}

int flushBpvDmaStrips(void *opaque) {
    BpvDirectRenderContext *context =
        (BpvDirectRenderContext *)(opaque);
    int64_t render_start;
    esp_err_t result;
    if (!context) return 1;
    render_start = microsNow();
    result = cyd_display_flush(&display);
    context->render_us +=
        (uint32_t)(microsNow() - render_start);
    if (result != ESP_OK) context->display_failed = true;
    return result == ESP_OK ? 0 : 1;
}

void playOneBpvFrameSequential() {
    if (bpv_header.version >= BPV1_PIXEL_MOTION_VERSION) {
        PresentationState presentation;
        const BPV1Frame *frame = NULL;
        uint32_t decode_us = 0;
        uint32_t render_us = 0;
        int decode_result;
        BPV1DecodeProfile profile;
        uint64_t non_block_us;
        BpvDecodeBreakdown breakdown;

        if (decoded_frames >= bpv_header.frame_count) {
            finishVideoLoop();
            return;
        }
        presentation = beginPresentation();
        if (presentation.render &&
            !PLAYER_SCALE_VIDEO_TO_DISPLAY) {
            BpvDirectRenderContext render_context = {
                (kScreenWidth - bpv_header.width) / 2,
                (kScreenHeight - bpv_header.height) / 2,
                0, false
            };
            const int64_t combined_start = microsNow();
            uint32_t combined_us;
            decode_result =
                bpv_esp32_decoder_decode_next_direct_from_input(
                    &bpv_decoder, readBpvPrefetchedInput, NULL,
                    cyd_display_rows_per_transfer(&display),
                    acquireBpvDmaStrip, submitBpvDmaStrip,
                    flushBpvDmaStrips, &render_context, &frame);
            combined_us =
                (uint32_t)(microsNow() - combined_start);
            render_us = render_context.render_us;
            decode_us = combined_us > render_us
                            ? combined_us - render_us
                            : 0;
            if (render_context.display_failed &&
                decode_result != BPV1_OK) {
                failPlayback("Display DMA error", BPV1_ERR_IO);
                return;
            }
            if (decode_result == BPV1_OK) {
                applyKeyframeCatchup(
                    &presentation, frame->keyframe != 0);
            }
        } else {
            const int64_t decode_start = microsNow();
            decode_result =
                bpv_esp32_decoder_decode_next_direct_from_input(
                    &bpv_decoder, readBpvPrefetchedInput, NULL,
                    cyd_display_rows_per_transfer(&display),
                    NULL, NULL, NULL, NULL, &frame);
            decode_us =
                (uint32_t)(microsNow() - decode_start);
            if (decode_result == BPV1_OK) {
                applyKeyframeCatchup(
                    &presentation, frame->keyframe != 0);
            }
            if (decode_result == BPV1_OK && presentation.render) {
                const int64_t render_start = microsNow();
                if (!renderBpvFrame(frame)) {
                    failPlayback("Display DMA error", BPV1_ERR_IO);
                    return;
                }
                render_us =
                    (uint32_t)(microsNow() - render_start);
            }
        }
        if (decode_result == BPV1_OK &&
            !PLAYER_SCALE_VIDEO_TO_DISPLAY &&
            presentation.render && render_us == 0U) {
            const int64_t render_start = microsNow();
            if (!renderBpvFrame(frame)) {
                failPlayback("Display DMA error", BPV1_ERR_IO);
                return;
            }
            render_us = (uint32_t)(microsNow() - render_start);
        }
        if (decode_result == BPV1_ERR_IO) {
            failSdCardRead("cannot stream BPV1 video");
            return;
        }
        if (decode_result != BPV1_OK) {
            failPlayback("BPV1 decode error", decode_result);
            return;
        }
        profile = bpv_esp32_decoder_last_profile(&bpv_decoder);
        non_block_us =
            (uint64_t)profile.input_us +
            profile.reference_commit_us;
        breakdown.input_us = profile.input_us;
        breakdown.block_us =
            decode_us > non_block_us
                ? (uint32_t)(decode_us - non_block_us)
                : 0U;
        breakdown.reference_us = profile.reference_commit_us;
        breakdown.input_calls = profile.input_calls;
        breakdown.input_bytes = profile.input_bytes;
        finishPresentationDetailed(
            presentation, 0, decode_us, render_us, &breakdown);
        return;
    }

    BPV1Packet packet = {0};
    const int64_t read_start = microsNow();
    const int packet_result = bpv_esp32_decoder_read_packet(
        &bpv_decoder, video_file, &packet);
    const uint32_t read_us =
        (uint32_t)(microsNow() - read_start);
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

    if (video_waiting_for_keyframe && !packet.info.keyframe) {
        finishCompressedPredictiveSkip(read_us, 0);
        return;
    }

    PresentationState presentation = beginPresentation();
    applyKeyframeCatchup(&presentation, packet.info.keyframe != 0);
    const BPV1Frame *frame = NULL;
    const int64_t decode_start = microsNow();
    const int decode_result =
        bpv_esp32_decoder_decode(&bpv_decoder, &packet, &frame);
    const uint32_t decode_us =
        (uint32_t)(microsNow() - decode_start);
    uint32_t render_us = 0;
    if (decode_result == BPV1_OK && presentation.render) {
        const int64_t render_start = microsNow();
        if (!renderBpvFrame(frame)) {
            failPlayback("Display DMA error", BPV1_ERR_IO);
            return;
        }
        render_us =
            (uint32_t)(microsNow() - render_start);
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
        const int packet_result = bpv_esp32_decoder_read_packet(
            &bpv_decoder, video_file, &ready_bpv_packet);
        ready_bpv_read_us =
            (uint32_t)(microsNow() - read_start);
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
            presentBpvFrameWithLookahead(
                &pending_bpv_frame, true,
                pending_read_us, pending_decode_us);
        pending_bpv_frame_valid = false;
    }

    if (rendered &&
        !submitBpvDecode(&packet, video_file, true)) {
        failPlayback("BPV1 decode pipeline error", BPV1_ERR_IO);
        return;
    }

    if (rendered && pending_bpv_frame_valid) {
        rendered =
            presentBpvFrameWithLookahead(
                &pending_bpv_frame, packet.info.keyframe != 0,
                pending_read_us, pending_decode_us);
        pending_bpv_frame_valid = false;
    }

    DecodeResult result = {0};
    const bool received = rendered && waitDecode(&result);
    if (!rendered) {
        if (decode_in_flight) {
            DecodeResult ignored = {0};
            waitDecode(&ignored);
        }
        failPlayback("Display DMA error", BPV1_ERR_IO);
        return;
    }
    if (!received) {
        failPlayback("BPV1 decode pipeline error", BPV1_ERR_IO);
        return;
    }
    if (result.codec != VIDEO_CODEC_kBpv ||
        result.result != BPV1_OK ||
        (!result.skipped && !result.bpv_frame)) {
        failPlayback("BPV1 decode error", result.result);
        return;
    }
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
    if (result.skipped) {
        finishCompressedPredictiveSkip(read_us, result.decode_us);
        return;
    }
    pending_bpv_frame = *result.bpv_frame;
    pending_read_us = read_us;
    pending_decode_us = result.decode_us;
    pending_bpv_frame_valid = true;
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
        rendered = presentFrame(&pending_frame, pending_frame_keyframe,
                                pending_read_us, pending_decode_us);
        pending_frame_valid = false;
        pending_frame_keyframe = false;
    }

    DecodeResult result = {0};
    const bool received = waitDecode(&result);
    if (!received) {
        failPlayback("Decode pipeline error", HLV1_ERR_IO);
        return;
    }
    if (!rendered) {
        failPlayback("Display DMA error", HLV1_ERR_IO);
        return;
    }
    if (result.codec != VIDEO_CODEC_kHlv) {
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
    if (result.result == HLV1_OK && result.skipped) {
        finishCompressedPredictiveSkip(0, result.decode_us);
        return;
    }
    if (result.result != HLV1_OK || !result.hlv_frame) {
        ESP_LOGE(kTag, "HLV frame %u packet range %ld..%ld",
                 (unsigned)(decoded_frames + 1U),
                 pending_hlv_packet_offset, ftell(video_file));
        failPlayback("Decode error", result.result);
        return;
    }
    pending_frame = *result.hlv_frame;
    pending_read_us = 0;
    pending_decode_us = result.decode_us;
    pending_frame_keyframe = result.keyframe;
    pending_frame_valid = true;
}

void app_main(void) {
    initializeYuvTables();
#if defined(HLV_PLAYER_BARE_METAL_STYLE)
    esp_rom_printf("HLVBARE 1 MICROKERNEL cores=%d control_cpu=%d "
                   "decoder_target=1\n",
                   CONFIG_FREERTOS_NUMBER_OF_CORES, xPortGetCoreID());
#endif
    ESP_LOGI(kTag, "Multi-codec ESP-IDF SD player starting");
    const esp_err_t display_result = cyd_display_init(&display);
    if (display_result != ESP_OK) {
        ESP_LOGE(kTag, "Display initialization failed: %s",
                 esp_err_to_name(display_result));
        return;
    }
    if (!initializeBootButton()) {
        ESP_LOGE(kTag, "BOOT button initialization failed");
    }
    if (PLAYER_ENABLE_UART_CONTROL) {
        const esp_err_t uart_result = uart_file_upload_begin(
            &uart_upload, CONFIG_ESP_CONSOLE_UART_BAUDRATE);
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
        if (PLAYER_ENABLE_UART_CONTROL) {
        uart_upload_request_t upload_request = {0};
        if (uart_file_upload_poll_request(&uart_upload,
                                          &upload_request)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_file_upload_reject(&uart_upload, "NO_SD");
                last_retry_ms = millisNow();
                continue;
            }
            file_browser_active = false;
            closeVideo();
            beginUploadProgress(
                upload_request.filename, upload_request.size);
            char stored_path[128] = {0};
            const bool stored = uart_file_upload_receive(
                &uart_upload, &upload_request, PLAYER_VIDEO_DIRECTORY,
                stored_path, sizeof stored_path, updateUploadProgress,
                NULL);
            cyd_display_flush(&display);
            showStatus(stored ? "UART upload complete"
                              : "UART upload failed",
                       upload_request.filename);
            continue;
        }
        {
            char session_command[UART_SESSION_COMMAND_BYTES] = {0};
            if (uart_file_upload_take_session_request(
                    &uart_upload, session_command,
                    sizeof session_command)) {
                file_browser_active = false;
                quietUartDiagnostics();
                closeVideo();
                showUartSession(session_command);
                uart_file_upload_session_ready(
                    &uart_upload, session_command);
                continue;
            }
        }
        if (uart_file_upload_take_monitoring_request(&uart_upload)) {
            uart_file_upload_monitoring_ready(&uart_upload);
            restoreUartDiagnostics();
            showStatus("UART monitoring", "resuming video");
            if (sd_mounted && video_file == NULL && !openVideo()) {
                last_retry_ms = millisNow();
            }
            continue;
        }
        {
            uint32_t position_ms = 0;
            if (uart_file_upload_take_seek_request(
                    &uart_upload, &position_ms)) {
                uint64_t target_frame;
                file_browser_active = false;
                seek_fast_forward = false;
                showSeekStatus(position_ms);
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
                target_frame =
                    ((uint64_t)(position_ms) *
                     sequence_header.fps_num) /
                    (1000ULL * sequence_header.fps_den);
                if (sequence_header.frame_count > 0U &&
                    target_frame >= sequence_header.frame_count) {
                    target_frame = sequence_header.frame_count - 1U;
                }
                if (target_frame > UINT32_MAX) {
                    target_frame = UINT32_MAX;
                }
                seek_requested_ms = position_ms;
                seek_target_frame = (uint32_t)target_frame;
                seek_discarded_audio_samples = 0;
                if (seek_target_frame == 0U) {
                    esp_rom_printf(
                        "HLVSEEKDONE 1 %u 0 0\n",
                        (unsigned)(seek_requested_ms));
                } else {
                    seek_fast_forward = true;
                    esp_rom_printf(
                        "HLVSEEKBEGIN 1 %u %u\n",
                        (unsigned)(seek_requested_ms),
                        (unsigned)(seek_target_frame));
                }
                continue;
            }
        }
        if (uart_file_upload_take_list_request(&uart_upload)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_file_upload_reject(&uart_upload, "NO_SD");
                last_retry_ms = millisNow();
            } else {
                file_browser_active = false;
                closeVideo();
                showUartSession("LIST");
                uart_file_upload_list_directory(
                    &uart_upload, PLAYER_VIDEO_DIRECTORY);
            }
            continue;
        }
        {
            uint32_t requested_baud = 0;
            if (uart_file_upload_take_baud_request(
                    &uart_upload, &requested_baud)) {
                file_browser_active = false;
                showUartSession("BAUD");
                uart_file_upload_change_baud(
                    &uart_upload, requested_baud);
                continue;
            }
        }
        char delete_filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U] = {0};
        if (uart_file_upload_take_delete_request(
                &uart_upload, delete_filename,
                sizeof delete_filename)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_file_upload_reject(&uart_upload, "NO_SD");
                last_retry_ms = millisNow();
            } else {
                file_browser_active = false;
                closeVideo();
                showUartSession("DELETE");
                uart_file_upload_delete_file(
                    &uart_upload, PLAYER_VIDEO_DIRECTORY,
                    delete_filename);
            }
            continue;
        }
        char crc_filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U] = {0};
        if (uart_file_upload_take_crc_request(
                &uart_upload, crc_filename, sizeof crc_filename)) {
            if (!sd_mounted && !mountSdCard()) {
                uart_file_upload_reject(&uart_upload, "NO_SD");
                last_retry_ms = millisNow();
            } else {
                file_browser_active = false;
                closeVideo();
                showUartSession("CRC32");
                uart_file_upload_checksum_file(
                    &uart_upload, PLAYER_VIDEO_DIRECTORY, crc_filename);
            }
            continue;
        }
        {
            uart_block_crc_request_t block_crc_request = {0};
            if (uart_file_upload_take_block_crc_request(
                    &uart_upload, &block_crc_request)) {
                if (!sd_mounted && !mountSdCard()) {
                    uart_file_upload_reject(&uart_upload, "NO_SD");
                    last_retry_ms = millisNow();
                } else {
                    file_browser_active = false;
                    closeVideo();
                    showUartSession("BLOCK CRC32");
                    uart_file_upload_checksum_blocks(
                        &uart_upload, PLAYER_VIDEO_DIRECTORY,
                        &block_crc_request);
                }
                continue;
            }
        }
        {
            uart_read_request_t read_request = {0};
            if (uart_file_upload_take_read_request(
                    &uart_upload, &read_request)) {
                if (!sd_mounted && !mountSdCard()) {
                    uart_file_upload_reject(&uart_upload, "NO_SD");
                    last_retry_ms = millisNow();
                } else {
                    file_browser_active = false;
                    closeVideo();
                    showUartSession("READ");
                    uart_file_upload_read_file(
                        &uart_upload, PLAYER_VIDEO_DIRECTORY, &read_request);
                }
                continue;
            }
        }
        {
            uart_patch_request_t patch_request = {0};
            if (uart_file_upload_take_patch_request(
                    &uart_upload, &patch_request)) {
                if (!sd_mounted && !mountSdCard()) {
                    uart_file_upload_reject(&uart_upload, "NO_SD");
                    last_retry_ms = millisNow();
                } else {
                    file_browser_active = false;
                    closeVideo();
                    showUartSession("PATCH");
                    uart_file_upload_patch_file(
                        &uart_upload, PLAYER_VIDEO_DIRECTORY,
                        &patch_request);
                }
                continue;
            }
        }
        {
            uart_sd_benchmark_request_t sd_benchmark_request = {0};
            if (uart_file_upload_take_sd_benchmark_request(
                    &uart_upload, &sd_benchmark_request)) {
                if (!sd_mounted && !mountSdCard()) {
                    uart_file_upload_reject(&uart_upload, "NO_SD");
                    last_retry_ms = millisNow();
                } else {
                    bool completed;
                    file_browser_active = false;
                    closeVideo();
                    showUartSession("SD BENCHMARK");
                    completed = uart_file_upload_benchmark_sd(
                        &uart_upload, PLAYER_VIDEO_DIRECTORY,
                        &sd_benchmark_request);
                    showStatus(
                        completed ? "SD benchmark complete"
                                  : "SD benchmark failed",
                        completed ? "temporary file removed"
                                  : "see UART error");
                }
                continue;
            }
        }
        }
        if (file_browser_active) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (video_file && video_codec == VIDEO_CODEC_kMpeg1 &&
            mpeg_video) {
#if PLM_MPEG_STREAM_B_ROWS
            playOneMpegFrameStreamedRows();
#else
            if (PLAYER_USE_DUAL_CORE_PIPELINE) {
                playOneMpegFramePipelined();
            } else {
                playOneMpegFrameSequential();
            }
#endif
            continue;
        }
        if (video_file && isPacketVideoCodec(video_codec) &&
            h263_decoder) {
            if (PLAYER_USE_DUAL_CORE_PIPELINE &&
                (h263_dual_buffered || h263_row_pipelined)) {
                playOneH263FramePipelined();
            } else {
                playOneH263Frame();
            }
            continue;
        }
        if (video_file && video_codec == VIDEO_CODEC_kMjpeg &&
            mjpeg_avi_decoder_ready(&mjpeg_decoder)) {
            playOneMjpegFrame();
            continue;
        }
        if (video_file && video_codec == VIDEO_CODEC_kDivx3 &&
            divx3_decoder) {
            if (PLAYER_USE_DUAL_CORE_PIPELINE) {
                playOneDivx3FramePipelined();
            } else {
                playOneDivx3Frame();
            }
            continue;
        }
        if (video_file && video_codec == VIDEO_CODEC_kBpv &&
            bpv_esp32_decoder_ready(&bpv_decoder)) {
            if (PLAYER_USE_DUAL_CORE_PIPELINE &&
                bpv_header.version < BPV1_PIXEL_MOTION_VERSION) {
                playOneBpvFramePipelined();
            } else {
                playOneBpvFrameSequential();
            }
            continue;
        }
        if (video_file && video_codec == VIDEO_CODEC_kHlv &&
            hlv_esp32_decoder_ready(&decoder)) {
            if (PLAYER_USE_DUAL_CORE_PIPELINE) {
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
