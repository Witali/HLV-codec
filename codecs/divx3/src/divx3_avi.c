#include "divx3_avi.h"

#include "avi_demux.h"
#include "ima_adpcm.h"

#include <limits.h>
#include <string.h>

enum {
    DIVX3_AVI_FALLBACK_PACKET_BYTES = 128 * 1024,
    DIVX3_AVI_MAX_PACKET_BYTES = 1024 * 1024,
};

static int map_demux_result(int result) {
    switch (result) {
        case AVI_DEMUX_OK: return DIVX3_AVI_OK;
        case AVI_DEMUX_EOF: return DIVX3_AVI_EOF;
        case AVI_DEMUX_ERR_ARGUMENT: return DIVX3_AVI_ERR_ARGUMENT;
        case AVI_DEMUX_ERR_IO: return DIVX3_AVI_ERR_IO;
        case AVI_DEMUX_ERR_RANGE: return DIVX3_AVI_ERR_RANGE;
        default: return DIVX3_AVI_ERR_FORMAT;
    }
}

int divx3_avi_is_v3_fourcc(uint32_t value) {
    static const char aliases[][4] = {
        {'D', 'I', 'V', '3'}, {'M', 'P', '4', '3'},
        {'D', 'I', 'V', '4'}, {'D', 'I', 'V', '5'},
        {'D', 'I', 'V', '6'}, {'A', 'P', '4', '1'},
        {'C', 'O', 'L', '1'}, {'C', 'O', 'L', '0'},
        {'M', 'P', 'G', '3'}, {'D', 'V', 'X', '3'},
        {'3', 'I', 'V', '1'}, {'3', 'I', 'V', 'D'},
    };
    size_t index;
    uint8_t bytes[4];
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
    for (index = 0; index < sizeof(aliases) / sizeof(aliases[0]); ++index) {
        unsigned byte;
        int matches = 1;
        for (byte = 0; byte < 4; ++byte) {
            uint8_t actual = bytes[byte];
            const uint8_t expected = (uint8_t)aliases[index][byte];
            if (actual >= 'a' && actual <= 'z') actual -= 32;
            if (actual != expected) {
                matches = 0;
                break;
            }
        }
        if (matches) return 1;
    }
    return 0;
}

static void export_demux_info(const AviDemuxInfo *source,
                              Divx3AviInfo *target) {
    memset(target, 0, sizeof(*target));
    target->video_stream = source->video.stream_index;
    target->audio_stream = AVI_DEMUX_NO_STREAM;
    target->width = source->video.width;
    target->height = source->video.height;
    target->fps_num = source->video.rate;
    target->fps_den = source->video.scale;
    target->frame_count = source->video.frame_count
                              ? source->video.frame_count
                              : source->main_frame_count;
    target->max_video_packet_size = source->video.max_packet_size;
    target->movi_start = source->movi_start;
    target->movi_end = source->movi_end;
}

static void import_demux_info(const Divx3AviInfo *source,
                              AviDemuxInfo *target) {
    memset(target, 0, sizeof(*target));
    target->video.stream_index = source->video_stream;
    target->audio.stream_index = source->audio_stream;
    target->movi_start = source->movi_start;
    target->movi_end = source->movi_end;
}

static int accept_audio(const AviDemuxInfo *source, Divx3AviInfo *target) {
    const AviDemuxAudioInfo *audio = &source->audio;
    if (audio->stream_index == AVI_DEMUX_NO_STREAM) return DIVX3_AVI_OK;
    if (audio->format_tag != 1U && audio->format_tag != 0x11U)
        return DIVX3_AVI_OK;
    if (!audio->sample_rate || audio->sample_rate > 48000U ||
        audio->channels != 1U)
        return DIVX3_AVI_ERR_FORMAT;
    if (audio->format_tag == 1U) {
        if (audio->bits_per_sample != 8U || audio->block_align != 1U)
            return DIVX3_AVI_ERR_FORMAT;
        target->audio_samples_per_block = 1U;
    } else {
        const size_t expected =
            ima_adpcm_wav_mono_sample_count(audio->block_align);
        if (audio->bits_per_sample != 4U || audio->extra_size < 2U ||
            !expected || audio->samples_per_block != expected)
            return DIVX3_AVI_ERR_FORMAT;
        target->audio_samples_per_block = audio->samples_per_block;
    }
    target->audio_stream = audio->stream_index;
    target->audio_format_tag = audio->format_tag;
    target->audio_block_align = audio->block_align;
    target->audio_channels = (uint8_t)audio->channels;
    target->audio_sample_rate = audio->sample_rate;
    target->audio_bits_per_sample = (uint8_t)audio->bits_per_sample;
    return DIVX3_AVI_OK;
}

