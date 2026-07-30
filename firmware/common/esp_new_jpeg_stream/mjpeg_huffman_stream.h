/*
 * Streaming entropy input for Espressif esp_new_jpeg 1.0.2.
 *
 * The public decoder API requires a contiguous input buffer.  This adapter
 * replaces only jpeg_dec_huffman while a stream is attached, leaving header
 * parsing, IDCT, colour conversion, and block output in esp_new_jpeg.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_jpeg_dec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*mjpeg_huffman_refill_t)(
    void *context, uint8_t *destination, size_t capacity);

typedef struct {
    jpeg_dec_handle_t decoder;
    uint8_t *buffer;
    size_t capacity;
    mjpeg_huffman_refill_t refill;
    void *refill_context;
    uint32_t refill_count;
    uint32_t refill_bytes;
    bool input_failed;
    bool attached;
} mjpeg_huffman_stream_t;

bool mjpeg_huffman_stream_attach(
    mjpeg_huffman_stream_t *stream,
    jpeg_dec_handle_t decoder,
    uint8_t *buffer,
    size_t capacity,
    mjpeg_huffman_refill_t refill,
    void *refill_context);

void mjpeg_huffman_stream_detach(mjpeg_huffman_stream_t *stream);

#ifdef __cplusplus
}
#endif
