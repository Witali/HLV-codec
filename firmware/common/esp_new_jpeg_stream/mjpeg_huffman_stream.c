/*
 * Copyright (c) 2024 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * This is an ABI-compatible streaming replacement for the entropy decoder in
 * esp_new_jpeg 1.0.2.  The private field layout and algorithm were recovered
 * from the Espressif binary distributed under the ESPRESSIF MIT License; see
 * docs/reverse_engineering/esp_new_jpeg_1.0.2.
 */

#include "mjpeg_huffman_stream.h"

#include <limits.h>
#include <string.h>

#include "esp_attr.h"

/*
 * esp_new_jpeg 1.0.2 private jpeg_decoder_t layout on ESP32.  Keep access
 * limited to the fields required by entropy decoding.  Offsets are verified
 * against DWARF from the pinned component archive.
 */
#define JD_DCTR_OFFSET 12U
#define JD_DPTR_OFFSET 16U
#define JD_GET_BUFFER_OFFSET 20U
#define JD_BITS_LEFT_OFFSET 24U
#define JD_NRST_OFFSET 28U
#define JD_DCV_OFFSET 30U
#define JD_HUFFDCID_OFFSET 148U
#define JD_HUFFACID_OFFSET 151U
#define JD_HUFFMAXCODE_OFFSET 172U
#define JD_HUFFDATA_OFFSET_OFFSET 428U
#define JD_HUFF_LOOK_NBITS_OFFSET 684U
#define JD_HUFF_LOOK_SYM_OFFSET 700U
#define JD_HUFFDATA_OFFSET 780U

#define HUFF_CLASS_COUNT 2U
#define HUFF_TABLE_COUNT 2U
#define HUFF_CODE_LENGTHS 16U
#define HUFF_SYMBOL_COUNT 256U
#define STOCK_KERNEL_REFILL_GUARD 256

typedef struct {
    uint8_t *pointer;
    int32_t count;
    uint32_t bits;
    int32_t bit_count;
    mjpeg_huffman_stream_t *stream;
} bit_reader_t;

static mjpeg_huffman_stream_t *s_active_stream;

extern jpeg_error_t __real_jpeg_dec_huffman(
    void *decoder, int mcu_num, uint8_t component, int16_t *coefficients);

static inline int32_t *jd_dctr(void *decoder) {
    return (int32_t *)((uint8_t *)decoder + JD_DCTR_OFFSET);
}

static inline uint8_t **jd_dptr(void *decoder) {
    return (uint8_t **)((uint8_t *)decoder + JD_DPTR_OFFSET);
}

static inline uint32_t *jd_get_buffer(void *decoder) {
    return (uint32_t *)((uint8_t *)decoder + JD_GET_BUFFER_OFFSET);
}

static inline int32_t *jd_bits_left(void *decoder) {
    return (int32_t *)((uint8_t *)decoder + JD_BITS_LEFT_OFFSET);
}

static inline uint16_t jd_restart_interval(void *decoder) {
    uint16_t value;
    memcpy(&value, (uint8_t *)decoder + JD_NRST_OFFSET, sizeof value);
    return value;
}

static inline int16_t *jd_dc_predictors(void *decoder) {
    return (int16_t *)((uint8_t *)decoder + JD_DCV_OFFSET);
}

static inline uint8_t *jd_huffman_ids(void *decoder, bool ac) {
    return (uint8_t *)decoder +
           (ac ? JD_HUFFACID_OFFSET : JD_HUFFDCID_OFFSET);
}

static inline int32_t (*jd_max_codes(void *decoder))[HUFF_TABLE_COUNT]
                                                    [HUFF_CODE_LENGTHS] {
    return (int32_t (*)[HUFF_TABLE_COUNT][HUFF_CODE_LENGTHS])(
        (uint8_t *)decoder + JD_HUFFMAXCODE_OFFSET);
}

static inline int32_t (*jd_data_offsets(void *decoder))[HUFF_TABLE_COUNT]
                                                       [HUFF_CODE_LENGTHS] {
    return (int32_t (*)[HUFF_TABLE_COUNT][HUFF_CODE_LENGTHS])(
        (uint8_t *)decoder + JD_HUFFDATA_OFFSET_OFFSET);
}

