#include "h263_3gp.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include "mp4dec_api.h"

namespace {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(d));
}

constexpr size_t kInputPadding = 8;

bool isSupportedGeometry(uint16_t width, uint16_t height) {
    return (width == 176 && height == 144) ||
           (width == 256 && (height == 144 || height == 192)) ||
           (width == 320 && (height == 180 || height == 240));
}

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
    return offset <= static_cast<uint64_t>(
                         std::numeric_limits<off_t>::max()) &&
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

uint32_t gcd32(uint32_t a, uint32_t b) {
    while (b != 0) {
        const uint32_t next = a % b;
        a = b;
        b = next;
    }
    return a ? a : 1;
}

}  // namespace

struct H2633gpDecoder {
    VideoDecControls controls{};
    H2633gpInfo info{};
    uint8_t *packet = nullptr;
    uint8_t *output_y[2]{};
    uint8_t *output_u[2]{};
    uint8_t *output_v[2]{};
    size_t output_bytes = 0;
    uint8_t output_count = 0;
    uint16_t buffer_width = 0;
    uint16_t buffer_height = 0;
    bool intra_only = false;
    bool pv_ready = false;

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
        if (pv_ready) PVCleanUpVideoDecoder(&controls);
        pv_ready = false;
        std::free(packet);
        packet = nullptr;
        for (uint8_t i = 0; i < 2; ++i) {
            std::free(output_y[i]);
            std::free(output_u[i]);
            std::free(output_v[i]);
            output_y[i] = nullptr;
            output_u[i] = nullptr;
            output_v[i] = nullptr;
        }
        std::free(stsc);
        stsc = nullptr;
        std::free(stts);
        stts = nullptr;
        *this = H2633gpDecoder{};
    }
};

