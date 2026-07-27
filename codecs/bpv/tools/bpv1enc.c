#define _CRT_SECURE_NO_WARNINGS

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#define BPV_VERSION 6
#define BLOCK_SIZE 4
#define PIXELS_PER_BLOCK 16
#define PALETTE_COUNT 64
#define COLORS_PER_PALETTE 16
#define LOCAL_COLORS 4
#define DIRECT_RECORD_FLAG 0x80
#define RECORD_BYTES 9
#define PATTERN_BYTES 4
#define MAX_CANDIDATE_PALETTES 8

enum {
    MODE_SKIP = 0,
    MODE_MOTION = 1,
    MODE_BLOCK_DICT = 2,
    MODE_RAW = 3,
    MODE_COUNT = 4
};

typedef struct {
    uint8_t palette_index;
    uint8_t local_colors[LOCAL_COLORS];
    uint8_t pattern[PATTERN_BYTES];
} Block;

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} Buffer;

typedef struct {
    int width;
    int height;
    int fps_numerator;
    int fps_denominator;
    int chroma_width;
    int chroma_height;
    size_t luma_bytes;
    size_t chroma_bytes;
    size_t frame_bytes;
} Y4mInfo;

typedef struct {
    float descriptor[6];
    uint8_t pixels[PIXELS_PER_BLOCK][3];
} SampleBlock;

typedef struct {
    SampleBlock *blocks;
    size_t count;
    size_t capacity;
    uint64_t seen;
    uint64_t random_state;
} SampleReservoir;

typedef struct {
    float block_centers[PALETTE_COUNT][6];
    uint8_t palette[PALETTE_COUNT][COLORS_PER_PALETTE][3];
} Training;

typedef struct {
    int threads;
    int gop;
    int minimum_gop;
    double scene_threshold;
    double lambda;
    int candidate_palettes;
    int search_radius;
    int max_block_dictionary;
    size_t maximum_sample_blocks;
    int sample_blocks_per_frame;
    int block_iterations;
    int color_iterations;
    size_t maximum_colors_per_cluster;
    uint32_t max_frames;
    const char *report_path;
    int force;
    int progress;
    const char *audio_path;
    int audio_rate;
    int active_palettes;
    const char *active_palette_path;
} Options;

typedef struct {
    FILE *file;
    uint32_t sample_rate;
    uint64_t phase;
    uint64_t bytes;
} AudioInput;

typedef struct {
    uint64_t mode_counts[MODE_COUNT];
    uint64_t raw_1;
    uint64_t raw_2;
    uint64_t raw_4;
    uint64_t direct_5_to_8;
    uint64_t direct_9_to_16;
    uint64_t squared_error;
    uint64_t samples;
    uint64_t previous_decisions;
    uint64_t quantized_decisions;
} EncodeStats;

typedef struct {
    Block *items;
    int count;
    int capacity;
} BlockDictionary;

typedef struct {
    const Options *options;
    const Training *training;
    const Y4mInfo *info;
    uint8_t *frames;
    int frame_count;
    int first_frame;
    size_t gop_index;
    Buffer output;
    EncodeStats stats;
    int result;
} GopJob;

typedef struct {
    uint32_t first_frame;
    double scene_score;
    int scene_cut;
} GopEntry;

typedef struct {
    GopEntry *entries;
    size_t count;
    size_t capacity;
} GopPlan;

_Static_assert(sizeof(Block) == RECORD_BYTES, "BPV1 Block must occupy 9 bytes");

static void usage(FILE *stream) {
    fprintf(stream,
        "Usage:\n"
        "  bpv1enc <input.y4m> <output.bpv1> [options]\n\n"
        "Options:\n"
        "  --threads N                    GOP workers 1..16 (default 8)\n"
        "  --gop N                        maximum GOP interval (default 48)\n"
        "  --min-gop N                    minimum scene GOP (default 12)\n"
        "  --scene-threshold N            cut score 0..1 (default 0.35)\n"
        "  --no-scene-cuts                use only fixed GOP boundaries\n"
        "  --lambda N                     RD multiplier (default 64)\n"
        "  --candidate-palettes N         nearby palettes 1..8 (default 3)\n"
        "  --search-radius N              motion radius 0..7 blocks (default 2)\n"
        "  --sample-blocks N              palette reservoir (default 32768)\n"
        "  --samples-per-frame N          training blocks/frame (default 256)\n"
        "  --block-iterations N           block k-means passes (default 10)\n"
        "  --color-iterations N           color k-means passes (default 10)\n"
        "  --colors-per-cluster N         color samples/palette (default 8192)\n"
        "  --max-block-dictionary N       block dictionary entries (default 256)\n"
        "  --max-frames N                 encode a leading test fragment\n"
        "  --audio-u8 FILE                mux unsigned 8-bit mono raw PCM\n"
        "  --audio-rate N                 PCM sample rate (default 16000)\n"
        "  --active-palettes              train/transmit one bank per GOP (default)\n"
        "  --active-palette-file FILE     use consecutive 64x16 RGB banks per GOP\n"
        "  --fixed-palettes               use one legacy bank for the whole file\n"
        "  --report FILE                  write JSON metrics\n"
        "  --force                        replace output\n"
        "  --no-progress                  suppress progress\n"
        "  -h, --help                     show help\n\n"
        "The input must be a seekable 8-bit YUV 4:2:0 Y4M file. In the default\n"
        "BPV1 v6 mode every independent GOP carries a 64x16 RGB palette bank\n"
        "and uses a 2-bit mode map. Unified RAW records occupy 2 bytes for\n"
        "one color, 4 bytes for two, 7 bytes for a four-color block, or\n"
        "9 bytes for 5-16 direct colors. --fixed-palettes repeats one global\n"
        "bank in each GOP.\n");
}

static int buffer_reserve(Buffer *buffer, size_t extra) {
    size_t required;
    size_t capacity;
    uint8_t *replacement;
    if (extra > SIZE_MAX - buffer->size) return -1;
    required = buffer->size + extra;
    if (required <= buffer->capacity) return 0;
    capacity = buffer->capacity ? buffer->capacity : 4096;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    replacement = (uint8_t *)realloc(buffer->data, capacity);
    if (!replacement) return -1;
    buffer->data = replacement;
    buffer->capacity = capacity;
    return 0;
}

static int buffer_write(Buffer *buffer, const void *data, size_t size) {
    if (buffer_reserve(buffer, size)) return -1;
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return 0;
}

static int buffer_u8(Buffer *buffer, uint8_t value) {
    return buffer_write(buffer, &value, 1);
}

static int buffer_u16(Buffer *buffer, uint16_t value) {
    uint8_t bytes[2] = {
        (uint8_t)(value & 255),
        (uint8_t)((value >> 8) & 255)
    };
    return buffer_write(buffer, bytes, sizeof bytes);
}

static int buffer_u32(Buffer *buffer, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)(value & 255),
        (uint8_t)((value >> 8) & 255),
        (uint8_t)((value >> 16) & 255),
        (uint8_t)((value >> 24) & 255)
    };
    return buffer_write(buffer, bytes, sizeof bytes);
}

static void buffer_free(Buffer *buffer) {
    free(buffer->data);
    memset(buffer, 0, sizeof *buffer);
}

static int read_line(FILE *file, char *line, size_t capacity) {
    size_t length = 0;
    int value;
    if (!capacity) return -1;
    while ((value = fgetc(file)) != EOF) {
        if (value == '\n') break;
        if (length + 1 >= capacity) return -1;
        if (value != '\r') line[length++] = (char)value;
    }
    if (value == EOF && length == 0) return 0;
    line[length] = '\0';
    return 1;
}

static int parse_positive(const char *text, int *value) {
    char *end = NULL;
    long parsed;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno || !end || *end || parsed <= 0 || parsed > 65535) return -1;
    *value = (int)parsed;
    return 0;
}

static int parse_y4m_header(FILE *file, Y4mInfo *info) {
    char line[4096];
    char *token;
    int width = 0, height = 0, fps_n = 25, fps_d = 1;
    int result = read_line(file, line, sizeof line);
    if (result <= 0 || strncmp(line, "YUV4MPEG2", 9)) {
        fprintf(stderr, "bpv1enc: invalid Y4M header\n");
        return -1;
    }
    token = strtok(line + 9, " ");
    while (token) {
        if (token[0] == 'W' && parse_positive(token + 1, &width)) return -1;
        if (token[0] == 'H' && parse_positive(token + 1, &height)) return -1;
        if (token[0] == 'F') {
            char *separator = strchr(token + 1, ':');
            if (!separator) return -1;
            *separator = '\0';
            if (parse_positive(token + 1, &fps_n) ||
                parse_positive(separator + 1, &fps_d)) return -1;
        }
        if (token[0] == 'C' &&
            strncmp(token + 1, "420", 3)) {
            fprintf(stderr, "bpv1enc: only 8-bit YUV 4:2:0 Y4M is supported\n");
            return -1;
        }
        token = strtok(NULL, " ");
    }
    if (!width || !height) return -1;
    memset(info, 0, sizeof *info);
    info->width = width;
    info->height = height;
    info->fps_numerator = fps_n;
    info->fps_denominator = fps_d;
    info->chroma_width = (width + 1) / 2;
    info->chroma_height = (height + 1) / 2;
    info->luma_bytes = (size_t)width * (size_t)height;
    info->chroma_bytes =
        (size_t)info->chroma_width * (size_t)info->chroma_height;
    if (info->luma_bytes > SIZE_MAX - info->chroma_bytes * 2) return -1;
    info->frame_bytes = info->luma_bytes + info->chroma_bytes * 2;
    return 0;
}

