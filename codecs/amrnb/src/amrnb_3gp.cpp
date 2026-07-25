#include "amrnb_3gp.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include "gsmamr_dec.h"

namespace {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(d));
}

constexpr uint8_t kPayloadBytes[16] = {
    12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0,
};

struct Box {
    uint32_t type = 0;
    uint64_t start = 0;
    uint64_t data = 0;
    uint64_t end = 0;
};

struct StscEntry {
    uint32_t first_chunk = 0;
    uint32_t samples_per_chunk = 0;
};

struct SttsEntry {
    uint32_t sample_count = 0;
    uint32_t sample_delta = 0;
};

bool seekFile(FILE *file, uint64_t offset) {
#ifdef _WIN32
    return offset <= static_cast<uint64_t>(INT64_MAX) &&
           _fseeki64(file, static_cast<int64_t>(offset), SEEK_SET) == 0;
#else
    return offset <=
               static_cast<uint64_t>(std::numeric_limits<off_t>::max()) &&
           fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

bool tellFile(FILE *file, uint64_t *offset) {
#ifdef _WIN32
    const int64_t position = _ftelli64(file);
#else
    const off_t position = ftello(file);
#endif
    if (position < 0) return false;
    *offset = static_cast<uint64_t>(position);
    return true;
}

bool readExact(FILE *file, void *destination, size_t size) {
    return size == 0 || ::fread(destination, 1, size, file) == size;
}

bool readU16(FILE *file, uint16_t *value) {
    uint8_t bytes[2];
    if (!readExact(file, bytes, sizeof bytes)) return false;
    *value = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
    return true;
}

bool readU32(FILE *file, uint32_t *value) {
    uint8_t bytes[4];
    if (!readExact(file, bytes, sizeof bytes)) return false;
    *value = (static_cast<uint32_t>(bytes[0]) << 24) |
             (static_cast<uint32_t>(bytes[1]) << 16) |
             (static_cast<uint32_t>(bytes[2]) << 8) |
             static_cast<uint32_t>(bytes[3]);
    return true;
}

bool readU64(FILE *file, uint64_t *value) {
    uint32_t high = 0;
    uint32_t low = 0;
    if (!readU32(file, &high) || !readU32(file, &low)) return false;
    *value = (static_cast<uint64_t>(high) << 32) | low;
    return true;
}

bool fileSize(FILE *file, uint64_t *size) {
    uint64_t saved = 0;
    if (!tellFile(file, &saved)) return false;
#ifdef _WIN32
    if (_fseeki64(file, 0, SEEK_END) != 0) return false;
#else
    if (fseeko(file, 0, SEEK_END) != 0) return false;
#endif
    const bool ok = tellFile(file, size);
    return seekFile(file, saved) && ok;
}

bool readBox(FILE *file, uint64_t limit, Box *box) {
    uint64_t start = 0;
    uint32_t size32 = 0;
    uint32_t type = 0;
    if (!tellFile(file, &start) || start > limit || limit - start < 8 ||
        !readU32(file, &size32) || !readU32(file, &type)) {
        return false;
    }
    uint64_t size = size32;
    uint64_t header = 8;
    if (size32 == 1) {
        if (limit - start < 16 || !readU64(file, &size)) return false;
        header = 16;
    } else if (size32 == 0) {
        size = limit - start;
    }
    if (size < header || size > limit - start) return false;
    box->type = type;
    box->start = start;
    box->data = start + header;
    box->end = start + size;
    return true;
}

bool findChild(FILE *file, const Box &parent, uint32_t type, Box *child,
               uint64_t first_data = 0) {
    uint64_t cursor = first_data ? first_data : parent.data;
    while (cursor < parent.end) {
        if (!seekFile(file, cursor) || !readBox(file, parent.end, child))
            return false;
        if (child->type == type) return true;
        if (child->end <= cursor) return false;
        cursor = child->end;
    }
    return false;
}

}  // namespace

struct AmrNb3gpDecoder {
    AmrNb3gpInfo info{};
    void *amr_state = nullptr;
    uint8_t *packet = nullptr;
    int16_t pcm[AMRNB_SAMPLES_PER_FRAME]{};