namespace {

int parseMediaHeader(FILE *file, const Box &mdhd, H2633gpInfo *info) {
    if (!seekFile(file, mdhd.data)) return H263_3GP_ERR_IO;
    uint32_t version_flags = 0;
    if (!readU32(file, &version_flags)) return H263_3GP_ERR_IO;
    const uint8_t version = static_cast<uint8_t>(version_flags >> 24);
    if (version == 0) {
        uint32_t ignored = 0;
        uint32_t duration = 0;
        if (!readU32(file, &ignored) || !readU32(file, &ignored) ||
            !readU32(file, &info->timescale) ||
            !readU32(file, &duration)) {
            return H263_3GP_ERR_IO;
        }
        info->duration_ticks = duration;
    } else if (version == 1) {
        uint64_t ignored = 0;
        if (!readU64(file, &ignored) || !readU64(file, &ignored) ||
            !readU32(file, &info->timescale) ||
            !readU64(file, &info->duration_ticks)) {
            return H263_3GP_ERR_IO;
        }
    } else {
        return H263_3GP_ERR_UNSUPPORTED;
    }
    return info->timescale ? H263_3GP_OK : H263_3GP_ERR_FORMAT;
}

int parseSampleDescription(FILE *file, const Box &stsd, H2633gpInfo *info) {
    if (!seekFile(file, stsd.data + 4)) return H263_3GP_ERR_IO;
    uint32_t count = 0;
    if (!readU32(file, &count)) return H263_3GP_ERR_IO;
    uint64_t cursor = stsd.data + 8;
    for (uint32_t index = 0; index < count && cursor < stsd.end; ++index) {
        if (!seekFile(file, cursor)) return H263_3GP_ERR_IO;
        Box entry;
        if (!readBox(file, stsd.end, &entry)) return H263_3GP_ERR_FORMAT;
        if (entry.type == fourcc('s', '2', '6', '3')) {
            if (entry.end - entry.data < 78 ||
                !seekFile(file, entry.data + 24) ||
                !readU16(file, &info->width) ||
                !readU16(file, &info->height)) {
                return H263_3GP_ERR_FORMAT;
            }
            info->profile = 0;
            info->level = 0;
            Box d263;
            if (findChild(file, entry, fourcc('d', '2', '6', '3'), &d263,
                          entry.data + 78) &&
                d263.end - d263.data >= 7 &&
                seekFile(file, d263.data + 5)) {
                uint8_t values[2];
                if (!readExact(file, values, sizeof values))
                    return H263_3GP_ERR_IO;
                info->level = values[0];
                info->profile = values[1];
            }
            return H263_3GP_OK;
        }
        cursor = entry.end;
    }
    return H263_3GP_ERR_UNSUPPORTED;
}

int parseStsz(FILE *file, const Box &stsz, H2633gpDecoder *decoder) {
    if (!seekFile(file, stsz.data + 4) ||
        !readU32(file, &decoder->fixed_sample_size) ||
        !readU32(file, &decoder->info.frame_count)) {
        return H263_3GP_ERR_IO;
    }
    if (decoder->info.frame_count == 0) return H263_3GP_ERR_FORMAT;
    decoder->stsz_entries = stsz.data + 12;
    if (decoder->fixed_sample_size != 0) {
        decoder->info.max_sample_size = decoder->fixed_sample_size;
        return H263_3GP_OK;
    }
    if (stsz.end - decoder->stsz_entries <
        static_cast<uint64_t>(decoder->info.frame_count) * 4) {
        return H263_3GP_ERR_FORMAT;
    }
    if (!seekFile(file, decoder->stsz_entries)) return H263_3GP_ERR_IO;
    for (uint32_t i = 0; i < decoder->info.frame_count; ++i) {
        uint32_t size = 0;
        if (!readU32(file, &size)) return H263_3GP_ERR_IO;
        decoder->info.max_sample_size =
            std::max(decoder->info.max_sample_size, size);
    }
    return decoder->info.max_sample_size ? H263_3GP_OK
                                         : H263_3GP_ERR_FORMAT;
}

int parseChunks(FILE *file, const Box &box, H2633gpDecoder *decoder) {
    if (!seekFile(file, box.data + 4) ||
        !readU32(file, &decoder->chunk_count)) {
        return H263_3GP_ERR_IO;
    }
    if (decoder->chunk_count == 0) return H263_3GP_ERR_FORMAT;
    decoder->chunks_are_64_bit = box.type == fourcc('c', 'o', '6', '4');
    decoder->chunk_entries = box.data + 8;
    const uint64_t entry_size = decoder->chunks_are_64_bit ? 8 : 4;
    return box.end - decoder->chunk_entries >=
                   static_cast<uint64_t>(decoder->chunk_count) * entry_size
               ? H263_3GP_OK
               : H263_3GP_ERR_FORMAT;
}

int parseStsc(FILE *file, const Box &stsc, H2633gpDecoder *decoder) {
    if (!seekFile(file, stsc.data + 4) ||
        !readU32(file, &decoder->stsc_count)) {
        return H263_3GP_ERR_IO;
    }
    if (decoder->stsc_count == 0 ||
        decoder->stsc_count >
            std::numeric_limits<size_t>::max() / sizeof(StscEntry)) {
        return H263_3GP_ERR_FORMAT;
    }
    decoder->stsc = static_cast<StscEntry *>(
        std::calloc(decoder->stsc_count, sizeof(StscEntry)));
    if (!decoder->stsc) return H263_3GP_ERR_MEMORY;
    if (stsc.end - (stsc.data + 8) <
        static_cast<uint64_t>(decoder->stsc_count) * 12) {
        return H263_3GP_ERR_FORMAT;
    }
    for (uint32_t i = 0; i < decoder->stsc_count; ++i) {
        uint32_t ignored = 0;
        if (!readU32(file, &decoder->stsc[i].first_chunk) ||
            !readU32(file, &decoder->stsc[i].samples_per_chunk) ||
            !readU32(file, &ignored)) {
            return H263_3GP_ERR_IO;
        }
        if (decoder->stsc[i].first_chunk == 0 ||
            decoder->stsc[i].samples_per_chunk == 0 ||
            (i && decoder->stsc[i].first_chunk <=
                      decoder->stsc[i - 1].first_chunk)) {
            return H263_3GP_ERR_FORMAT;
        }
    }
    return decoder->stsc[0].first_chunk == 1 ? H263_3GP_OK
                                             : H263_3GP_ERR_FORMAT;
}

int parseStts(FILE *file, const Box &stts, H2633gpDecoder *decoder) {
    if (!seekFile(file, stts.data + 4) ||
        !readU32(file, &decoder->stts_count)) {
        return H263_3GP_ERR_IO;
    }
    if (decoder->stts_count != 1 ||
        decoder->stts_count >
            std::numeric_limits<size_t>::max() / sizeof(SttsEntry)) {
        return H263_3GP_ERR_UNSUPPORTED;
    }
    decoder->stts = static_cast<SttsEntry *>(
        std::calloc(decoder->stts_count, sizeof(SttsEntry)));
    if (!decoder->stts) return H263_3GP_ERR_MEMORY;
    if (stts.end - (stts.data + 8) <
        static_cast<uint64_t>(decoder->stts_count) * 8) {
        return H263_3GP_ERR_FORMAT;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < decoder->stts_count; ++i) {
        if (!readU32(file, &decoder->stts[i].sample_count) ||
            !readU32(file, &decoder->stts[i].sample_delta)) {
            return H263_3GP_ERR_IO;
        }
        if (decoder->stts[i].sample_count == 0 ||
            decoder->stts[i].sample_delta == 0) {
            return H263_3GP_ERR_FORMAT;
        }
        total += decoder->stts[i].sample_count;
    }
    if (total != decoder->info.frame_count) return H263_3GP_ERR_FORMAT;
    decoder->stts_remaining = decoder->stts[0].sample_count;
    const uint32_t divisor =
        gcd32(decoder->info.timescale, decoder->stts[0].sample_delta);
    decoder->info.fps_num = decoder->info.timescale / divisor;
    decoder->info.fps_den = decoder->stts[0].sample_delta / divisor;
    return H263_3GP_OK;
}

int parseTrack(FILE *file, const Box &trak, H2633gpDecoder *decoder) {
    Box mdia;
    Box hdlr;
    if (!findChild(file, trak, fourcc('m', 'd', 'i', 'a'), &mdia) ||
        !findChild(file, mdia, fourcc('h', 'd', 'l', 'r'), &hdlr)) {
        return H263_3GP_ERR_FORMAT;
    }
    if (!seekFile(file, hdlr.data + 8)) return H263_3GP_ERR_IO;
    uint32_t handler = 0;
    if (!readU32(file, &handler)) return H263_3GP_ERR_IO;
    if (handler != fourcc('v', 'i', 'd', 'e'))
        return H263_3GP_ERR_UNSUPPORTED;

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
        return H263_3GP_ERR_FORMAT;
    }
    if (!findChild(file, stbl, fourcc('s', 't', 'c', 'o'), &chunks) &&
        !findChild(file, stbl, fourcc('c', 'o', '6', '4'), &chunks)) {
        return H263_3GP_ERR_FORMAT;
    }