int divx3_avi_read_info(FILE *file, Divx3AviInfo *info) {
    AviDemuxInfo demux;
    int result;
    if (!file || !info) return DIVX3_AVI_ERR_ARGUMENT;
    result = avi_demux_read_info(file, &demux);
    if (result != AVI_DEMUX_OK) return map_demux_result(result);
    if (!divx3_avi_is_v3_fourcc(demux.video.compression_fourcc) ||
        !demux.video.rate || !demux.video.scale)
        return DIVX3_AVI_ERR_FORMAT;
    export_demux_info(&demux, info);
    result = accept_audio(&demux, info);
    if (result != DIVX3_AVI_OK) return result;
    if (!info->frame_count) return DIVX3_AVI_ERR_FORMAT;
    if (!info->max_video_packet_size)
        info->max_video_packet_size = DIVX3_AVI_FALLBACK_PACKET_BYTES;
    if (info->max_video_packet_size > DIVX3_AVI_MAX_PACKET_BYTES)
        return DIVX3_AVI_ERR_RANGE;
    return DIVX3_AVI_OK;
}

int divx3_avi_begin_video_packet(FILE *file, const Divx3AviInfo *info,
                                 uint32_t *packet_size,
                                 long *next_offset) {
    AviDemuxInfo demux;
    AviDemuxPacket packet;
    int result;
    if (!file || !info || !packet_size || !next_offset)
        return DIVX3_AVI_ERR_ARGUMENT;
    import_demux_info(info, &demux);
    result = avi_demux_next_packet(file, &demux, AVI_DEMUX_PACKET_VIDEO,
                                   &packet);
    if (result != AVI_DEMUX_OK) return map_demux_result(result);
    *packet_size = packet.payload_size;
    *next_offset = packet.next_offset;
    return DIVX3_AVI_OK;
}

int divx3_avi_finish_video_packet(FILE *file, long next_offset) {
    AviDemuxPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.next_offset = next_offset;
    return map_demux_result(avi_demux_finish_packet(file, &packet));
}

int divx3_avi_read_video_packet(FILE *file, const Divx3AviInfo *info,
                                uint8_t *buffer, size_t capacity,
                                size_t *packet_size) {
    uint32_t size;
    long next_offset;
    int result;
    if (!file || !info || !buffer || !capacity || !packet_size)
        return DIVX3_AVI_ERR_ARGUMENT;
    result = divx3_avi_begin_video_packet(file, info, &size, &next_offset);
    if (result != DIVX3_AVI_OK) return result;
    *packet_size = size;
    if (size > capacity) return DIVX3_AVI_ERR_RANGE;
    if (fread(buffer, 1, size, file) != size) return DIVX3_AVI_ERR_IO;
    return divx3_avi_finish_video_packet(file, next_offset);
}

int divx3_avi_next_audio_chunk(FILE *file, const Divx3AviInfo *info,
                               uint32_t *payload_size) {
    AviDemuxInfo demux;
    AviDemuxPacket packet;
    int result;
    if (!file || !info || !payload_size) return DIVX3_AVI_ERR_ARGUMENT;
    import_demux_info(info, &demux);
    result = avi_demux_next_packet(file, &demux, AVI_DEMUX_PACKET_AUDIO,
                                   &packet);
    if (result == AVI_DEMUX_OK) *payload_size = packet.payload_size;
    return map_demux_result(result);
}

const char *divx3_avi_strerror(int result) {
    switch (result) {
        case DIVX3_AVI_OK: return "success";
        case DIVX3_AVI_EOF: return "end of file";
        case DIVX3_AVI_ERR_ARGUMENT: return "invalid AVI argument";
        case DIVX3_AVI_ERR_IO: return "AVI I/O error";
        case DIVX3_AVI_ERR_FORMAT: return "unsupported DivX 3 AVI";
        case DIVX3_AVI_ERR_RANGE: return "DivX 3 AVI value is out of range";
        default: return "unknown DivX 3 AVI error";
    }
}