    uint64_t stsz_entries = 0;
    uint32_t fixed_sample_size = 0;
    uint64_t chunk_entries = 0;
    uint32_t chunk_count = 0;
    bool chunks_are_64_bit = false;
    StscEntry *stsc = nullptr;
    uint32_t stsc_count = 0;
    SttsEntry *stts = nullptr;
    uint32_t stts_count = 0;

    uint32_t sample_index = 0;
    uint32_t chunk_index = 0;
    uint32_t sample_in_chunk = 0;
    uint64_t sample_offset = 0;
    uint32_t samples_in_chunk = 0;
    uint32_t stsc_index = 0;
    uint32_t stts_index = 0;
    uint32_t stts_remaining = 0;
    uint64_t timestamp = 0;

    void clear() {
        if (amr_state) GSMDecodeFrameExit(&amr_state);
        std::free(packet);
        std::free(stsc);
        std::free(stts);
        *this = AmrNb3gpDecoder{};
    }
};

namespace {

int parseMediaHeader(FILE *file, const Box &mdhd, AmrNb3gpInfo *info) {
    if (!seekFile(file, mdhd.data)) return AMRNB_3GP_ERR_IO;
    uint32_t version_flags = 0;
    if (!readU32(file, &version_flags)) return AMRNB_3GP_ERR_IO;
    const uint8_t version = static_cast<uint8_t>(version_flags >> 24);
    if (version == 0) {
        uint32_t ignored = 0;
        uint32_t duration = 0;
        if (!readU32(file, &ignored) || !readU32(file, &ignored) ||
            !readU32(file, &info->timescale) ||
            !readU32(file, &duration)) {
            return AMRNB_3GP_ERR_IO;
        }
        info->duration_ticks = duration;
    } else if (version == 1) {
        uint64_t ignored = 0;
        if (!readU64(file, &ignored) || !readU64(file, &ignored) ||
            !readU32(file, &info->timescale) ||
            !readU64(file, &info->duration_ticks)) {
            return AMRNB_3GP_ERR_IO;
        }
    } else {
        return AMRNB_3GP_ERR_UNSUPPORTED;
    }
    return info->timescale ? AMRNB_3GP_OK : AMRNB_3GP_ERR_FORMAT;
}

int parseSampleDescription(FILE *file, const Box &stsd,
                           AmrNb3gpInfo *info) {
    if (!seekFile(file, stsd.data + 4)) return AMRNB_3GP_ERR_IO;
    uint32_t count = 0;
    if (!readU32(file, &count)) return AMRNB_3GP_ERR_IO;
    uint64_t cursor = stsd.data + 8;
    for (uint32_t index = 0; index < count && cursor < stsd.end; ++index) {
        if (!seekFile(file, cursor)) return AMRNB_3GP_ERR_IO;
        Box entry;
        if (!readBox(file, stsd.end, &entry)) return AMRNB_3GP_ERR_FORMAT;
        if (entry.type == fourcc('s', 'a', 'm', 'r')) {
            uint16_t channels = 0;
            uint16_t sample_bits = 0;
            uint32_t sample_rate_fixed = 0;
            if (entry.end - entry.data < 28 ||
                !seekFile(file, entry.data + 16) ||
                !readU16(file, &channels) ||
                !readU16(file, &sample_bits) ||
                !seekFile(file, entry.data + 24) ||
                !readU32(file, &sample_rate_fixed)) {
                return AMRNB_3GP_ERR_FORMAT;
            }
            info->channels = static_cast<uint8_t>(channels);
            info->sample_rate =
                static_cast<uint16_t>(sample_rate_fixed >> 16);
            Box damr;
            if (!findChild(file, entry, fourcc('d', 'a', 'm', 'r'), &damr,
                           entry.data + 28) ||
                damr.end - damr.data < 9 ||
                !seekFile(file, damr.data + 5) ||
                !readU16(file, &info->mode_set)) {
                return AMRNB_3GP_ERR_FORMAT;
            }
            uint8_t mode_change_period = 0;
            if (!readExact(file, &mode_change_period, 1) ||
                !readExact(file, &info->frames_per_sample, 1)) {
                return AMRNB_3GP_ERR_IO;
            }
            if (channels != 1 || sample_bits != 16 ||
                info->sample_rate != AMRNB_SAMPLE_RATE ||
                info->frames_per_sample != 1) {
                return AMRNB_3GP_ERR_UNSUPPORTED;
            }
            return AMRNB_3GP_OK;
        }
        cursor = entry.end;
    }
    return AMRNB_3GP_ERR_UNSUPPORTED;
}

int parseStsz(FILE *file, const Box &stsz, AmrNb3gpDecoder *decoder) {
    if (!seekFile(file, stsz.data + 4) ||
        !readU32(file, &decoder->fixed_sample_size) ||
        !readU32(file, &decoder->info.frame_count)) {
        return AMRNB_3GP_ERR_IO;
    }
    if (!decoder->info.frame_count) return AMRNB_3GP_ERR_FORMAT;
    decoder->stsz_entries = stsz.data + 12;
    if (decoder->fixed_sample_size) {
        decoder->info.max_sample_size = decoder->fixed_sample_size;
        return AMRNB_3GP_OK;
    }
    if (stsz.end - decoder->stsz_entries <
        static_cast<uint64_t>(decoder->info.frame_count) * 4) {
        return AMRNB_3GP_ERR_FORMAT;
    }
    if (!seekFile(file, decoder->stsz_entries)) return AMRNB_3GP_ERR_IO;
    for (uint32_t i = 0; i < decoder->info.frame_count; ++i) {
        uint32_t size = 0;
        if (!readU32(file, &size)) return AMRNB_3GP_ERR_IO;
        decoder->info.max_sample_size =
            std::max(decoder->info.max_sample_size, size);
    }
    return decoder->info.max_sample_size ? AMRNB_3GP_OK
                                         : AMRNB_3GP_ERR_FORMAT;
}

int parseChunks(FILE *file, const Box &box, AmrNb3gpDecoder *decoder) {
    if (!seekFile(file, box.data + 4) ||
        !readU32(file, &decoder->chunk_count)) {
        return AMRNB_3GP_ERR_IO;
    }
    if (!decoder->chunk_count) return AMRNB_3GP_ERR_FORMAT;
    decoder->chunks_are_64_bit = box.type == fourcc('c', 'o', '6', '4');
    decoder->chunk_entries = box.data + 8;
    const uint64_t entry_size = decoder->chunks_are_64_bit ? 8 : 4;
    return box.end - decoder->chunk_entries >=
                   static_cast<uint64_t>(decoder->chunk_count) * entry_size
               ? AMRNB_3GP_OK
               : AMRNB_3GP_ERR_FORMAT;
}

int parseStsc(FILE *file, const Box &stsc, AmrNb3gpDecoder *decoder) {
    if (!seekFile(file, stsc.data + 4) ||
        !readU32(file, &decoder->stsc_count)) {
        return AMRNB_3GP_ERR_IO;
    }
    if (!decoder->stsc_count ||
        decoder->stsc_count >
            std::numeric_limits<size_t>::max() / sizeof(StscEntry)) {
        return AMRNB_3GP_ERR_FORMAT;
    }
    decoder->stsc = static_cast<StscEntry *>(
        std::calloc(decoder->stsc_count, sizeof(StscEntry)));
    if (!decoder->stsc) return AMRNB_3GP_ERR_MEMORY;
    if (stsc.end - (stsc.data + 8) <
        static_cast<uint64_t>(decoder->stsc_count) * 12) {
        return AMRNB_3GP_ERR_FORMAT;
    }
    for (uint32_t i = 0; i < decoder->stsc_count; ++i) {
        uint32_t ignored = 0;
        if (!readU32(file, &decoder->stsc[i].first_chunk) ||
            !readU32(file, &decoder->stsc[i].samples_per_chunk) ||
            !readU32(file, &ignored)) {
            return AMRNB_3GP_ERR_IO;
        }
        if (!decoder->stsc[i].first_chunk ||
            !decoder->stsc[i].samples_per_chunk ||
            (i && decoder->stsc[i].first_chunk <=
                      decoder->stsc[i - 1].first_chunk)) {
            return AMRNB_3GP_ERR_FORMAT;
        }
    }
    return decoder->stsc[0].first_chunk == 1 ? AMRNB_3GP_OK
                                             : AMRNB_3GP_ERR_FORMAT;
}

int parseStts(FILE *file, const Box &stts, AmrNb3gpDecoder *decoder) {
    if (!seekFile(file, stts.data + 4) ||
        !readU32(file, &decoder->stts_count)) {
        return AMRNB_3GP_ERR_IO;
    }
    if (!decoder->stts_count ||
        decoder->stts_count >
            std::numeric_limits<size_t>::max() / sizeof(SttsEntry)) {
        return AMRNB_3GP_ERR_FORMAT;
    }
    decoder->stts = static_cast<SttsEntry *>(
        std::calloc(decoder->stts_count, sizeof(SttsEntry)));
    if (!decoder->stts) return AMRNB_3GP_ERR_MEMORY;
    if (stts.end - (stts.data + 8) <
        static_cast<uint64_t>(decoder->stts_count) * 8) {
        return AMRNB_3GP_ERR_FORMAT;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < decoder->stts_count; ++i) {
        if (!readU32(file, &decoder->stts[i].sample_count) ||
            !readU32(file, &decoder->stts[i].sample_delta)) {
            return AMRNB_3GP_ERR_IO;
        }
        const bool final_partial_frame =
            i + 1 == decoder->stts_count &&
            decoder->stts[i].sample_count == 1 &&
            decoder->stts[i].sample_delta > 0 &&
            decoder->stts[i].sample_delta < AMRNB_SAMPLES_PER_FRAME;
        if (!decoder->stts[i].sample_count ||
            (decoder->stts[i].sample_delta != AMRNB_SAMPLES_PER_FRAME &&
             !final_partial_frame) ||
            total > decoder->info.frame_count ||
            decoder->stts[i].sample_count >
                decoder->info.frame_count - total) {
            return AMRNB_3GP_ERR_FORMAT;
        }
        total += decoder->stts[i].sample_count;
    }
    if (total != decoder->info.frame_count) return AMRNB_3GP_ERR_FORMAT;
    decoder->stts_remaining = decoder->stts[0].sample_count;
    return AMRNB_3GP_OK;
}

int parseTrack(FILE *file, const Box &trak, AmrNb3gpDecoder *decoder) {
    Box mdia;
    Box hdlr;
    if (!findChild(file, trak, fourcc('m', 'd', 'i', 'a'), &mdia) ||
        !findChild(file, mdia, fourcc('h', 'd', 'l', 'r'), &hdlr)) {
        return AMRNB_3GP_ERR_FORMAT;
    }
    if (!seekFile(file, hdlr.data + 8)) return AMRNB_3GP_ERR_IO;
    uint32_t handler = 0;
    if (!readU32(file, &handler)) return AMRNB_3GP_ERR_IO;
    if (handler != fourcc('s', 'o', 'u', 'n'))
        return AMRNB_3GP_ERR_UNSUPPORTED;

    Box mdhd;
    Box minf;
    Box stbl;
    Box stsd;
    Box stsz;
    Box stsc;
    Box stts;
    Box chunks;
    if (!findChild(file, mdia, fourcc('m', 'd', 'h', 'd'), &mdhd) ||
        !findChild(file, mdia, fourcc('m', 'i', 'n', 'f'), &minf) ||
        !findChild(file, minf, fourcc('s', 't', 'b', 'l'), &stbl) ||
        !findChild(file, stbl, fourcc('s', 't', 's', 'd'), &stsd) ||
        !findChild(file, stbl, fourcc('s', 't', 's', 'z'), &stsz) ||
        !findChild(file, stbl, fourcc('s', 't', 's', 'c'), &stsc) ||
        !findChild(file, stbl, fourcc('s', 't', 't', 's'), &stts)) {
        return AMRNB_3GP_ERR_FORMAT;
    }
    if (!findChild(file, stbl, fourcc('s', 't', 'c', 'o'), &chunks) &&
        !findChild(file, stbl, fourcc('c', 'o', '6', '4'), &chunks)) {
        return AMRNB_3GP_ERR_FORMAT;
    }

    int result = parseMediaHeader(file, mdhd, &decoder->info);
    if (result == AMRNB_3GP_OK)
        result = parseSampleDescription(file, stsd, &decoder->info);
    if (result == AMRNB_3GP_OK) result = parseStsz(file, stsz, decoder);
    if (result == AMRNB_3GP_OK) result = parseChunks(file, chunks, decoder);
    if (result == AMRNB_3GP_OK) result = parseStsc(file, stsc, decoder);
    if (result == AMRNB_3GP_OK) result = parseStts(file, stts, decoder);
    return result;
}

int parseContainer(FILE *file, AmrNb3gpDecoder *decoder) {
    uint64_t size = 0;
    if (!fileSize(file, &size)) return AMRNB_3GP_ERR_IO;
    Box root;
    root.data = 0;
    root.end = size;
    Box ftyp;
    Box moov;
    if (!findChild(file, root, fourcc('f', 't', 'y', 'p'), &ftyp) ||
        !findChild(file, root, fourcc('m', 'o', 'o', 'v'), &moov)) {
        return AMRNB_3GP_ERR_FORMAT;
    }
    uint64_t cursor = moov.data;
    while (cursor < moov.end) {
        if (!seekFile(file, cursor)) return AMRNB_3GP_ERR_IO;
        Box child;
        if (!readBox(file, moov.end, &child)) return AMRNB_3GP_ERR_FORMAT;
        if (child.type == fourcc('t', 'r', 'a', 'k')) {
            const int result = parseTrack(file, child, decoder);
            if (result == AMRNB_3GP_OK) return AMRNB_3GP_OK;
            if (result != AMRNB_3GP_ERR_UNSUPPORTED) return result;
        }
        cursor = child.end;
    }
    return AMRNB_3GP_ERR_UNSUPPORTED;
}

int sampleSize(FILE *file, const AmrNb3gpDecoder *decoder, uint32_t index,
               uint32_t *size) {
    if (decoder->fixed_sample_size) {
        *size = decoder->fixed_sample_size;
        return AMRNB_3GP_OK;
    }
    if (!seekFile(file, decoder->stsz_entries +
                            static_cast<uint64_t>(index) * 4) ||
        !readU32(file, size)) {
        return AMRNB_3GP_ERR_IO;
    }
    return *size ? AMRNB_3GP_OK : AMRNB_3GP_ERR_FORMAT;
}

int beginChunk(FILE *file, AmrNb3gpDecoder *decoder) {
    if (decoder->chunk_index >= decoder->chunk_count)
        return AMRNB_3GP_ERR_FORMAT;
    while (decoder->stsc_index + 1 < decoder->stsc_count &&
           decoder->chunk_index + 1 >=
               decoder->stsc[decoder->stsc_index + 1].first_chunk) {
        ++decoder->stsc_index;
    }
    decoder->samples_in_chunk =
        decoder->stsc[decoder->stsc_index].samples_per_chunk;
    const uint64_t entry_offset =
        decoder->chunk_entries +
        static_cast<uint64_t>(decoder->chunk_index) *
            (decoder->chunks_are_64_bit ? 8 : 4);
    if (!seekFile(file, entry_offset)) return AMRNB_3GP_ERR_IO;
    if (decoder->chunks_are_64_bit) {
        if (!readU64(file, &decoder->sample_offset))
            return AMRNB_3GP_ERR_IO;
    } else {
        uint32_t offset = 0;
        if (!readU32(file, &offset)) return AMRNB_3GP_ERR_IO;
        decoder->sample_offset = offset;
    }
    return AMRNB_3GP_OK;
}

}  // namespace