    int result = parseMediaHeader(file, mdhd, &decoder->info);
    if (result == H263_3GP_OK)
        result = parseSampleDescription(file, stsd, &decoder->info);
    if (result == H263_3GP_OK) result = parseStsz(file, stsz, decoder);
    if (result == H263_3GP_OK) result = parseChunks(file, chunks, decoder);
    if (result == H263_3GP_OK) result = parseStsc(file, stsc, decoder);
    if (result == H263_3GP_OK) result = parseStts(file, stts, decoder);
    return result;
}

int parseContainer(FILE *file, H2633gpDecoder *decoder) {
    uint64_t size = 0;
    if (!fileSize(file, &size)) return H263_3GP_ERR_IO;
    Box root;
    root.data = 0;
    root.end = size;
    Box ftyp;
    Box moov;
    if (!findChild(file, root, fourcc('f', 't', 'y', 'p'), &ftyp) ||
        !findChild(file, root, fourcc('m', 'o', 'o', 'v'), &moov)) {
        return H263_3GP_ERR_FORMAT;
    }
    uint64_t cursor = moov.data;
    bool saw_video = false;
    while (cursor < moov.end) {
        if (!seekFile(file, cursor)) return H263_3GP_ERR_IO;
        Box child;
        if (!readBox(file, moov.end, &child)) return H263_3GP_ERR_FORMAT;
        if (child.type == fourcc('t', 'r', 'a', 'k')) {
            const int result = parseTrack(file, child, decoder);
            if (result == H263_3GP_OK) {
                saw_video = true;
                break;
            }
            if (result != H263_3GP_ERR_UNSUPPORTED) return result;
        }
        cursor = child.end;
    }
    if (!saw_video) return H263_3GP_ERR_UNSUPPORTED;
    if (!isSupportedGeometry(decoder->info.width, decoder->info.height) ||
        decoder->info.profile != 0) {
        return H263_3GP_ERR_UNSUPPORTED;
    }
    return H263_3GP_OK;
}

int sampleSize(FILE *file, const H2633gpDecoder *decoder, uint32_t index,
               uint32_t *size) {
    if (decoder->fixed_sample_size) {
        *size = decoder->fixed_sample_size;
        return H263_3GP_OK;
    }
    if (!seekFile(file, decoder->stsz_entries +
                            static_cast<uint64_t>(index) * 4) ||
        !readU32(file, size)) {
        return H263_3GP_ERR_IO;
    }
    return *size ? H263_3GP_OK : H263_3GP_ERR_FORMAT;
}