static int read_y4m_frame(FILE *file, const Y4mInfo *info, uint8_t *frame) {
    char line[4096];
    int result = read_line(file, line, sizeof line);
    if (result == 0) return 0;
    if (result < 0 || strncmp(line, "FRAME", 5) ||
        (line[5] && line[5] != ' ')) {
        fprintf(stderr, "bpv1enc: invalid Y4M frame marker\n");
        return -1;
    }
    if (fread(frame, 1, info->frame_bytes, file) != info->frame_bytes) {
        fprintf(stderr, "bpv1enc: truncated Y4M frame\n");
        return -1;
    }
    return 1;
}

static uint8_t clamp_byte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

static void yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t rgb[3]) {
    int c = (int)y - 16;
    int d = (int)u - 128;
    int e = (int)v - 128;
    if (c < 0) c = 0;
    rgb[0] = clamp_byte((298 * c + 409 * e + 128) >> 8);
    rgb[1] = clamp_byte((298 * c - 100 * d - 208 * e + 128) >> 8);
    rgb[2] = clamp_byte((298 * c + 516 * d + 128) >> 8);
}

static void extract_block(
    const uint8_t *frame,
    const Y4mInfo *info,
    int block_index,
    uint8_t pixels[PIXELS_PER_BLOCK][3]
) {
    int blocks_x = (info->width + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int block_x = block_index % blocks_x;
    int block_y = block_index / blocks_x;
    const uint8_t *y_plane = frame;
    const uint8_t *u_plane = frame + info->luma_bytes;
    const uint8_t *v_plane = u_plane + info->chroma_bytes;
    int target = 0;
    int local_y, local_x;
    for (local_y = 0; local_y < BLOCK_SIZE; ++local_y) {
        int y = block_y * BLOCK_SIZE + local_y;
        if (y >= info->height) y = info->height - 1;
        for (local_x = 0; local_x < BLOCK_SIZE; ++local_x) {
            int x = block_x * BLOCK_SIZE + local_x;
            size_t chroma;
            if (x >= info->width) x = info->width - 1;
            chroma =
                (size_t)(y / 2) * (size_t)info->chroma_width + (size_t)(x / 2);
            yuv_to_rgb(
                y_plane[(size_t)y * (size_t)info->width + (size_t)x],
                u_plane[chroma],
                v_plane[chroma],
                pixels[target++]
            );
        }
    }
}

static void describe_pixels(
    const uint8_t pixels[PIXELS_PER_BLOCK][3],
    float descriptor[6]
) {
    double mean[3] = {0, 0, 0};
    double variance[3] = {0, 0, 0};
    int pixel, channel;
    for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
        for (channel = 0; channel < 3; ++channel)
            mean[channel] += pixels[pixel][channel];
    }
    for (channel = 0; channel < 3; ++channel)
        mean[channel] /= PIXELS_PER_BLOCK;
    for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
        for (channel = 0; channel < 3; ++channel) {
            double difference = pixels[pixel][channel] - mean[channel];
            variance[channel] += difference * difference;
        }
    }
    for (channel = 0; channel < 3; ++channel) {
        descriptor[channel] = (float)mean[channel];
        descriptor[channel + 3] =
            (float)sqrt(variance[channel] / PIXELS_PER_BLOCK);
    }
}

static int load_active_palette(
    const Options *options,
    size_t gop_index,
    Training *training
) {
    FILE *file;
    size_t offset;
    int palette;
    if (!options->active_palette_path || !training)
        return -1;
    if (gop_index > SIZE_MAX / sizeof training->palette) return -1;
    offset = gop_index * sizeof training->palette;
    if (offset > LONG_MAX) return -1;
    file = fopen(options->active_palette_path, "rb");
    if (!file) return -1;
    if (fseek(file, (long)offset, SEEK_SET) ||
        fread(training->palette, 1, sizeof training->palette, file) !=
            sizeof training->palette) {
        fclose(file);
        return -1;
    }
    if (fclose(file)) return -1;
    for (palette = 0; palette < PALETTE_COUNT; ++palette)
        describe_pixels(
            training->palette[palette],
            training->block_centers[palette]);
    return 0;
}

static uint64_t random_next(uint64_t *state) {
    uint64_t value = *state;
    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    *state = value;
    return value * UINT64_C(2685821657736338717);
}

static void reservoir_add(
    SampleReservoir *reservoir,
    const uint8_t pixels[PIXELS_PER_BLOCK][3]
) {
    size_t destination;
    reservoir->seen++;
    if (reservoir->count < reservoir->capacity) {
        destination = reservoir->count++;
    } else {
        uint64_t selected = random_next(&reservoir->random_state) %
            reservoir->seen;
        if (selected >= reservoir->capacity) return;
        destination = (size_t)selected;
    }
    memcpy(reservoir->blocks[destination].pixels, pixels,
        sizeof reservoir->blocks[destination].pixels);
    describe_pixels(pixels, reservoir->blocks[destination].descriptor);
}

static float descriptor_distance(const float *left, const float *right) {
    float value = 0.0f;
    int index;
    for (index = 0; index < 6; ++index) {
        float difference = left[index] - right[index];
        value += difference * difference;
    }
    return value;
}

static int nearest_descriptor(
    const float descriptor[6],
    const float centers[PALETTE_COUNT][6]
) {
    int best = 0;
    float best_distance = descriptor_distance(descriptor, centers[0]);
    int center;
    for (center = 1; center < PALETTE_COUNT; ++center) {
        float distance = descriptor_distance(descriptor, centers[center]);
        if (distance < best_distance) {
            best = center;
            best_distance = distance;
        }
    }
    return best;
}

static int train_block_centers(
    const SampleReservoir *reservoir,
    int iterations,
    Training *training,
    uint8_t *labels
) {
    float mean[6] = {0, 0, 0, 0, 0, 0};
    float *nearest = NULL;
    size_t sample;
    int dimension, center, iteration;
    if (reservoir->count < PALETTE_COUNT) return -1;
    nearest = (float *)malloc(reservoir->count * sizeof *nearest);
    if (!nearest) return -1;
    for (sample = 0; sample < reservoir->count; ++sample) {
        for (dimension = 0; dimension < 6; ++dimension)
            mean[dimension] += reservoir->blocks[sample].descriptor[dimension];
    }
    for (dimension = 0; dimension < 6; ++dimension)
        mean[dimension] /= (float)reservoir->count;
    {
        size_t first = 0;
        float farthest = -1.0f;
        for (sample = 0; sample < reservoir->count; ++sample) {
            float distance =
                descriptor_distance(reservoir->blocks[sample].descriptor, mean);
            if (distance > farthest) {
                farthest = distance;
                first = sample;
            }
        }
        memcpy(training->block_centers[0],
            reservoir->blocks[first].descriptor,
            sizeof training->block_centers[0]);
    }
    for (sample = 0; sample < reservoir->count; ++sample)
        nearest[sample] = INFINITY;
    for (center = 1; center < PALETTE_COUNT; ++center) {
        size_t next = 0;
        float farthest = -1.0f;
        for (sample = 0; sample < reservoir->count; ++sample) {
            float distance = descriptor_distance(
                reservoir->blocks[sample].descriptor,
                training->block_centers[center - 1]);
            if (distance < nearest[sample]) nearest[sample] = distance;
            if (nearest[sample] > farthest) {
                farthest = nearest[sample];
                next = sample;
            }
        }
        memcpy(training->block_centers[center],
            reservoir->blocks[next].descriptor,
            sizeof training->block_centers[center]);
    }
    memset(labels, 0, reservoir->count);
    for (iteration = 0; iteration < iterations; ++iteration) {
        double sums[PALETTE_COUNT][6] = {{0}};
        uint32_t counts[PALETTE_COUNT] = {0};
        for (sample = 0; sample < reservoir->count; ++sample) {
            int label = nearest_descriptor(
                reservoir->blocks[sample].descriptor,
                training->block_centers);
            labels[sample] = (uint8_t)label;
            counts[label]++;
            for (dimension = 0; dimension < 6; ++dimension)
                sums[label][dimension] +=
                    reservoir->blocks[sample].descriptor[dimension];
        }
        for (center = 0; center < PALETTE_COUNT; ++center) {
            if (!counts[center]) continue;
            for (dimension = 0; dimension < 6; ++dimension)
                training->block_centers[center][dimension] =
                    (float)(sums[center][dimension] / counts[center]);
        }
    }
    free(nearest);
    return 0;
}

static uint32_t color_distance(const uint8_t *left, const uint8_t *right) {
    int red = (int)left[0] - right[0];
    int green = (int)left[1] - right[1];
    int blue = (int)left[2] - right[2];
    return (uint32_t)(red * red + green * green + blue * blue);
}