static inline uint8_t *(*jd_look_nbits(void *decoder))[HUFF_TABLE_COUNT] {
    return (uint8_t *(*)[HUFF_TABLE_COUNT])(
        (uint8_t *)decoder + JD_HUFF_LOOK_NBITS_OFFSET);
}

static inline uint8_t *(*jd_look_symbols(void *decoder))[HUFF_TABLE_COUNT] {
    return (uint8_t *(*)[HUFF_TABLE_COUNT])(
        (uint8_t *)decoder + JD_HUFF_LOOK_SYM_OFFSET);
}

static inline uint8_t *jd_huffman_data(
    void *decoder, unsigned table, unsigned table_class) {
    return (uint8_t *)decoder + JD_HUFFDATA_OFFSET +
           (table * HUFF_CLASS_COUNT + table_class) *
               (HUFF_SYMBOL_COUNT * sizeof(uint16_t));
}

static bool refill_reader(bit_reader_t *reader, size_t minimum) {
    size_t retained;
    size_t received;

    if (reader == NULL || reader->stream == NULL ||
        reader->stream->refill == NULL ||
        reader->stream->buffer == NULL ||
        minimum > reader->stream->capacity) {
        return false;
    }
    retained = reader->count > 0 ? (size_t)reader->count : 0U;
    if (retained >= minimum) {
        return true;
    }
    if (retained != 0U && reader->pointer != reader->stream->buffer) {
        memmove(reader->stream->buffer, reader->pointer, retained);
    }
    received = reader->stream->refill(
        reader->stream->refill_context,
        reader->stream->buffer + retained,
        reader->stream->capacity - retained);
    if (received > reader->stream->capacity - retained) {
        reader->stream->input_failed = true;
        return false;
    }
    reader->pointer = reader->stream->buffer;
    reader->count = (int32_t)(retained + received);
    if (received != 0U) {
        ++reader->stream->refill_count;
        reader->stream->refill_bytes += (uint32_t)received;
    }
    return (size_t)reader->count >= minimum;
}

static bool read_entropy_byte(bit_reader_t *reader, uint8_t *value) {
    uint8_t byte;

    if (!refill_reader(reader, 1U)) {
        return false;
    }
    byte = *reader->pointer++;
    --reader->count;
    if (byte != 0xffU) {
        *value = byte;
        return true;
    }
    do {
        if (!refill_reader(reader, 1U)) {
            return false;
        }
        byte = *reader->pointer++;
        --reader->count;
    } while (byte == 0xffU);
    if (byte == 0x00U) {
        *value = 0xffU;
        return true;
    }
    return false;
}

static bool ensure_bits(bit_reader_t *reader, int count) {
    while (reader->bit_count < count) {
        uint8_t byte;
        if (!read_entropy_byte(reader, &byte)) {
            return false;
        }
        reader->bits = (reader->bits << 8) | byte;
        reader->bit_count += 8;
    }
    return true;
}

static bool get_bits(bit_reader_t *reader, int count, uint32_t *value) {
    uint32_t mask;

    if (count == 0) {
        *value = 0U;
        return true;
    }
    if (count < 0 || count > 16 || !ensure_bits(reader, count)) {
        return false;
    }
    reader->bit_count -= count;
    mask = (1U << count) - 1U;
    *value = (reader->bits >> reader->bit_count) & mask;
    return true;
}