int beginChunk(FILE *file, H2633gpDecoder *decoder) {
    if (decoder->chunk_index >= decoder->chunk_count)
        return H263_3GP_ERR_FORMAT;
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
    if (!seekFile(file, entry_offset)) return H263_3GP_ERR_IO;
    if (decoder->chunks_are_64_bit) {
        if (!readU64(file, &decoder->sample_offset))
            return H263_3GP_ERR_IO;
    } else {
        uint32_t offset = 0;
        if (!readU32(file, &offset)) return H263_3GP_ERR_IO;
        decoder->sample_offset = offset;
    }
    return H263_3GP_OK;
}

int initializeDecoder(H2633gpDecoder *decoder) {
    const int32 expected_width =
        (static_cast<int32>(decoder->info.width) + 15) & -16;
    const int32 expected_height =
        (static_cast<int32>(decoder->info.height) + 15) & -16;
    decoder->buffer_width = static_cast<uint16_t>(expected_width);
    decoder->buffer_height = static_cast<uint16_t>(expected_height);
    decoder->output_bytes =
        static_cast<size_t>(expected_width) * expected_height * 3 / 2;
    decoder->intra_only = decoder->info.width != 176;
    decoder->output_count = decoder->intra_only ? 1 : 2;

    // Reserve the frame planes before PacketVideo makes its smaller table
    // allocations. Separate Y/U/V blocks avoid requiring one contiguous
    // 115,200-byte allocation at 320x240. H.263+ profiles are intra-only, so
    // one set of planes can serve as current output and nominal reference.
    const size_t y_bytes =
        static_cast<size_t>(expected_width) * expected_height;
    const size_t chroma_bytes = y_bytes / 4;
    for (uint8_t i = 0; i < decoder->output_count; ++i) {
        decoder->output_y[i] =
            static_cast<uint8_t *>(std::malloc(y_bytes));
        decoder->output_u[i] =
            static_cast<uint8_t *>(std::malloc(chroma_bytes));
        decoder->output_v[i] =
            static_cast<uint8_t *>(std::malloc(chroma_bytes));
        if (!decoder->output_y[i] || !decoder->output_u[i] ||
            !decoder->output_v[i]) {
            return H263_3GP_ERR_FRAME_MEMORY;
        }
        std::memset(decoder->output_y[i], 0, y_bytes);
        std::memset(decoder->output_u[i], 0, chroma_bytes);
        std::memset(decoder->output_v[i], 0, chroma_bytes);
    }

    uint8 *vol_data[1] = {nullptr};
    int32 vol_size[1] = {0};
    if (!PVInitVideoDecoder(&decoder->controls, vol_data, vol_size, 1,
                            decoder->info.width, decoder->info.height,
                            H263_MODE)) {
        return H263_3GP_ERR_DECODER_MEMORY;
    }
    decoder->pv_ready = true;
    PVSetPostProcType(&decoder->controls, PV_NO_POST_PROC);
    int32 buffer_width = 0;
    int32 buffer_height = 0;
    PVGetBufferDimensions(&decoder->controls, &buffer_width, &buffer_height);
    if (buffer_width != expected_width ||
        buffer_height != expected_height ||
        decoder->controls.size != buffer_width * buffer_height ||
        buffer_width > UINT16_MAX || buffer_height > UINT16_MAX) {
        return H263_3GP_ERR_UNSUPPORTED;
    }
    const uint8_t reference = decoder->output_count - 1;
    PVSetReferenceYUVPlanes(
        &decoder->controls,
        decoder->output_y[reference],
        decoder->output_u[reference],
        decoder->output_v[reference]);
    return H263_3GP_OK;
}

}  // namespace