extern "C" {

AmrNb3gpDecoder *amrnb_3gp_decoder_create(void) {
    return new (std::nothrow) AmrNb3gpDecoder();
}

void amrnb_3gp_decoder_destroy(AmrNb3gpDecoder *decoder) {
    if (!decoder) return;
    decoder->clear();
    delete decoder;
}

int amrnb_3gp_decoder_open(AmrNb3gpDecoder *decoder, FILE *file,
                           AmrNb3gpInfo *info) {
    if (!decoder || !file || !info) return AMRNB_3GP_ERR_ARGUMENT;
    decoder->clear();
    int result = parseContainer(file, decoder);
    if (result == AMRNB_3GP_OK &&
        (decoder->info.timescale != AMRNB_SAMPLE_RATE ||
         decoder->info.max_sample_size > 32)) {
        result = AMRNB_3GP_ERR_UNSUPPORTED;
    }
    if (result == AMRNB_3GP_OK) {
        decoder->packet = static_cast<uint8_t *>(
            std::malloc(decoder->info.max_sample_size));
        if (!decoder->packet) result = AMRNB_3GP_ERR_MEMORY;
    }
    if (result == AMRNB_3GP_OK &&
        GSMInitDecode(&decoder->amr_state,
                      reinterpret_cast<Word8 *>(
                          const_cast<char *>("HLV AMR-NB"))) != 0) {
        result = AMRNB_3GP_ERR_MEMORY;
    }
    if (result != AMRNB_3GP_OK) {
        decoder->clear();
        return result;
    }
    *info = decoder->info;
    return AMRNB_3GP_OK;
}

int amrnb_3gp_decoder_decode_next(AmrNb3gpDecoder *decoder, FILE *file,
                                  AmrNb3gpFrame *frame) {
    if (!decoder || !file || !frame || !decoder->amr_state)
        return AMRNB_3GP_ERR_ARGUMENT;
    if (decoder->sample_index >= decoder->info.frame_count)
        return AMRNB_3GP_EOF;
    if (!decoder->sample_in_chunk) {
        const int result = beginChunk(file, decoder);
        if (result != AMRNB_3GP_OK) return result;
    }
    uint32_t size = 0;
    int result = sampleSize(file, decoder, decoder->sample_index, &size);
    if (result != AMRNB_3GP_OK) return result;
    if (!size || size > decoder->info.max_sample_size ||
        !seekFile(file, decoder->sample_offset) ||
        !readExact(file, decoder->packet, size)) {
        return AMRNB_3GP_ERR_IO;
    }

    const uint8_t toc = decoder->packet[0];
    const uint8_t frame_type = static_cast<uint8_t>((toc >> 3) & 0x0f);
    if ((toc & 0x83U) != 0 || (toc & 0x04U) == 0 ||
        (frame_type > 8 && frame_type != 15) ||
        size != static_cast<uint32_t>(kPayloadBytes[frame_type]) + 1U) {
        return AMRNB_3GP_ERR_FORMAT;
    }
    const Word16 consumed = AMRDecode(
        decoder->amr_state, static_cast<Frame_Type_3GPP>(frame_type),
        decoder->packet + 1, decoder->pcm, MIME_IETF);
    if (consumed != kPayloadBytes[frame_type])
        return AMRNB_3GP_ERR_DECODE;

    const uint32_t duration =
        decoder->stts[decoder->stts_index].sample_delta;
    frame->samples = decoder->pcm;
    frame->sample_count = AMRNB_SAMPLES_PER_FRAME;
    frame->frame_type = frame_type;
    frame->timestamp_ticks = decoder->timestamp;
    frame->duration_ticks = duration;
    frame->index = decoder->sample_index;

    decoder->sample_offset += size;
    ++decoder->sample_index;
    ++decoder->sample_in_chunk;
    if (decoder->sample_in_chunk == decoder->samples_in_chunk) {
        decoder->sample_in_chunk = 0;
        ++decoder->chunk_index;
    }
    decoder->timestamp += duration;
    if (--decoder->stts_remaining == 0 &&
        decoder->stts_index + 1 < decoder->stts_count) {
        ++decoder->stts_index;
        decoder->stts_remaining =
            decoder->stts[decoder->stts_index].sample_count;
    }
    return AMRNB_3GP_OK;
}

size_t amrnb_3gp_decoder_memory_bytes(const AmrNb3gpDecoder *decoder) {
    if (!decoder) return 0;
    return sizeof(*decoder) + decoder->info.max_sample_size +
           static_cast<size_t>(decoder->stsc_count) * sizeof(StscEntry) +
           static_cast<size_t>(decoder->stts_count) * sizeof(SttsEntry);
}

const char *amrnb_3gp_strerror(int result) {
    switch (result) {
        case AMRNB_3GP_OK:
            return "success";
        case AMRNB_3GP_EOF:
            return "end of stream";
        case AMRNB_3GP_ERR_ARGUMENT:
            return "invalid argument";
        case AMRNB_3GP_ERR_IO:
            return "I/O error";
        case AMRNB_3GP_ERR_FORMAT:
            return "invalid 3GP/AMR-NB stream";
        case AMRNB_3GP_ERR_UNSUPPORTED:
            return "unsupported 3GP/AMR-NB profile";
        case AMRNB_3GP_ERR_MEMORY:
            return "out of memory";
        case AMRNB_3GP_ERR_DECODE:
            return "AMR-NB decode error";
        default:
            return "unknown 3GP/AMR-NB error";
    }
}

}  // extern "C"