static jpeg_error_t decode_symbol(
    void *decoder, bit_reader_t *reader,
    unsigned table, unsigned table_class, uint8_t *symbol) {
    uint32_t code;
    int length;

    if (table >= HUFF_TABLE_COUNT || table_class >= HUFF_CLASS_COUNT) {
        return JPEG_ERR_BAD_DATA;
    }
    if (ensure_bits(reader, 8)) {
        uint8_t *look_nbits =
            jd_look_nbits(decoder)[table][table_class];
        uint8_t *look_symbols =
            jd_look_symbols(decoder)[table][table_class];
        uint32_t look =
            (reader->bits >> (reader->bit_count - 8)) & 0xffU;
        uint8_t used = look_nbits[look];
        if (used != 0U) {
            reader->bit_count -= used;
            *symbol = look_symbols[look];
            return JPEG_ERR_OK;
        }
    }
    if (!get_bits(reader, 1, &code)) {
        return JPEG_ERR_NO_MORE_DATA;
    }
    for (length = 1; length <= 16; ++length) {
        int32_t maximum =
            jd_max_codes(decoder)[table][table_class][length - 1];
        if (maximum >= 0 && code <= (uint32_t)maximum) {
            int32_t index =
                jd_data_offsets(decoder)[table][table_class][length - 1] +
                (int32_t)code;
            if (index < 0 || index >= (int32_t)HUFF_SYMBOL_COUNT) {
                return JPEG_ERR_BAD_DATA;
            }
            *symbol =
                jd_huffman_data(decoder, table, table_class)[index];
            return JPEG_ERR_OK;
        }
        if (length != 16) {
            uint32_t bit;
            if (!get_bits(reader, 1, &bit)) {
                return JPEG_ERR_NO_MORE_DATA;
            }
            code = (code << 1) | bit;
        }
    }
    return JPEG_ERR_BAD_DATA;
}

static jpeg_error_t receive_extend(
    bit_reader_t *reader, unsigned count, int32_t *value) {
    uint32_t bits;
    uint32_t threshold;

    if (count > 16U || !get_bits(reader, (int)count, &bits)) {
        return JPEG_ERR_NO_MORE_DATA;
    }
    if (count == 0U) {
        *value = 0;
        return JPEG_ERR_OK;
    }
    threshold = 1U << (count - 1U);
    *value = bits < threshold
                 ? (int32_t)bits - (int32_t)((1U << count) - 1U)
                 : (int32_t)bits;
    return JPEG_ERR_OK;
}

static jpeg_error_t decode_streaming_huffman(
    void *decoder, int mcu_num, uint8_t component, int16_t *coefficients,
    mjpeg_huffman_stream_t *stream) {
    static const uint8_t zigzag[64] = {
        0, 1, 8, 16, 9, 2, 3, 10,
        17, 24, 32, 25, 18, 11, 4, 5,
        12, 19, 26, 33, 40, 48, 41, 34,
        27, 20, 13, 6, 7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36,
        29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63
    };
    bit_reader_t reader;
    int block;

    if (mcu_num < 0 || component >= 3U ||
        (mcu_num != 0 && coefficients == NULL)) {
        return JPEG_ERR_INVALID_PARAM;
    }
    reader.pointer = *jd_dptr(decoder);
    reader.count = *jd_dctr(decoder);
    reader.bits = *jd_get_buffer(decoder);
    reader.bit_count = *jd_bits_left(decoder);
    reader.stream = stream;
    for (block = 0; block < mcu_num; ++block) {
        uint8_t symbol;
        unsigned dc_table = jd_huffman_ids(decoder, false)[component];
        unsigned ac_table = jd_huffman_ids(decoder, true)[component];
        int32_t difference;
        int coefficient = 1;
        jpeg_error_t result;

        memset(coefficients, 0, 64U * sizeof *coefficients);
        result = decode_symbol(
            decoder, &reader, dc_table, 0U, &symbol);
        if (result != JPEG_ERR_OK || symbol > 11U) {
            return result != JPEG_ERR_OK ? result : JPEG_ERR_BAD_DATA;
        }
        result = receive_extend(&reader, symbol, &difference);
        if (result != JPEG_ERR_OK) {
            return result;
        }
        jd_dc_predictors(decoder)[component] =
            (int16_t)(jd_dc_predictors(decoder)[component] + difference);
        coefficients[0] = jd_dc_predictors(decoder)[component];

        while (coefficient < 64) {
            unsigned run;
            unsigned size;
            result = decode_symbol(
                decoder, &reader, ac_table, 1U, &symbol);
            if (result != JPEG_ERR_OK) {
                return result;
            }
            run = symbol >> 4;
            size = symbol & 0x0fU;
            if (size == 0U) {
                if (run != 15U) {
                    break;
                }
                coefficient += 16;
                continue;
            }
            coefficient += (int)run;
            if (coefficient >= 64 || size > 10U) {
                return JPEG_ERR_BAD_DATA;
            }
            result = receive_extend(&reader, size, &difference);
            if (result != JPEG_ERR_OK) {
                return result;
            }
            coefficients[zigzag[coefficient]] = (int16_t)difference;
            ++coefficient;
        }
        coefficients += 64;
    }

    /*
     * Restart markers are consumed by esp_new_jpeg's MCU loop outside this
     * replacement.  Keep the two marker bytes contiguous even when they fall
     * on a refill boundary.
     */
    if (jd_restart_interval(decoder) != 0U &&
        reader.count < 2 && !refill_reader(&reader, 2U)) {
        return JPEG_ERR_NO_MORE_DATA;
    }
    *jd_dptr(decoder) = reader.pointer;
    *jd_dctr(decoder) = reader.count;
    *jd_get_buffer(decoder) = reader.bits;
    *jd_bits_left(decoder) = reader.bit_count;
    return JPEG_ERR_OK;
}