static int train_color_centers(
    const uint8_t *points,
    size_t point_count,
    int iterations,
    uint8_t output[COLORS_PER_PALETTE][3]
) {
    double mean[3] = {0, 0, 0};
    float centers[COLORS_PER_PALETTE][3];
    float *nearest = NULL;
    size_t point;
    int channel, center, iteration;
    if (!point_count) return -1;
    nearest = (float *)malloc(point_count * sizeof *nearest);
    if (!nearest) return -1;
    for (point = 0; point < point_count; ++point)
        for (channel = 0; channel < 3; ++channel)
            mean[channel] += points[point * 3 + channel];
    for (channel = 0; channel < 3; ++channel)
        mean[channel] /= point_count;
    {
        size_t first = 0;
        double farthest = -1.0;
        for (point = 0; point < point_count; ++point) {
            double distance = 0.0;
            for (channel = 0; channel < 3; ++channel) {
                double difference = points[point * 3 + channel] - mean[channel];
                distance += difference * difference;
            }
            if (distance > farthest) {
                farthest = distance;
                first = point;
            }
        }
        for (channel = 0; channel < 3; ++channel)
            centers[0][channel] = points[first * 3 + channel];
    }
    for (point = 0; point < point_count; ++point) nearest[point] = INFINITY;
    for (center = 1; center < COLORS_PER_PALETTE; ++center) {
        size_t next = 0;
        float farthest = -1.0f;
        for (point = 0; point < point_count; ++point) {
            float distance = 0.0f;
            for (channel = 0; channel < 3; ++channel) {
                float difference =
                    points[point * 3 + channel] - centers[center - 1][channel];
                distance += difference * difference;
            }
            if (distance < nearest[point]) nearest[point] = distance;
            if (nearest[point] > farthest) {
                farthest = nearest[point];
                next = point;
            }
        }
        for (channel = 0; channel < 3; ++channel)
            centers[center][channel] = points[next * 3 + channel];
    }
    for (iteration = 0; iteration < iterations; ++iteration) {
        double sums[COLORS_PER_PALETTE][3] = {{0}};
        uint32_t counts[COLORS_PER_PALETTE] = {0};
        for (point = 0; point < point_count; ++point) {
            int best = 0;
            float best_distance = INFINITY;
            for (center = 0; center < COLORS_PER_PALETTE; ++center) {
                float distance = 0.0f;
                for (channel = 0; channel < 3; ++channel) {
                    float difference =
                        points[point * 3 + channel] - centers[center][channel];
                    distance += difference * difference;
                }
                if (distance < best_distance) {
                    best = center;
                    best_distance = distance;
                }
            }
            counts[best]++;
            for (channel = 0; channel < 3; ++channel)
                sums[best][channel] += points[point * 3 + channel];
        }
        for (center = 0; center < COLORS_PER_PALETTE; ++center) {
            if (!counts[center]) continue;
            for (channel = 0; channel < 3; ++channel)
                centers[center][channel] =
                    (float)(sums[center][channel] / counts[center]);
        }
    }
    for (center = 0; center < COLORS_PER_PALETTE; ++center)
        for (channel = 0; channel < 3; ++channel)
            output[center][channel] =
                clamp_byte((int)floor(centers[center][channel] + 0.5f));
    free(nearest);
    return 0;
}

static int compare_palette_color(const void *left, const void *right) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    int luma_a = 77 * a[0] + 150 * a[1] + 29 * a[2];
    int luma_b = 77 * b[0] + 150 * b[1] + 29 * b[2];
    if (luma_a != luma_b) return luma_a < luma_b ? -1 : 1;
    return memcmp(a, b, 3);
}

static int train_palettes(
    const SampleReservoir *reservoir,
    const uint8_t *labels,
    const Options *options,
    Training *training
) {
    uint8_t *points = NULL;
    int cluster;
    points = (uint8_t *)malloc(options->maximum_colors_per_cluster * 3);
    if (!points) return -1;
    for (cluster = 0; cluster < PALETTE_COUNT; ++cluster) {
        size_t total = 0, take, ordinal = 0, selected = 0;
        size_t sample;
        int fallback_global = 0;
        for (sample = 0; sample < reservoir->count; ++sample)
            if (labels[sample] == cluster) total += PIXELS_PER_BLOCK;
        if (!total) {
            total = reservoir->count * PIXELS_PER_BLOCK;
            fallback_global = 1;
        }
        take = total < options->maximum_colors_per_cluster
            ? total : options->maximum_colors_per_cluster;
        for (sample = 0; sample < reservoir->count && selected < take; ++sample) {
            int pixel;
            if (!fallback_global && labels[sample] != cluster) continue;
            for (pixel = 0; pixel < PIXELS_PER_BLOCK && selected < take; ++pixel) {
                size_t target = selected * total / take;
                if (ordinal == target) {
                    memcpy(points + selected * 3,
                        reservoir->blocks[sample].pixels[pixel], 3);
                    selected++;
                }
                ordinal++;
            }
        }
        if (!selected ||
            train_color_centers(points, selected, options->color_iterations,
                training->palette[cluster])) {
            free(points);
            return -1;
        }
        qsort(training->palette[cluster], COLORS_PER_PALETTE, 3,
            compare_palette_color);
    }
    free(points);
    return 0;
}

static int gop_plan_add(
    GopPlan *plan,
    uint32_t first_frame,
    int scene_cut,
    double scene_score
) {
    GopEntry *resized;
    size_t capacity;
    if (!plan || (plan->count &&
        first_frame <= plan->entries[plan->count - 1].first_frame)) {
        return -1;
    }
    if (plan->count == plan->capacity) {
        capacity = plan->capacity ? plan->capacity * 2U : 64U;
        if (capacity < plan->capacity ||
            capacity > SIZE_MAX / sizeof *plan->entries) {
            return -1;
        }
        resized = (GopEntry *)realloc(
            plan->entries, capacity * sizeof *plan->entries);
        if (!resized) return -1;
        plan->entries = resized;
        plan->capacity = capacity;
    }
    plan->entries[plan->count].first_frame = first_frame;
    plan->entries[plan->count].scene_cut = scene_cut;
    plan->entries[plan->count].scene_score = scene_score;
    plan->count++;
    return 0;
}

static double scene_change_score(
    const uint8_t *previous,
    const uint8_t *current,
    const Y4mInfo *info,
    double *previous_mafd
) {
    uint64_t absolute_difference = 0;
    size_t pixel;
    double mafd;
    double delta;
    if (!previous || !current || !info || !info->luma_bytes ||
        !previous_mafd) {
        return 0.0;
    }
    for (pixel = 0; pixel < info->luma_bytes; ++pixel) {
        int difference = (int)current[pixel] - previous[pixel];
        absolute_difference +=
            (uint64_t)(difference < 0 ? -difference : difference);
    }
    mafd = absolute_difference / (double)info->luma_bytes;
    delta = fabs(mafd - *previous_mafd);
    *previous_mafd = mafd;
    return fmin(mafd, delta) / 100.0;
}

static int scan_and_train(
    FILE *input,
    const Options *options,
    Y4mInfo *info,
    Training *training,
    uint32_t *frame_count,
    GopPlan *plan
) {
    SampleReservoir reservoir;
    uint8_t *frame = NULL;
    uint8_t *previous_frame = NULL;
    uint8_t *labels = NULL;
    uint32_t frames = 0;
    double previous_mafd = 0.0;
    int blocks_x, blocks_y, block_count;
    int result = 0;
    memset(&reservoir, 0, sizeof reservoir);
    if (!plan || parse_y4m_header(input, info)) goto fail;
    frame = (uint8_t *)malloc(info->frame_bytes);
    if (!frame) goto fail;
    if (options->scene_threshold > 0.0) {
        previous_frame = (uint8_t *)malloc(info->frame_bytes);
        if (!previous_frame) goto fail;
    }
    blocks_x = (info->width + BLOCK_SIZE - 1) / BLOCK_SIZE;
    blocks_y = (info->height + BLOCK_SIZE - 1) / BLOCK_SIZE;
    block_count = blocks_x * blocks_y;
    if (training) {
        reservoir.capacity = options->maximum_sample_blocks;
        reservoir.random_state = UINT64_C(0x9e3779b97f4a7c15);
        reservoir.blocks = (SampleBlock *)malloc(
            reservoir.capacity * sizeof *reservoir.blocks);
        if (!reservoir.blocks) goto fail;
    }
    while ((!options->max_frames || frames < options->max_frames) &&
           (result = read_y4m_frame(input, info, frame)) > 0) {
        if (!frames) {
            if (gop_plan_add(plan, 0, 0, 0.0)) goto fail;
        } else {
            const uint32_t first =
                plan->entries[plan->count - 1].first_frame;
            const uint32_t length = frames - first;
            double score = 0.0;
            int scene_cut = 0;
            if (previous_frame) {
                score = scene_change_score(
                    previous_frame, frame, info, &previous_mafd);
                scene_cut =
                    length >= (uint32_t)options->minimum_gop &&
                    score >= options->scene_threshold;
            }
            if (scene_cut || length >= (uint32_t)options->gop) {
                if (gop_plan_add(plan, frames, scene_cut, score)) goto fail;
            }
        }
        if (training) {
            int sample_count = options->sample_blocks_per_frame < block_count
                ? options->sample_blocks_per_frame : block_count;
            int sample;
            for (sample = 0; sample < sample_count; ++sample) {
                int block_index =
                    ((sample * block_count) / sample_count +
                     (int)((uint64_t)frames * 977u %
                           (uint64_t)block_count)) % block_count;
                uint8_t pixels[PIXELS_PER_BLOCK][3];
                extract_block(frame, info, block_index, pixels);
                reservoir_add(&reservoir, pixels);
            }
        }
        if (previous_frame)
            memcpy(previous_frame, frame, info->frame_bytes);
        frames++;
        if (options->progress && !(frames % 1000))
            fprintf(stderr, "BPV1 %s scan: %u frames\n",
                training ? "training" : "frame-count", frames);
    }
    if (result < 0 || !frames ||
        (training && reservoir.count < PALETTE_COUNT)) goto fail;
    if (training) {
        labels = (uint8_t *)malloc(reservoir.count);
        if (!labels) goto fail;
        if (train_block_centers(&reservoir, options->block_iterations,
                training, labels) ||
            train_palettes(&reservoir, labels, options, training)) goto fail;
        if (options->progress)
            fprintf(stderr,
                "BPV1 palette ready: %zu sampled blocks from %u frames\n",
                reservoir.count, frames);
    }
    *frame_count = frames;
    free(labels);
    free(frame);
    free(previous_frame);
    free(reservoir.blocks);
    return 0;
fail:
    free(labels);
    free(frame);
    free(previous_frame);
    free(reservoir.blocks);
    return -1;
}