extern "C" {

H2633gpDecoder *h263_3gp_decoder_create(void) {
    return new (std::nothrow) H2633gpDecoder();
}

void h263_3gp_decoder_destroy(H2633gpDecoder *decoder) {
    if (!decoder) return;
    decoder->clear();
    delete decoder;
}

int h263_3gp_decoder_open(H2633gpDecoder *decoder, FILE *file,
                          H2633gpInfo *info) {
    if (!decoder || !file || !info) return H263_3GP_ERR_ARGUMENT;
    decoder->clear();
    int result = parseContainer(file, decoder);
    if (result == H263_3GP_OK &&
        decoder->info.max_sample_size >
            std::numeric_limits<size_t>::max() - kInputPadding) {
        result = H263_3GP_ERR_UNSUPPORTED;
    }
    if (result == H263_3GP_OK) result = initializeDecoder(decoder);
    if (result == H263_3GP_OK) {
        decoder->packet = static_cast<uint8_t *>(
            std::malloc(decoder->info.max_sample_size + kInputPadding));
        if (!decoder->packet) result = H263_3GP_ERR_PACKET_MEMORY;
    }
    if (result != H263_3GP_OK) {
        decoder->clear();
        return result;
    }
    *info = decoder->info;
    return H263_3GP_OK;
}

int h263_3gp_decoder_decode_next(H2633gpDecoder *decoder, FILE *file,
                                 H2633gpFrame *frame) {
    if (!decoder || !file || !frame || !decoder->pv_ready)
        return H263_3GP_ERR_ARGUMENT;
    if (decoder->sample_index >= decoder->info.frame_count)
        return H263_3GP_EOF;
    if (decoder->sample_in_chunk == 0) {
        const int result = beginChunk(file, decoder);
        if (result != H263_3GP_OK) return result;
    }
    uint32_t size = 0;
    int result = sampleSize(file, decoder, decoder->sample_index, &size);
    if (result != H263_3GP_OK) return result;
    if (size > decoder->info.max_sample_size)
        return H263_3GP_ERR_FORMAT;
    if (!seekFile(file, decoder->sample_offset) ||
        !readExact(file, decoder->packet, size)) {
        return H263_3GP_ERR_IO;
    }
    std::memset(decoder->packet + size, 0, kInputPadding);

    uint8 *bitstream = decoder->packet;
    int32 input_size = static_cast<int32>(size);
    uint32 timestamp = static_cast<uint32_t>(
        std::min<uint64_t>(decoder->timestamp, UINT32_MAX));
    uint use_external_timestamp = 1;
    VopHeaderInfo header{};
    const uint8_t output_index =
        decoder->output_count == 1
            ? 0
            : (decoder->sample_index & 1U);
    uint8_t *output = decoder->output_y[output_index];
    if (!PVDecodeVopHeader(&decoder->controls, &bitstream, &timestamp,
                           &input_size, &header, &use_external_timestamp,
                           output)) {
        return H263_3GP_ERR_DECODE;
    }
    if (decoder->intra_only && header.frameType != MP4_I_FRAME)
        return H263_3GP_ERR_UNSUPPORTED;
    PVSetCurrentYUVPlanes(
        &decoder->controls,
        decoder->output_y[output_index],
        decoder->output_u[output_index],
        decoder->output_v[output_index]);
    int32 width = 0;
    int32 height = 0;
    PVGetVideoDimensions(&decoder->controls, &width, &height);
    if (width != decoder->info.width || height != decoder->info.height)
        return H263_3GP_ERR_UNSUPPORTED;
    if (!PVDecodeVopBody(&decoder->controls, &input_size))
        return H263_3GP_ERR_DECODE;

    const uint32_t duration =
        decoder->stts[decoder->stts_index].sample_delta;
    frame->y = decoder->output_y[output_index];
    frame->u = decoder->output_u[output_index];
    frame->v = decoder->output_v[output_index];
    frame->width = decoder->info.width;
    frame->height = decoder->info.height;
    frame->y_stride = decoder->buffer_width;
    frame->chroma_stride = decoder->buffer_width / 2;
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
    return H263_3GP_OK;
}

size_t h263_3gp_decoder_memory_bytes(const H2633gpDecoder *decoder) {
    if (!decoder) return 0;
    return sizeof(*decoder) + decoder->info.max_sample_size + kInputPadding +
           decoder->output_bytes * decoder->output_count +
           static_cast<size_t>(decoder->stsc_count) * sizeof(StscEntry) +
           static_cast<size_t>(decoder->stts_count) * sizeof(SttsEntry) +
           (decoder->pv_ready
                ? static_cast<size_t>(PVGetDecMemoryUsage(
                      const_cast<VideoDecControls *>(&decoder->controls)))
                : 0);
}

const char *h263_3gp_strerror(int result) {
    switch (result) {
        case H263_3GP_OK:
            return "success";
        case H263_3GP_EOF:
            return "end of stream";
        case H263_3GP_ERR_ARGUMENT:
            return "invalid argument";
        case H263_3GP_ERR_IO:
            return "I/O error";
        case H263_3GP_ERR_FORMAT:
            return "invalid 3GP container";
        case H263_3GP_ERR_UNSUPPORTED:
            return "unsupported 3GP/H.263 profile";
        case H263_3GP_ERR_MEMORY:
            return "out of memory";
        case H263_3GP_ERR_FRAME_MEMORY:
            return "frame buffer memory";
        case H263_3GP_ERR_DECODER_MEMORY:
            return "decoder table memory";
        case H263_3GP_ERR_PACKET_MEMORY:
            return "compressed packet memory";
        case H263_3GP_ERR_DECODE:
            return "H.263 decode error";
        default:
            return "unknown H.263/3GP error";
    }
}

}  // extern "C"