bool mjpeg_huffman_stream_attach(
    mjpeg_huffman_stream_t *stream,
    jpeg_dec_handle_t decoder,
    uint8_t *buffer,
    size_t capacity,
    mjpeg_huffman_refill_t refill,
    void *refill_context) {
    if (stream == NULL || decoder == NULL || buffer == NULL ||
        capacity < 32U || capacity > INT32_MAX || refill == NULL ||
        s_active_stream != NULL) {
        return false;
    }
    memset(stream, 0, sizeof *stream);
    stream->decoder = decoder;
    stream->buffer = buffer;
    stream->capacity = capacity;
    stream->refill = refill;
    stream->refill_context = refill_context;
    stream->attached = true;
    s_active_stream = stream;
    return true;
}

void mjpeg_huffman_stream_detach(mjpeg_huffman_stream_t *stream) {
    if (stream != NULL) {
        if (s_active_stream == stream) {
            s_active_stream = NULL;
        }
        stream->attached = false;
    }
}

jpeg_error_t IRAM_ATTR __wrap_jpeg_dec_huffman(
    void *decoder, int mcu_num, uint8_t component, int16_t *coefficients) {
    mjpeg_huffman_stream_t *stream = s_active_stream;
    jpeg_error_t result;
    bit_reader_t reader;
    int16_t saved_predictors[3];

    if (stream == NULL || !stream->attached ||
        stream->decoder != decoder) {
        return __real_jpeg_dec_huffman(
            decoder, mcu_num, component, coefficients);
    }
    /*
     * Keep the stock optimized kernel on the common path.  Refill between
     * block calls while its private pointer is stable.  If an unusually
     * large block still crosses the boundary, the stock function returns
     * NO_MORE_DATA without committing its local bit state, so the streaming
     * implementation can safely decode the same block from the saved state.
     */
    reader.pointer = *jd_dptr(decoder);
    reader.count = *jd_dctr(decoder);
    reader.bits = *jd_get_buffer(decoder);
    reader.bit_count = *jd_bits_left(decoder);
    reader.stream = stream;
    if (reader.count < STOCK_KERNEL_REFILL_GUARD) {
        (void)refill_reader(&reader, stream->capacity);
        *jd_dptr(decoder) = reader.pointer;
        *jd_dctr(decoder) = reader.count;
    }
    memcpy(saved_predictors, jd_dc_predictors(decoder),
           sizeof saved_predictors);
    result = __real_jpeg_dec_huffman(
        decoder, mcu_num, component, coefficients);
    if (result != JPEG_ERR_NO_MORE_DATA) {
        return result;
    }
    memcpy(jd_dc_predictors(decoder), saved_predictors,
           sizeof saved_predictors);
    return decode_streaming_huffman(
        decoder, mcu_num, component, coefficients, stream);
}