static int train_gop_palette(
    const uint8_t *frames,
    int frame_count,
    int first_frame,
    const Y4mInfo *info,
    const Options *options,
    Training *training
) {
    SampleReservoir reservoir;
    uint8_t *labels = NULL;
    const int blocks_x = (info->width + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int blocks_y = (info->height + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int block_count = blocks_x * blocks_y;
    int samples_per_frame = options->sample_blocks_per_frame;
    size_t available;
    int frame_index;
    int result = -1;
    memset(&reservoir, 0, sizeof reservoir);
    memset(training, 0, sizeof *training);
    if (!frames || frame_count <= 0 || block_count <= 0) return -1;
    if (samples_per_frame > block_count) samples_per_frame = block_count;
    if ((size_t)frame_count * (size_t)samples_per_frame < PALETTE_COUNT) {
        samples_per_frame =
            (PALETTE_COUNT + frame_count - 1) / frame_count;
        if (samples_per_frame > block_count) samples_per_frame = block_count;
    }
    if ((size_t)frame_count * (size_t)samples_per_frame < PALETTE_COUNT ||
        (size_t)frame_count > SIZE_MAX / (size_t)samples_per_frame) {
        return -1;
    }
    available = (size_t)frame_count * (size_t)samples_per_frame;
    reservoir.capacity = options->maximum_sample_blocks < available
        ? options->maximum_sample_blocks : available;
    if (reservoir.capacity < PALETTE_COUNT) return -1;
    reservoir.random_state =
        UINT64_C(0x9e3779b97f4a7c15) ^
        ((uint64_t)(uint32_t)first_frame * UINT64_C(0xd1b54a32d192ed03));
    reservoir.blocks = (SampleBlock *)malloc(
        reservoir.capacity * sizeof *reservoir.blocks);
    if (!reservoir.blocks) return -1;

    for (frame_index = 0; frame_index < frame_count; ++frame_index) {
        const uint8_t *frame =
            frames + (size_t)frame_index * info->frame_bytes;
        int sample;
        for (sample = 0; sample < samples_per_frame; ++sample) {
            const int global_frame = first_frame + frame_index;
            const int block_index =
                ((sample * block_count) / samples_per_frame +
                 (int)((uint64_t)(uint32_t)global_frame * 977U %
                       (uint64_t)block_count)) % block_count;
            uint8_t pixels[PIXELS_PER_BLOCK][3];
            extract_block(frame, info, block_index, pixels);
            reservoir_add(&reservoir, pixels);
        }
    }
    if (reservoir.count < PALETTE_COUNT) goto cleanup;
    labels = (uint8_t *)malloc(reservoir.count);
    if (!labels) goto cleanup;
    if (train_block_centers(&reservoir, options->block_iterations,
            training, labels) ||
        train_palettes(&reservoir, labels, options, training)) {
        goto cleanup;
    }
    result = 0;

cleanup:
    free(labels);
    free(reservoir.blocks);
    return result;
}

static int block_equal(const Block *left, const Block *right) {
    return !memcmp(left, right, sizeof *left);
}

static int block_dictionary_find(
    const BlockDictionary *dictionary,
    const Block *block
) {
    int index;
    for (index = dictionary->count - 1; index >= 0; --index)
        if (block_equal(&dictionary->items[index], block)) return index;
    return -1;
}

static void block_dictionary_add(
    BlockDictionary *dictionary,
    const Block *block
) {
    if (block_dictionary_find(dictionary, block) >= 0) return;
    if (dictionary->count == dictionary->capacity) {
        memmove(dictionary->items, dictionary->items + 1,
            (size_t)(dictionary->capacity - 1) * sizeof *dictionary->items);
        dictionary->count--;
    }
    dictionary->items[dictionary->count++] = *block;
}

static int block_is_direct(const Block *block) {
    return (block->palette_index & DIRECT_RECORD_FLAG) != 0;
}

static unsigned block_palette_index(const Block *block) {
    return block->palette_index & 0x3fU;
}

static uint8_t *block_direct_bytes(Block *block) {
    return block->local_colors;
}

static const uint8_t *block_direct_bytes_const(const Block *block) {
    return block->local_colors;
}

static unsigned block_direct_color(const Block *block, unsigned pixel) {
    const uint8_t value = block_direct_bytes_const(block)[pixel >> 1];
    return pixel & 1U ? value & 15U : value >> 4;
}

static void block_direct_color_store(Block *block, unsigned pixel,
                                     unsigned color) {
    uint8_t *value = block_direct_bytes(block) + (pixel >> 1);
    if (pixel & 1U)
        *value = (uint8_t)((*value & 0xf0U) | color);
    else
        *value = (uint8_t)((*value & 0x0fU) | (color << 4));
}

static unsigned popcount16(uint16_t value) {
    unsigned count = 0;
    while (value) {
        value &= (uint16_t)(value - 1U);
        ++count;
    }
    return count;
}

static uint16_t block_direct_used_mask(const Block *block) {
    uint16_t mask = 0;
    unsigned pixel;
    for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel)
        mask |= (uint16_t)(1U << block_direct_color(block, pixel));
    return mask;
}

static uint64_t block_error(
    const uint8_t pixels[PIXELS_PER_BLOCK][3],
    const Block *block,
    const Training *training
) {
    uint64_t error = 0;
    int pixel;
    const unsigned palette_index = block_palette_index(block);
    for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
        unsigned color_index;
        if (block_is_direct(block)) {
            color_index = block_direct_color(block, (unsigned)pixel);
        } else {
            int shift = 6 - ((pixel & 3) * 2);
            int local = (block->pattern[pixel >> 2] >> shift) & 3;
            color_index = block->local_colors[local];
        }
        const uint8_t *color =
            training->palette[palette_index][color_index];
        error += color_distance(pixels[pixel], color);
    }
    return error;
}

static uint64_t quantize_block(
    const uint8_t pixels[PIXELS_PER_BLOCK][3],
    int palette_index,
    const Training *training,
    int color_limit,
    Block *block
) {
    uint32_t distances[PIXELS_PER_BLOCK][COLORS_PER_PALETTE];
    uint32_t current[PIXELS_PER_BLOCK];
    int selected[COLORS_PER_PALETTE];
    int pixel, color, slot;
    uint64_t total_error = 0;
    if (color_limit != 4 && color_limit != 8 &&
        color_limit != COLORS_PER_PALETTE) {
        return UINT64_MAX;
    }
    for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
        current[pixel] = UINT32_MAX;
        for (color = 0; color < COLORS_PER_PALETTE; ++color)
            distances[pixel][color] = color_distance(
                pixels[pixel], training->palette[palette_index][color]);
    }
    for (slot = 0; slot < color_limit; ++slot) {
        int best_color = 0;
        uint64_t best_error = UINT64_MAX;
        for (color = 0; color < COLORS_PER_PALETTE; ++color) {
            uint64_t error = 0;
            int used = 0;
            int previous;
            for (previous = 0; previous < slot; ++previous)
                if (selected[previous] == color) used = 1;
            if (used) continue;
            for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
                uint32_t distance = distances[pixel][color];
                error += distance < current[pixel]
                    ? distance : current[pixel];
            }
            if (error < best_error) {
                best_error = error;
                best_color = color;
            }
        }
        selected[slot] = best_color;
        for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel)
            if (distances[pixel][best_color] < current[pixel])
                current[pixel] = distances[pixel][best_color];
    }
    for (slot = 0; slot < color_limit - 1; ++slot) {
        int other;
        for (other = slot + 1; other < color_limit; ++other) {
            if (selected[other] < selected[slot]) {
                int temporary = selected[slot];
                selected[slot] = selected[other];
                selected[other] = temporary;
            }
        }
    }
    memset(block, 0, sizeof *block);
    block->palette_index = (uint8_t)(
        palette_index | (color_limit > LOCAL_COLORS
                             ? DIRECT_RECORD_FLAG : 0));
    if (color_limit == LOCAL_COLORS) {
        for (slot = 0; slot < LOCAL_COLORS; ++slot)
            block->local_colors[slot] = (uint8_t)selected[slot];
    }
    for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
        int best_slot = 0;
        uint32_t best_distance = distances[pixel][selected[0]];
        for (slot = 1; slot < color_limit; ++slot) {
            uint32_t distance = distances[pixel][selected[slot]];
            if (distance < best_distance) {
                best_distance = distance;
                best_slot = slot;
            }
        }
        if (color_limit > LOCAL_COLORS) {
            block_direct_color_store(
                block, (unsigned)pixel, (unsigned)selected[best_slot]);
        } else {
            block->pattern[pixel >> 2] |=
                (uint8_t)(best_slot << (6 - ((pixel & 3) * 2)));
        }
        total_error += best_distance;
    }
    return total_error;
}

static unsigned canonicalize_block(Block *block) {
    uint8_t used[LOCAL_COLORS] = {0};
    uint8_t mapping[LOCAL_COLORS] = {0};
    uint8_t colors[LOCAL_COLORS] = {0};
    uint8_t pattern[PATTERN_BYTES] = {0};
    unsigned count = 0;
    int pixel;
    int slot;
    if (block_is_direct(block)) {
        const uint16_t used_mask = block_direct_used_mask(block);
        count = popcount16(used_mask);
        if (count <= LOCAL_COLORS) {
            Block compact;
            uint8_t direct_to_local[COLORS_PER_PALETTE] = {0};
            unsigned color;
            memset(&compact, 0, sizeof compact);
            compact.palette_index =
                (uint8_t)block_palette_index(block);
            count = 0;
            for (color = 0; color < COLORS_PER_PALETTE; ++color) {
                if (!(used_mask & (1U << color))) continue;
                direct_to_local[color] = (uint8_t)count;
                compact.local_colors[count++] = (uint8_t)color;
            }
            for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
                const unsigned local =
                    direct_to_local[
                        block_direct_color(block, (unsigned)pixel)];
                compact.pattern[pixel >> 2] |=
                    (uint8_t)(local <<
                        (6 - ((pixel & 3) * 2)));
            }
            *block = compact;
        }
        return count;
    }
    for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
        const int shift = 6 - ((pixel & 3) * 2);
        used[(block->pattern[pixel >> 2] >> shift) & 3U] = 1;
    }
    for (slot = 0; slot < LOCAL_COLORS; ++slot) {
        if (!used[slot]) continue;
        mapping[slot] = (uint8_t)count;
        colors[count++] = block->local_colors[slot];
    }
    for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
        const int shift = 6 - ((pixel & 3) * 2);
        const unsigned source =
            (block->pattern[pixel >> 2] >> shift) & 3U;
        pattern[pixel >> 2] |=
            (uint8_t)(mapping[source] << shift);
    }
    memcpy(block->local_colors, colors, sizeof colors);
    memcpy(block->pattern, pattern, sizeof pattern);
    return count;
}

static unsigned block_color_count(const Block *block) {
    if (block_is_direct(block))
        return popcount16(block_direct_used_mask(block));
    unsigned count = 1;
    int row;
    for (row = 0; row < PATTERN_BYTES; ++row) {
        unsigned shift;
        for (shift = 0; shift < 8; shift += 2) {
            const unsigned local = (block->pattern[row] >> shift) & 3U;
            if (local + 1U > count) count = local + 1U;
        }
    }
    return count;
}

static int raw_payload_bits(const Block *block) {
    if (block_is_direct(block)) return 72;
    const unsigned count = block_color_count(block);
    return count == 1 ? 16 : count == 2 ? 32 : 56;
}

static int buffer_packed_prefix(Buffer *output, const Block *block,
                                unsigned count) {
    const unsigned subtype = count == 1U ? 0U : count == 2U ? 1U : 2U;
    const uint8_t tag = (uint8_t)(
        (subtype << 6) | block->palette_index);
    const uint8_t first = (uint8_t)(
        (block->local_colors[0] << 4) |
        (count > 1 ? block->local_colors[1] : 0));
    if (buffer_u8(output, tag) || buffer_u8(output, first))
        return -1;
    if (count > 2) {
        const uint8_t second = (uint8_t)(
            (block->local_colors[2] << 4) |
            (count > 3 ? block->local_colors[3] : 0));
        if (buffer_u8(output, second)) return -1;
    }
    return 0;
}

static int buffer_v6_raw(Buffer *output, const Block *block) {
    const unsigned count = block_color_count(block);
    if (buffer_packed_prefix(output, block, count)) return -1;
    if (count == 1) return 0;
    if (count == 2) {
        uint8_t bitmap[2] = {0};
        int pixel;
        for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
            const int pattern_shift = 6 - ((pixel & 3) * 2);
            const unsigned local =
                (block->pattern[pixel >> 2] >> pattern_shift) & 3U;
            bitmap[pixel >> 3] |=
                (uint8_t)(local << (7 - (pixel & 7)));
        }
        return buffer_write(output, bitmap, sizeof bitmap);
    }
    return buffer_write(output, block->pattern, PATTERN_BYTES);
}

static int buffer_raw_direct(Buffer *output, const Block *block) {
    const unsigned count = block_color_count(block);
    if (!block_is_direct(block) || count < 5U || count > 16U)
        return -1;
    return buffer_u8(output,
                     (uint8_t)(0xc0U | block_palette_index(block))) ||
           buffer_write(output, block_direct_bytes_const(block), 8);
}

static void nearest_palettes(
    const float descriptor[6],
    const Training *training,
    int count,
    int *indices
) {
    float distances[MAX_CANDIDATE_PALETTES];
    int center, slot;
    for (slot = 0; slot < count; ++slot) {
        distances[slot] = INFINITY;
        indices[slot] = 0;
    }
    for (center = 0; center < PALETTE_COUNT; ++center) {
        float distance = descriptor_distance(
            descriptor, training->block_centers[center]);
        for (slot = 0; slot < count; ++slot) {
            if (distance < distances[slot] ||
                (distance == distances[slot] && center < indices[slot])) {
                int move;
                for (move = count - 1; move > slot; --move) {
                    distances[move] = distances[move - 1];
                    indices[move] = indices[move - 1];
                }
                distances[slot] = distance;
                indices[slot] = center;
                break;
            }
        }
    }
}

static int better_candidate(
    double score,
    int bits,
    uint64_t error,
    const Block *block,
    double best_score,
    int best_bits,
    uint64_t best_error,
    const Block *best_block
) {
    if (score != best_score) return score < best_score;
    if (bits != best_bits) return bits < best_bits;
    if (error != best_error) return error < best_error;
    return memcmp(block, best_block, sizeof *block) < 0;
}

static int find_motion(
    const Block *block,
    const Block *previous,
    int block_index,
    int blocks_x,
    int blocks_y,
    int radius,
    int *motion_x,
    int *motion_y
) {
    int block_x = block_index % blocks_x;
    int block_y = block_index / blocks_x;
    int distance, dy, dx;
    for (distance = 1; distance <= radius; ++distance) {
        for (dy = -distance; dy <= distance; ++dy) {
            for (dx = -distance; dx <= distance; ++dx) {
                int source_x, source_y;
                int ring = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
                if (ring != distance) continue;
                source_x = block_x + dx;
                source_y = block_y + dy;
                if (source_x < 0 || source_y < 0 ||
                    source_x >= blocks_x || source_y >= blocks_y) continue;
                if (block_equal(block,
                        &previous[source_y * blocks_x + source_x])) {
                    *motion_x = dx;
                    *motion_y = dy;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void pack_modes(const uint8_t *modes, int count, uint8_t *output) {
    int index;
    memset(output, 0, ((size_t)count * 2 + 7) / 8);
    for (index = 0; index < count; ++index) {
        int bit;
        for (bit = 0; bit < 2; ++bit) {
            int value = (modes[index] >> (1 - bit)) & 1;
            int target = index * 2 + bit;
            output[target >> 3] |= (uint8_t)(value << (7 - (target & 7)));
        }
    }
}

static int encode_gop(GopJob *job) {
    const Options *options = job->options;
    const Y4mInfo *info = job->info;
    Training active_training;
    const Training *training = job->training;
    int blocks_x = (info->width + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int blocks_y = (info->height + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int block_count = blocks_x * blocks_y;
    size_t mode_bytes = ((size_t)block_count * 2 + 7) / 8;
    Block *previous = NULL;
    Block *current = NULL;
    uint8_t *modes = NULL;
    uint8_t *packed_modes = NULL;
    BlockDictionary block_dictionary = {0}, shadow_blocks = {0};
    int frame_index;
    if (options->active_palettes) {
        if (options->active_palette_path
                ? load_active_palette(
                    options, job->gop_index, &active_training)
                : train_gop_palette(job->frames, job->frame_count,
                    job->first_frame, info, options, &active_training)) {
            goto fail;
        }
        training = &active_training;
    }
    if (!training) goto fail;
    previous = (Block *)calloc((size_t)block_count, sizeof *previous);
    current = (Block *)calloc((size_t)block_count, sizeof *current);
    modes = (uint8_t *)malloc((size_t)block_count);
    packed_modes = (uint8_t *)malloc(mode_bytes);
    block_dictionary.capacity = options->max_block_dictionary;
    shadow_blocks.capacity = options->max_block_dictionary;
    block_dictionary.items = (Block *)malloc(
        (size_t)block_dictionary.capacity * sizeof *block_dictionary.items);
    shadow_blocks.items = (Block *)malloc(
        (size_t)shadow_blocks.capacity * sizeof *shadow_blocks.items);
    if (!previous || !current || !modes || !packed_modes ||
        !block_dictionary.items || !shadow_blocks.items) goto fail;

    for (frame_index = 0; frame_index < job->frame_count; ++frame_index) {
        const uint8_t *frame =
            job->frames + (size_t)frame_index * info->frame_bytes;
        Buffer payload = {0};
        int block_index;
        for (block_index = 0; block_index < block_count; ++block_index) {
            uint8_t pixels[PIXELS_PER_BLOCK][3];
            float descriptor[6];
            int palette_indices[MAX_CANDIDATE_PALETTES];
            Block best_block, candidate;
            uint64_t best_error = UINT64_MAX;
            double best_score = HUGE_VAL;
            int best_bits = 0;
            int best_source_previous = 0;
            int palette_slot;
            extract_block(frame, info, block_index, pixels);
            describe_pixels(pixels, descriptor);
            memset(&best_block, 0, sizeof best_block);
            if (frame_index > 0) {
                best_block = previous[block_index];
                best_error = block_error(
                    pixels, &best_block, training);
                best_score = (double)best_error;
                best_bits = 0;
                best_source_previous = 1;
            }
            nearest_palettes(descriptor, training,
                options->candidate_palettes, palette_indices);
            for (palette_slot = 0;
                 palette_slot < options->candidate_palettes;
                 ++palette_slot) {
                static const int color_limits[3] = {
                    4, 8, COLORS_PER_PALETTE
                };
                int color_class;
                for (color_class = 0; color_class < 3; ++color_class) {
                    uint64_t error = quantize_block(
                        pixels, palette_indices[palette_slot],
                        training, color_limits[color_class],
                        &candidate);
                    int bits;
                    double score;
                    canonicalize_block(&candidate);
                    bits = raw_payload_bits(&candidate);
                    if (frame_index > 0 &&
                        block_equal(
                            &candidate, &previous[block_index])) {
                        bits = 0;
                    } else if (block_dictionary_find(
                                   &shadow_blocks, &candidate) >= 0) {
                        bits = 16;
                    }
                    score = (double)error + options->lambda * bits;
                    if (best_score == HUGE_VAL ||
                        better_candidate(
                            score, bits, error, &candidate,
                            best_score, best_bits, best_error,
                            &best_block)) {
                        best_block = candidate;
                        best_error = error;
                        best_score = score;
                        best_bits = bits;
                        best_source_previous = 0;
                    }
                }
            }
            current[block_index] = best_block;
            job->stats.squared_error += best_error;
            job->stats.samples += PIXELS_PER_BLOCK * 3;
            if (best_source_previous) job->stats.previous_decisions++;
            else job->stats.quantized_decisions++;

            if (!(frame_index > 0 &&
                  block_equal(&best_block, &previous[block_index])) &&
                block_dictionary_find(&shadow_blocks, &best_block) < 0) {
                block_dictionary_add(&shadow_blocks, &best_block);
            }

            if (frame_index > 0 &&
                block_equal(&best_block, &previous[block_index])) {
                modes[block_index] = MODE_SKIP;
            } else {
                int motion_x = 0, motion_y = 0;
                int dictionary_index;
                if (frame_index > 0 && options->search_radius > 0 &&
                    find_motion(&best_block, previous, block_index,
                        blocks_x, blocks_y, options->search_radius,
                        &motion_x, &motion_y)) {
                    modes[block_index] = MODE_MOTION;
                    if (buffer_u8(
                            &payload,
                            (uint8_t)(((motion_x & 15) << 4) |
                                      (motion_y & 15))))
                        goto frame_fail;
                } else if ((dictionary_index = block_dictionary_find(
                                &block_dictionary, &best_block)) >= 0) {
                    modes[block_index] = MODE_BLOCK_DICT;
                    if (buffer_u16(&payload, (uint16_t)dictionary_index))
                        goto frame_fail;
                } else {
                    modes[block_index] = MODE_RAW;
                    if (block_is_direct(&best_block)) {
                        const unsigned direct_colors =
                            block_color_count(&best_block);
                        if (buffer_raw_direct(
                                &payload, &best_block)) {
                            goto frame_fail;
                        }
                        if (direct_colors <= 8U)
                            job->stats.direct_5_to_8++;
                        else
                            job->stats.direct_9_to_16++;
                    } else {
                        const unsigned local_colors =
                            block_color_count(&best_block);
                        if (buffer_v6_raw(
                                &payload, &best_block)) {
                            goto frame_fail;
                        }
                        if (local_colors == 1U)
                            job->stats.raw_1++;
                        else if (local_colors == 2U)
                            job->stats.raw_2++;
                        else
                            job->stats.raw_4++;
                    }
                    block_dictionary_add(
                        &block_dictionary, &best_block);
                }
            }
            job->stats.mode_counts[modes[block_index]]++;
        }
        pack_modes(modes, block_count, packed_modes);
        {
            const size_t palette_bytes =
                frame_index == 0 ? sizeof training->palette : 0U;
            if (palette_bytes > UINT32_MAX - mode_bytes ||
                payload.size > UINT32_MAX - mode_bytes - palette_bytes ||
                buffer_u8(&job->output, frame_index == 0 ? 1 : 0) ||
                buffer_u32(&job->output,
                    (uint32_t)(palette_bytes + mode_bytes + payload.size)) ||
                buffer_u32(&job->output, (uint32_t)mode_bytes) ||
                (palette_bytes &&
                 buffer_write(&job->output, training->palette,
                              palette_bytes)) ||
                buffer_write(&job->output, packed_modes, mode_bytes) ||
                buffer_write(&job->output, payload.data, payload.size)) {
                goto frame_fail;
            }
        }
        buffer_free(&payload);
        {
            Block *temporary = previous;
            previous = current;
            current = temporary;
        }
        continue;
frame_fail:
        buffer_free(&payload);
        goto fail;
    }
    free(previous);
    free(current);
    free(modes);
    free(packed_modes);
    free(block_dictionary.items);
    free(shadow_blocks.items);
    return 0;
fail:
    free(previous);
    free(current);
    free(modes);
    free(packed_modes);
    free(block_dictionary.items);
    free(shadow_blocks.items);
    buffer_free(&job->output);
    return -1;
}

static int gop_worker(void *opaque) {
    GopJob *job = (GopJob *)opaque;
    job->result = encode_gop(job);
    return job->result;
}

static int write_u8(FILE *file, uint8_t value) {
    return fwrite(&value, 1, 1, file) == 1 ? 0 : -1;
}

static int write_u16(FILE *file, uint16_t value) {
    uint8_t bytes[2] = {
        (uint8_t)(value & 255),
        (uint8_t)((value >> 8) & 255)
    };
    return fwrite(bytes, 1, sizeof bytes, file) == sizeof bytes ? 0 : -1;
}

static int write_u32(FILE *file, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)(value & 255),
        (uint8_t)((value >> 8) & 255),
        (uint8_t)((value >> 16) & 255),
        (uint8_t)((value >> 24) & 255)
    };
    return fwrite(bytes, 1, sizeof bytes, file) == sizeof bytes ? 0 : -1;
}

static int write_bpv_header(
    FILE *output,
    const Y4mInfo *info,
    const Options *options,
    const Training *training,
    uint32_t frame_count
) {
    const int has_audio = options->audio_path != NULL;
    if (!options->active_palettes && !training) return -1;
    if (fwrite("BPV1", 1, 4, output) != 4 ||
        write_u8(output, BPV_VERSION) ||
        write_u16(output, (uint16_t)info->width) ||
        write_u16(output, (uint16_t)info->height) ||
        write_u32(output, frame_count) ||
        write_u16(output, (uint16_t)info->fps_numerator) ||
        write_u16(output, (uint16_t)info->fps_denominator) ||
        write_u16(output, (uint16_t)options->gop) ||
        write_u16(output, (uint16_t)options->max_block_dictionary) ||
        write_u16(output, 0) ||
        write_u8(output, (uint8_t)options->search_radius) ||
        write_u8(output, has_audio ? 1 : 0) ||
        write_u16(output,
                  (uint16_t)(has_audio ? options->audio_rate : 0)) ||
        write_u8(output, has_audio ? 1 : 0) ||
        write_u8(output, 0)) return -1;
    return 0;
}

static int write_audio_frame(
    FILE *output,
    AudioInput *audio,
    const Y4mInfo *info,
    const uint8_t *encoded,
    size_t encoded_size,
    size_t *consumed,
    uint64_t *written_bytes
) {
    uint64_t step;
    uint64_t sample_count;
    uint8_t samples[4096];
    size_t offset = *consumed;
    size_t remaining;
    if (offset > encoded_size || encoded_size - offset < 9U) return -1;
    const uint32_t frame_bytes =
        (uint32_t)encoded[offset + 1U] |
        ((uint32_t)encoded[offset + 2U] << 8) |
        ((uint32_t)encoded[offset + 3U] << 16) |
        ((uint32_t)encoded[offset + 4U] << 24);
    if ((size_t)frame_bytes > encoded_size - offset - 9U) return -1;

    sample_count = 0;
    if (audio && audio->file) {
        step = (uint64_t)audio->sample_rate *
               (uint32_t)info->fps_denominator;
        if (audio->phase > UINT64_MAX - step) return -1;
        audio->phase += step;
        sample_count = audio->phase / (uint32_t)info->fps_numerator;
        audio->phase %= (uint32_t)info->fps_numerator;
    }
    if (sample_count > UINT32_MAX) return -1;

    if (fwrite(encoded + offset, 1, 9, output) != 9 ||
        write_u32(output, (uint32_t)sample_count) ||
        fwrite(encoded + offset + 9U, 1, frame_bytes, output) !=
            frame_bytes) {
        return -1;
    }
    remaining = (size_t)sample_count;
    while (remaining) {
        const size_t chunk =
            remaining < sizeof samples ? remaining : sizeof samples;
        const size_t got = fread(samples, 1, chunk, audio->file);
        if (got < chunk) {
            if (ferror(audio->file)) return -1;
            memset(samples + got, 128, chunk - got);
        }
        if (fwrite(samples, 1, chunk, output) != chunk) return -1;
        remaining -= chunk;
    }
    if (audio) audio->bytes += sample_count;
    *written_bytes += 13U + frame_bytes + sample_count;
    *consumed = offset + 9U + frame_bytes;
    return 0;
}

static void stats_add(EncodeStats *target, const EncodeStats *source) {
    int mode;
    for (mode = 0; mode < MODE_COUNT; ++mode)
        target->mode_counts[mode] += source->mode_counts[mode];
    target->raw_1 += source->raw_1;
    target->raw_2 += source->raw_2;
    target->raw_4 += source->raw_4;
    target->direct_5_to_8 += source->direct_5_to_8;
    target->direct_9_to_16 += source->direct_9_to_16;
    target->squared_error += source->squared_error;
    target->samples += source->samples;
    target->previous_decisions += source->previous_decisions;
    target->quantized_decisions += source->quantized_decisions;
}

static int encode_all_gops(
    FILE *input,
    FILE *output,
    const Y4mInfo *expected_info,
    const Options *options,
    const Training *training,
    uint32_t frame_count,
    const GopPlan *plan,
    EncodeStats *stats,
    uint64_t *payload_bytes,
    AudioInput *audio
) {
    Y4mInfo info;
    uint32_t next_frame = 0;
    size_t next_gop = 0;
    if (!plan || !plan->count ||
        fseek(input, 0, SEEK_SET) || parse_y4m_header(input, &info) ||
        memcmp(&info, expected_info, sizeof info)) return -1;
    while (next_gop < plan->count) {
        GopJob jobs[16];
        thrd_t workers[16];
        int worker_active[16] = {0};
        int job_count = 0;
        int job_index;
        memset(jobs, 0, sizeof jobs);
        while (job_count < options->threads && next_gop < plan->count) {
            const uint32_t first = plan->entries[next_gop].first_frame;
            const uint32_t end = next_gop + 1U < plan->count
                ? plan->entries[next_gop + 1U].first_frame
                : frame_count;
            int count;
            int frame;
            GopJob *job = &jobs[job_count];
            if (first != next_frame || end <= first ||
                end - first > (uint32_t)INT_MAX) {
                goto batch_fail;
            }
            count = (int)(end - first);
            job_count++;
            job->options = options;
            job->training = training;
            job->info = expected_info;
            job->frame_count = count;
            job->first_frame = (int)first;
            job->gop_index = next_gop;
            job->frames = (uint8_t *)malloc(
                (size_t)count * expected_info->frame_bytes);
            if (!job->frames) goto batch_fail;
            for (frame = 0; frame < count; ++frame) {
                int result = read_y4m_frame(input, expected_info,
                    job->frames + (size_t)frame * expected_info->frame_bytes);
                if (result != 1) goto batch_fail;
            }
            next_frame = end;
            next_gop++;
        }
        for (job_index = 0; job_index < job_count; ++job_index) {
            if (thrd_create(&workers[job_index], gop_worker,
                    &jobs[job_index]) == thrd_success) {
                worker_active[job_index] = 1;
            } else {
                gop_worker(&jobs[job_index]);
            }
        }
        for (job_index = 0; job_index < job_count; ++job_index) {
            int worker_result = 0;
            if (worker_active[job_index]) {
                thrd_join(workers[job_index], &worker_result);
                worker_active[job_index] = 0;
            }
            if (jobs[job_index].result || worker_result) goto batch_fail;
            {
                size_t consumed = 0;
                int frame;
                for (frame = 0; frame < jobs[job_index].frame_count;
                     ++frame) {
                    if (write_audio_frame(
                            output, audio, expected_info,
                            jobs[job_index].output.data,
                            jobs[job_index].output.size, &consumed,
                            payload_bytes)) {
                        goto batch_fail;
                    }
                }
                if (consumed != jobs[job_index].output.size)
                    goto batch_fail;
            }
            stats_add(stats, &jobs[job_index].stats);
            free(jobs[job_index].frames);
            jobs[job_index].frames = NULL;
            buffer_free(&jobs[job_index].output);
            if (options->progress)
                fprintf(stderr,
                    "BPV1 encoded: %u/%u frames, %.3f MiB payload\n",
                    (unsigned)(jobs[job_index].first_frame +
                        jobs[job_index].frame_count),
                    frame_count, *payload_bytes / 1048576.0);
        }
        continue;
batch_fail:
        for (job_index = 0; job_index < job_count; ++job_index) {
            if (worker_active[job_index]) {
                int ignored;
                thrd_join(workers[job_index], &ignored);
            }
            free(jobs[job_index].frames);
            buffer_free(&jobs[job_index].output);
        }
        return -1;
    }
    return 0;
}

static double wall_seconds(void) {
    struct timespec value;
    timespec_get(&value, TIME_UTC);
    return value.tv_sec + value.tv_nsec / 1000000000.0;
}

static int parse_int_option(
    const char *text,
    int minimum,
    int maximum,
    int *value
) {
    char *end = NULL;
    long parsed;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno || !end || *end || parsed < minimum || parsed > maximum)
        return -1;
    *value = (int)parsed;
    return 0;
}

static int parse_size_option(
    const char *text,
    size_t minimum,
    size_t maximum,
    size_t *value
) {
    char *end = NULL;
    unsigned long long parsed;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || !end || *end || parsed < minimum || parsed > maximum)
        return -1;
    *value = (size_t)parsed;
    return 0;
}

static int parse_double_option(
    const char *text,
    double minimum,
    double maximum,
    double *value
) {
    char *end = NULL;
    double parsed;
    errno = 0;
    parsed = strtod(text, &end);
    if (errno || !end || *end || !isfinite(parsed) ||
        parsed < minimum || parsed > maximum) return -1;
    *value = parsed;
    return 0;
}

static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static int write_json_string(FILE *stream, const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    if (fputc('"', stream) == EOF) return -1;
    while (*cursor) {
        unsigned char character = *cursor++;
        if (character == '"' || character == '\\') {
            if (fputc('\\', stream) == EOF ||
                fputc(character, stream) == EOF) return -1;
        } else if (character == '\b') {
            if (fputs("\\b", stream) == EOF) return -1;
        } else if (character == '\f') {
            if (fputs("\\f", stream) == EOF) return -1;
        } else if (character == '\n') {
            if (fputs("\\n", stream) == EOF) return -1;
        } else if (character == '\r') {
            if (fputs("\\r", stream) == EOF) return -1;
        } else if (character == '\t') {
            if (fputs("\\t", stream) == EOF) return -1;
        } else if (character < 0x20) {
            if (fprintf(stream, "\\u%04x", character) < 0) return -1;
        } else if (fputc(character, stream) == EOF) {
            return -1;
        }
    }
    return fputc('"', stream) == EOF ? -1 : 0;
}

static int write_report(
    FILE *stream,
    const char *input_path,
    const char *output_path,
    const Y4mInfo *info,
    const Options *options,
    const EncodeStats *stats,
    uint32_t frame_count,
    const GopPlan *plan,
    uint64_t file_bytes,
    double elapsed_seconds
) {
    const int version = BPV_VERSION;
    uint32_t scene_keyframes = 0;
    const uint32_t palette_updates =
        plan ? (uint32_t)plan->count : 0U;
    size_t gop_index;
    double duration =
        frame_count * (double)info->fps_denominator / info->fps_numerator;
    double mse = stats->samples
        ? stats->squared_error / (double)stats->samples : 0.0;
    double psnr = mse > 0.0 ? 10.0 * log10(255.0 * 255.0 / mse) : 0.0;
    if (!plan || !plan->count) return -1;
    for (gop_index = 0; gop_index < plan->count; ++gop_index)
        if (plan->entries[gop_index].scene_cut) scene_keyframes++;
    if (fprintf(stream,
        "{\n"
        "  \"codec\": \"BPV1\",\n"
        "  \"version\": %d,\n"
        "  \"encoder\": \"native C11\",\n"
        "  \"input\": ",
        version) < 0 ||
        write_json_string(stream, input_path) ||
        fputs(",\n  \"output\": ", stream) == EOF ||
        write_json_string(stream, output_path) ||
        fprintf(stream,
        ",\n"
        "  \"width\": %d,\n"
        "  \"height\": %d,\n"
        "  \"frames\": %u,\n"
        "  \"fpsNumerator\": %d,\n"
        "  \"fpsDenominator\": %d,\n"
        "  \"durationSeconds\": %.9f,\n"
        "  \"bytes\": %" PRIu64 ",\n"
        "  \"bitrateKbps\": %.6f,\n"
        "  \"bitsPerPixelPerFrame\": %.9f,\n"
        "  \"audioCodec\": \"%s\",\n"
        "  \"audioSampleRate\": %d,\n"
        "  \"audioChannels\": %d,\n"
        "  \"threads\": %d,\n"
        "  \"gop\": %d,\n"
        "  \"minimumGop\": %d,\n"
        "  \"sceneThreshold\": %.6f,\n"
        "  \"sceneKeyframes\": %u,\n"
        "  \"lambda\": %.6f,\n"
        "  \"candidatePaletteCount\": %d,\n"
        "  \"searchRadius\": %d,\n"
        "  \"paletteMode\": \"%s\",\n"
        "  \"paletteUpdates\": %u,\n"
        "  \"paletteTrainingColorSpace\": \"rgb\",\n"
        "  \"sampleBlocks\": %zu,\n"
        "  \"samplesPerFrame\": %d,\n"
        "  \"modeCounts\": {\n"
        "    \"skip\": %" PRIu64 ",\n"
        "    \"motion\": %" PRIu64 ",\n"
        "    \"blockDictionary\": %" PRIu64 ",\n"
        "    \"raw\": %" PRIu64 "\n"
        "  },\n"
        "  \"rawSubtypeCounts\": {\n"
        "    \"oneColor\": %" PRIu64 ",\n"
        "    \"twoColor\": %" PRIu64 ",\n"
        "    \"fourColor\": %" PRIu64 ",\n"
        "    \"direct5To8\": %" PRIu64 ",\n"
        "    \"direct9To16\": %" PRIu64 "\n"
        "  },\n"
        "  \"rgbMse\": %.9f,\n"
        "  \"rgbPsnrDb\": %.9f,\n"
        "  \"decisionCounts\": {\n"
        "    \"previous\": %" PRIu64 ",\n"
        "    \"quantized\": %" PRIu64 "\n"
        "  },\n"
        "  \"elapsedSeconds\": %.6f,\n"
        "  \"encodeFps\": %.6f,\n"
        "  \"keyframes\": [",
        info->width, info->height, frame_count,
        info->fps_numerator, info->fps_denominator, duration, file_bytes,
        file_bytes * 8.0 / duration / 1000.0,
        file_bytes * 8.0 /
            ((double)info->width * info->height * frame_count),
        options->audio_path ? "pcm_u8" : "none",
        options->audio_path ? options->audio_rate : 0,
        options->audio_path ? 1 : 0,
        options->threads, options->gop, options->minimum_gop,
        options->scene_threshold, scene_keyframes, options->lambda,
        options->candidate_palettes, options->search_radius,
        options->active_palette_path ? "active-override" :
            options->active_palettes ? "active-gop" : "fixed-global",
        palette_updates,
        options->maximum_sample_blocks,
        options->sample_blocks_per_frame,
        stats->mode_counts[MODE_SKIP],
        stats->mode_counts[MODE_MOTION],
        stats->mode_counts[MODE_BLOCK_DICT],
        stats->mode_counts[MODE_RAW],
        stats->raw_1,
        stats->raw_2,
        stats->raw_4,
        stats->direct_5_to_8,
        stats->direct_9_to_16,
        mse, psnr, stats->previous_decisions, stats->quantized_decisions,
        elapsed_seconds, frame_count / elapsed_seconds) < 0) return -1;
    for (gop_index = 0; gop_index < plan->count; ++gop_index) {
        const GopEntry *entry = &plan->entries[gop_index];
        const char *reason = !gop_index ? "initial" :
            entry->scene_cut ? "scene" : "maximumGop";
        if (fprintf(stream,
                "%s{\"frame\":%u,\"reason\":\"%s\","
                "\"sceneScore\":%.6f}",
                gop_index ? "," : "", entry->first_frame,
                reason, entry->scene_score) < 0) {
            return -1;
        }
    }
    return fputs("]\n}\n", stream) == EOF ? -1 : 0;
}

int main(int argc, char **argv) {
    Options options = {
        8, 48, 12, 0.35, 64.0, 3, 2, 256, 32768, 256,
        10, 10, 8192, 0, NULL, 0, 1, NULL, 16000, 1, NULL
    };
    const char *input_path = NULL;
    const char *output_path = NULL;
    char *partial_path = NULL;
    FILE *input = NULL;
    FILE *output = NULL;
    FILE *report = NULL;
    AudioInput audio = {0};
    Y4mInfo info;
    Training training;
    EncodeStats stats;
    GopPlan gop_plan = {0};
    uint32_t frame_count = 0;
    uint64_t payload_bytes = 0;
    uint64_t file_bytes;
    double started = wall_seconds();
    int positional = 0;
    int index;
    int exit_code = 1;
    memset(&training, 0, sizeof training);
    memset(&stats, 0, sizeof stats);

    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        const char *value = index + 1 < argc ? argv[index + 1] : NULL;
        if (!strcmp(argument, "-h") || !strcmp(argument, "--help")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(argument, "--threads") && value) {
            if (parse_int_option(value, 1, 16, &options.threads)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--gop") && value) {
            if (parse_int_option(value, 1, 65535, &options.gop)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--min-gop") && value) {
            if (parse_int_option(value, 1, 65535,
                    &options.minimum_gop)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--scene-threshold") && value) {
            if (parse_double_option(value, 0, 1,
                    &options.scene_threshold)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--no-scene-cuts")) {
            options.scene_threshold = 0.0;
        } else if (!strcmp(argument, "--lambda") && value) {
            if (parse_double_option(value, 0, 1e9, &options.lambda))
                goto bad_option;
            index++;
        } else if (!strcmp(argument, "--candidate-palettes") && value) {
            if (parse_int_option(value, 1, MAX_CANDIDATE_PALETTES,
                    &options.candidate_palettes)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--search-radius") && value) {
            if (parse_int_option(value, 0, 7, &options.search_radius))
                goto bad_option;
            index++;
        } else if (!strcmp(argument, "--sample-blocks") && value) {
            if (parse_size_option(value, PALETTE_COUNT, 262144,
                    &options.maximum_sample_blocks)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--samples-per-frame") && value) {
            if (parse_int_option(value, 1, 4096,
                    &options.sample_blocks_per_frame)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--block-iterations") && value) {
            if (parse_int_option(value, 1, 32,
                    &options.block_iterations)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--color-iterations") && value) {
            if (parse_int_option(value, 1, 32,
                    &options.color_iterations)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--colors-per-cluster") && value) {
            if (parse_size_option(value, COLORS_PER_PALETTE, 65536,
                    &options.maximum_colors_per_cluster)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--max-block-dictionary") && value) {
            if (parse_int_option(value, 1, 65535,
                    &options.max_block_dictionary)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--max-frames") && value) {
            int parsed;
            if (parse_int_option(value, 1, INT32_MAX, &parsed)) goto bad_option;
            options.max_frames = (uint32_t)parsed;
            index++;
        } else if (!strcmp(argument, "--audio-u8") && value) {
            options.audio_path = value;
            index++;
        } else if (!strcmp(argument, "--audio-rate") && value) {
            if (parse_int_option(value, 1000, 65535,
                    &options.audio_rate)) goto bad_option;
            index++;
        } else if (!strcmp(argument, "--active-palettes")) {
            options.active_palettes = 1;
        } else if (!strcmp(argument, "--active-palette-file") && value) {
            options.active_palette_path = value;
            options.active_palettes = 1;
            index++;
        } else if (!strcmp(argument, "--fixed-palettes")) {
            options.active_palettes = 0;
        } else if (!strcmp(argument, "--report") && value) {
            options.report_path = value;
            index++;
        } else if (!strcmp(argument, "--force")) {
            options.force = 1;
        } else if (!strcmp(argument, "--no-progress")) {
            options.progress = 0;
        } else if (argument[0] == '-') {
            goto bad_option;
        } else if (positional == 0) {
            input_path = argument;
            positional++;
        } else if (positional == 1) {
            output_path = argument;
            positional++;
        } else {
            goto bad_option;
        }
    }
    if (!input_path || !output_path) {
        usage(stderr);
        return 2;
    }
    if (options.active_palette_path && !options.active_palettes) {
        fprintf(stderr,
            "bpv1enc: --active-palette-file requires active palettes\n");
        return 2;
    }
    if (!options.force && file_exists(output_path)) {
        fprintf(stderr, "bpv1enc: output exists; use --force: %s\n",
            output_path);
        return 2;
    }
    partial_path = (char *)malloc(strlen(output_path) + 9);
    if (!partial_path) goto cleanup;
    sprintf(partial_path, "%s.partial", output_path);
    remove(partial_path);

    input = fopen(input_path, "rb");
    if (!input) {
        fprintf(stderr, "bpv1enc: cannot open %s\n", input_path);
        goto cleanup;
    }
    if (options.progress) {
        fprintf(stderr, "BPV1 C encoder: %s pass...\n",
            options.active_palettes ? "frame-count" : "palette-training");
    }
    if (scan_and_train(input, &options, &info,
            options.active_palettes ? NULL : &training, &frame_count,
            &gop_plan)) {
        fprintf(stderr, "bpv1enc: input scan failed\n");
        goto cleanup;
    }
    if (options.active_palette_path) {
        FILE *palette_file = fopen(options.active_palette_path, "rb");
        uint64_t expected =
            (uint64_t)gop_plan.count * sizeof training.palette;
        long actual;
        if (!palette_file || fseek(palette_file, 0, SEEK_END) ||
            (actual = ftell(palette_file)) < 0 ||
            (uint64_t)actual != expected) {
            if (palette_file) fclose(palette_file);
            fprintf(stderr,
                "bpv1enc: active palette file must contain exactly "
                "%" PRIu64 " bytes\n", expected);
            goto cleanup;
        }
        fclose(palette_file);
    }
    if (options.audio_path) {
        audio.file = fopen(options.audio_path, "rb");
        if (!audio.file) {
            fprintf(stderr, "bpv1enc: cannot open %s\n",
                    options.audio_path);
            goto cleanup;
        }
        audio.sample_rate = (uint32_t)options.audio_rate;
    }
    output = fopen(partial_path, "wb");
    if (!output || write_bpv_header(
            output, &info, &options, &training, frame_count)) {
        fprintf(stderr, "bpv1enc: cannot write output header\n");
        goto cleanup;
    }
    if (encode_all_gops(input, output, &info, &options, &training,
            frame_count, &gop_plan, &stats, &payload_bytes, &audio)) {
        fprintf(stderr, "bpv1enc: GOP encoding failed\n");
        goto cleanup;
    }
    if (fclose(output)) {
        output = NULL;
        goto cleanup;
    }
    output = NULL;
    fclose(input);
    input = NULL;
    if (options.force) remove(output_path);
    if (rename(partial_path, output_path)) {
        fprintf(stderr, "bpv1enc: cannot finalize %s\n", output_path);
        goto cleanup;
    }
    file_bytes = 29U + payload_bytes;
    if (options.report_path) {
        report = fopen(options.report_path, "wb");
        if (!report || write_report(report, input_path, output_path,
                &info, &options, &stats, frame_count, &gop_plan, file_bytes,
                wall_seconds() - started)) {
            fprintf(stderr, "bpv1enc: cannot write report\n");
            goto cleanup;
        }
        fclose(report);
        report = NULL;
    }
    write_report(stderr, input_path, output_path, &info, &options, &stats,
        frame_count, &gop_plan, file_bytes, wall_seconds() - started);
    if (audio.file)
        fprintf(stderr, "BPV1 audio: PCM_U8 mono %u Hz, %.3f MiB\n",
                audio.sample_rate, audio.bytes / 1048576.0);
    exit_code = 0;
    goto cleanup;

bad_option:
    fprintf(stderr, "bpv1enc: invalid option near '%s'\n", argv[index]);
    usage(stderr);
    return 2;

cleanup:
    if (report) fclose(report);
    if (output) fclose(output);
    if (input) fclose(input);
    if (audio.file) fclose(audio.file);
    if (exit_code && partial_path) remove(partial_path);
    free(gop_plan.entries);
    free(partial_path);
    return exit_code;
}
