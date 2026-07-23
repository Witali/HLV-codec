/*
 * hlvenc: command-line front end for the reference C encoder.
 *
 * Input is YUV4MPEG2, normally supplied by FFmpeg through stdin.  This tool
 * owns encoder-only policy: presets, adaptive quality, local windowed two-pass
 * control, reconstruction logging, and streaming header finalization.
 */
#include "hlv1.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/* --- Portable file/pipe handling -------------------------------------- */
static void binary_stdio(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

static FILE *open_input(const char *path) {
    if (!strcmp(path, "-")) return stdin;
    return fopen(path, "rb");
}

static FILE *open_output(const char *path, int *seekable) {
    if (!strcmp(path, "-")) {
        *seekable = 0;
        return stdout;
    }
    *seekable = 1;
    return fopen(path, "wb+");
}

static void close_if_file(FILE *f, FILE *standard) {
    if (f && f != standard) fclose(f);
}

typedef struct AudioInput {
    FILE *file;
    uint32_t sample_rate;
    uint64_t phase;
    uint64_t bytes;
} AudioInput;

/* Attach exactly one video-frame interval of unsigned 8-bit mono PCM.  The
 * rational accumulator alternates sample counts without long-term A/V drift. */
static int append_audio_interval(AudioInput *audio, int fps_num, int fps_den,
                                 HLV1Packet *packet) {
    if (!audio || !audio->file) return HLV1_OK;
    uint64_t step = (uint64_t)audio->sample_rate * (uint32_t)fps_den;
    if (audio->phase > UINT64_MAX - step) return HLV1_ERR_RANGE;
    audio->phase += step;
    uint64_t sample_count = audio->phase / (uint32_t)fps_num;
    audio->phase %= (uint32_t)fps_num;
    if (sample_count > SIZE_MAX) return HLV1_ERR_RANGE;
    size_t size = (size_t)sample_count;
    if (!size) return HLV1_OK;

    uint8_t *samples = (uint8_t *)malloc(size);
    if (!samples) return HLV1_ERR_MEMORY;
    size_t got = fread(samples, 1, size, audio->file);
    if (got < size) {
        if (ferror(audio->file)) {
            free(samples);
            return HLV1_ERR_IO;
        }
        memset(samples + got, 128, size - got);
    }
    int result = hlv1_packet_append_audio(packet, samples, size);
    free(samples);
    if (result >= 0) audio->bytes += size;
    return result;
}

static void usage(const char *p) {
    fprintf(stderr,
        "Usage: %s input.y4m|- output.hlv|- [options]\n"
        "Options:\n"
        "  --preset NAME     fast|balanced|slow (default balanced)\n"
        "  --quality N       friendly 1..100 quality scale (default 55)\n"
        "  --qstep-y N       exact luma quantizer step 1..2040 (v4)\n"
        "  --qstep-uv N      exact chroma quantizer step 1..2040 (v4)\n"
        "  --target-psnr X   fixed frame PSNR target in dB (encoder trials)\n"
        "  --adaptive-quality vary PSNR by motion/detail (default 30..35 dB)\n"
        "  --psnr-min X      fast detailed-scene target (default 30)\n"
        "  --psnr-max X      calm predictable-scene target (default 35)\n"
        "  --cq-trials N     trial encodes per frame 2..10 (default 5)\n"
        "  --cq-log FILE     write per-frame quality decisions as CSV\n"
        "  --bitrate N       target bitrate in kbit/s\n"
        "  --two-pass-window X local first-pass window in seconds (e.g. 10)\n"
        "  --two-pass-trials N whole-window q probes 2..10 (default 5)\n"
        "  --two-pass-log FILE write per-window analysis as CSV\n"
        "  --syntax N        stream syntax 1..13 (default latest)\n"
        "  --gop N           maximum keyframe interval (default by preset)\n"
        "  --adaptive-gop    compare K/P frames with encoder-only RDO\n"
        "  --min-key-interval N minimum frames between adaptive K frames (default 8)\n"
        "  --keyframe-bias X allow K when cost <= P*X, 1.0..1.25 (default 1.00)\n"
        "  --search N        motion search radius in pixels (default by preset)\n"
        "  --scene-cut N     mean luma threshold (default 38)\n"
        "  --chroma-scale X  chroma/luma qstep ratio (default 1.35)\n"
        "  --rd-luma-weight N distortion weight 1..16 (default 4)\n"
        "  --rd-lambda-scale X RDO lambda multiplier (default 1.0)\n"
        "  --decode-cycle-weight X estimated decoder cycles as equivalent bits (default 0)\n"
        "  --ac-deadzone X   AC zero threshold 0.5..2.0 (default 0.5)\n"
        "  --motion-candidates N fully RDO-test 1..8 motion candidates (default by preset)\n"
        "  --max-frames N    stop after N frames\n"
        "  --recon FILE.y4m  write encoder reconstruction\n"
        "  --audio-u8 FILE   mux unsigned 8-bit mono raw PCM\n"
        "  --audio-rate N    PCM sample rate in Hz (default 16000)\n"
        "\n"
        "Examples:\n"
        "  ffmpeg -i input.mp4 -vf scale=320:240,fps=15,format=yuv420p "
        "-f yuv4mpegpipe - | %s - output.hlv\n"
        "  %s input.y4m - | %s - output.y4m\n", p, p, p, "hlvdec");
}

static double now_sec(void) { return (double)clock() / CLOCKS_PER_SEC; }

static double clamp_double(double value, double low, double high) {
    return value < low ? low : value > high ? high : value;
}

static double smoothstep(double low, double high, double value) {
    double t = clamp_double((value - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/* --- Quality and scene-complexity analysis ----------------------------- */
static double frame_psnr_yuv420(const HLV1Frame *a, const HLV1Frame *b) {
    double sse = 0.0;
    uint64_t samples = 0;
    for (int y = 0; y < a->height; ++y) {
        const uint8_t *pa = a->y + y * a->stride_y;
        const uint8_t *pb = b->y + y * b->stride_y;
        for (int x = 0; x < a->width; ++x) {
            double d = (double)pa[x] - pb[x];
            sse += d * d;
        }
        samples += (uint64_t)a->width;
    }
    int cw = (a->width + 1) / 2;
    int ch = (a->height + 1) / 2;
    for (int plane = 0; plane < 2; ++plane) {
        const uint8_t *aa = plane ? a->v : a->u;
        const uint8_t *bb = plane ? b->v : b->u;
        int sa = plane ? a->stride_v : a->stride_u;
        int sb = plane ? b->stride_v : b->stride_u;
        for (int y = 0; y < ch; ++y) {
            const uint8_t *pa = aa + y * sa;
            const uint8_t *pb = bb + y * sb;
            for (int x = 0; x < cw; ++x) {
                double d = (double)pa[x] - pb[x];
                sse += d * d;
            }
        }
        samples += (uint64_t)cw * ch;
    }
    if (sse <= 0.0) return 99.0;
    return 10.0 * log10(255.0 * 255.0 * (double)samples / sse);
}

static double luma_detail_score(const HLV1Frame *frame) {
    uint64_t sum = 0, count = 0;
    for (int y = 1; y < frame->height; y += 2) {
        const uint8_t *row = frame->y + y * frame->stride_y;
        const uint8_t *top = row - frame->stride_y;
        for (int x = 1; x < frame->width; x += 2) {
            sum += (unsigned)abs((int)row[x] - row[x - 1]);
            sum += (unsigned)abs((int)row[x] - top[x]);
            count += 2;
        }
    }
    return count ? (double)sum / count : 0.0;
}

static double compensated_luma_motion(const HLV1Frame *previous,
                                      const HLV1Frame *current,
                                      int radius, double *raw_mad) {
    if (radius < 0) radius = 0;
    int max_radius = (current->width < current->height ?
                      current->width : current->height) / 8;
    if (radius > max_radius) radius = max_radius;
    int sample_step = 4;
    uint64_t raw_sum = 0, raw_count = 0;
    for (int y = radius; y < current->height - radius; y += sample_step) {
        const uint8_t *cur = current->y + y * current->stride_y;
        const uint8_t *prev = previous->y + y * previous->stride_y;
        for (int x = radius; x < current->width - radius; x += sample_step) {
            raw_sum += (unsigned)abs((int)cur[x] - prev[x]);
            ++raw_count;
        }
    }
    if (raw_mad) *raw_mad = raw_count ? (double)raw_sum / raw_count : 0.0;

    double best = HUGE_VAL;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            uint64_t sum = 0, count = 0;
            for (int y = radius; y < current->height - radius; y += sample_step) {
                const uint8_t *cur = current->y + y * current->stride_y;
                const uint8_t *prev = previous->y +
                    (y + dy) * previous->stride_y + dx;
                for (int x = radius; x < current->width - radius; x += sample_step) {
                    sum += (unsigned)abs((int)cur[x] - prev[x]);
                    ++count;
                }
            }
            double mad = count ? (double)sum / count : 0.0;
            if (mad < best) best = mad;
        }
    }
    return isfinite(best) ? best : 0.0;
}

static double adaptive_psnr_target(const HLV1Frame *previous,
                                   const HLV1Frame *current,
                                   int have_previous, double minimum,
                                   double maximum, double scene_cut,
                                   double *motion_out, double *detail_out,
                                   double *raw_motion_out) {
    double detail = luma_detail_score(current);
    double motion = 0.0, raw_motion = 0.0;
    if (have_previous)
        motion = compensated_luma_motion(previous, current, 4, &raw_motion);

    /* Static detail stays sharp.  Fine detail lowers the target primarily
       when it is also moving, where the eye is less sensitive and the cost
       of preserving every coefficient is especially high. */
    double motion_n = smoothstep(0.75, 12.0, motion);
    double detail_n = smoothstep(4.0, 28.0, detail);
    double stress = motion_n * (0.70 + 0.30 * detail_n);
    double target = maximum - (maximum - minimum) * stress;

    /* A hard cut becomes a new long-lived reference.  Do not encode it at
       the lowest transient-motion quality even though raw temporal MAD is
       high. */
    if (have_previous && raw_motion >= scene_cut) {
        double key_floor = minimum + 0.60 * (maximum - minimum);
        if (target < key_floor) target = key_floor;
    }
    if (motion_out) *motion_out = motion;
    if (detail_out) *detail_out = detail;
    if (raw_motion_out) *raw_motion_out = raw_motion;
    return clamp_double(target, minimum, maximum);
}

/* One isolated constant-quality trial.  The cloned encoder owns its
 * reconstructed reference until the trial is selected or discarded. */
typedef struct CQTrial {
    HLV1Encoder *encoder;
    HLV1Packet packet;
    const HLV1Frame *reconstructed;
    double psnr;
    int q_y;
    int q_uv;
} CQTrial;

static void cq_trial_free(CQTrial *trial) {
    if (!trial) return;
    hlv1_packet_free(&trial->packet);
    hlv1_encoder_destroy(trial->encoder);
    memset(trial, 0, sizeof *trial);
}

static int q_was_tested(const int *tested, int count, int q) {
    for (int i = 0; i < count; ++i)
        if (tested[i] == q) return 1;
    return 0;
}

/* Search for the largest quantizer that still reaches the requested PSNR.
 * Testing larger q first minimizes output size; fallback trials reduce RDO
 * lambda when SKIP makes the quality curve discontinuous. */
static int encode_constant_quality(HLV1Encoder *base,
                                   const HLV1Frame *input,
                                   double target_psnr, double chroma_scale,
                                   double base_lambda_scale,
                                   int luma_weight,
                                   int initial_q, int max_q, int trials,
                                   CQTrial *chosen) {
    int tested[16];
    int tested_count = 0;
    int q = initial_q;
    int good_q = 0, bad_q = 0;
    CQTrial best_good = {0}, best_bad = {0};
    q = q < 1 ? 1 : q > max_q ? max_q : q;

    for (int iteration = 0; iteration < trials; ++iteration) {
        if (q_was_tested(tested, tested_count, q)) {
            if (good_q && bad_q && bad_q - good_q > 1)
                q = good_q + (bad_q - good_q) / 2;
            else if (good_q && good_q < max_q)
                q = good_q + 1;
            else if (bad_q > 1)
                q = bad_q - 1;
            else
                break;
            if (q_was_tested(tested, tested_count, q)) break;
        }
        tested[tested_count++] = q;

        CQTrial trial = {0};
        trial.encoder = hlv1_encoder_clone(base);
        if (!trial.encoder) {
            cq_trial_free(&best_good);
            cq_trial_free(&best_bad);
            return HLV1_ERR_MEMORY;
        }
        trial.q_y = q;
        trial.q_uv = (int)llround(q * chroma_scale);
        if (trial.q_uv < 1) trial.q_uv = 1;
        if (trial.q_uv > HLV1_MAX_QSTEP) trial.q_uv = HLV1_MAX_QSTEP;
        int r = hlv1_encoder_set_quantization(trial.encoder,
                                              trial.q_y, trial.q_uv);
        if (r >= 0)
            r = hlv1_encoder_encode(trial.encoder, input, &trial.packet,
                                    &trial.reconstructed);
        if (r < 0) {
            cq_trial_free(&trial);
            cq_trial_free(&best_good);
            cq_trial_free(&best_bad);
            return r;
        }
        trial.psnr = frame_psnr_yuv420(input, trial.reconstructed);
        size_t trial_size = trial.packet.payload_size + HLV1_FRAME_HEADER_SIZE;

        if (trial.psnr + 0.01 >= target_psnr) {
            good_q = q > good_q ? q : good_q;
            size_t best_size = best_good.encoder ?
                best_good.packet.payload_size + HLV1_FRAME_HEADER_SIZE : SIZE_MAX;
            if (!best_good.encoder || trial_size < best_size ||
                (trial_size == best_size && trial.psnr < best_good.psnr)) {
                cq_trial_free(&best_good);
                best_good = trial;
                memset(&trial, 0, sizeof trial);
            }
        } else {
            bad_q = !bad_q || q < bad_q ? q : bad_q;
            if (!best_bad.encoder || trial.psnr > best_bad.psnr) {
                cq_trial_free(&best_bad);
                best_bad = trial;
                memset(&trial, 0, sizeof trial);
            }
        }
        double measured = trial.encoder ? trial.psnr :
                          (best_good.q_y == q ? best_good.psnr : best_bad.psnr);
        cq_trial_free(&trial);

        if (good_q && bad_q && bad_q - good_q > 1) {
            q = good_q + (bad_q - good_q) / 2;
        } else {
            double factor = pow(10.0, (measured - target_psnr) / 20.0);
            factor = clamp_double(factor, 0.25, 4.00);
            int next = (int)llround(q * factor);
            if (next == q) next += measured >= target_psnr ? 1 : -1;
            q = next < 1 ? 1 : next > max_q ? max_q : next;
        }
    }

    /* A predictive SKIP decision can make PSNR temporarily insensitive to
       qstep.  Before jumping to q=1, reduce only the encoder's RDO bit
       penalty at the best near-target qstep.  This often inserts a modest
       residual instead of switching from SKIP straight to a huge nearly
       lossless packet. */
    if (!best_good.encoder && best_bad.encoder) {
        const double lambda_factors[] = {0.25, 0.0625};
        for (unsigned i = 0;
             i < sizeof lambda_factors / sizeof lambda_factors[0] &&
             !best_good.encoder; ++i) {
            CQTrial trial = {0};
            trial.encoder = hlv1_encoder_clone(base);
            if (!trial.encoder) {
                cq_trial_free(&best_bad);
                return HLV1_ERR_MEMORY;
            }
            trial.q_y = best_bad.q_y;
            trial.q_uv = best_bad.q_uv;
            int r = hlv1_encoder_set_quantization(trial.encoder,
                                                  trial.q_y, trial.q_uv);
            if (r >= 0)
                r = hlv1_encoder_set_rd_parameters(
                    trial.encoder,
                    base_lambda_scale * lambda_factors[i], luma_weight);
            if (r >= 0)
                r = hlv1_encoder_encode(trial.encoder, input, &trial.packet,
                                        &trial.reconstructed);
            if (r < 0) {
                cq_trial_free(&trial);
                cq_trial_free(&best_bad);
                return r;
            }
            trial.psnr = frame_psnr_yuv420(input, trial.reconstructed);
            if (trial.psnr + 0.01 >= target_psnr) {
                /* Restore the normal RDO setting for subsequent frames; the
                   already reconstructed reference remains unchanged. */
                hlv1_encoder_set_rd_parameters(trial.encoder,
                                               base_lambda_scale,
                                               luma_weight);
                best_good = trial;
                memset(&trial, 0, sizeof trial);
            } else if (trial.psnr > best_bad.psnr) {
                cq_trial_free(&best_bad);
                best_bad = trial;
                memset(&trial, 0, sizeof trial);
            }
            cq_trial_free(&trial);
        }
    }

    /* A predictive SKIP decision can make PSNR temporarily insensitive to
       qstep.  If the normal search never found a candidate meeting the hard
       target, explicitly test the finest quantizer before accepting a miss. */
    if (!best_good.encoder && !q_was_tested(tested, tested_count, 1)) {
        CQTrial trial = {0};
        trial.encoder = hlv1_encoder_clone(base);
        if (!trial.encoder) {
            cq_trial_free(&best_bad);
            return HLV1_ERR_MEMORY;
        }
        trial.q_y = 1;
        trial.q_uv = (int)llround(chroma_scale);
        if (trial.q_uv < 1) trial.q_uv = 1;
        int r = hlv1_encoder_set_quantization(trial.encoder,
                                              trial.q_y, trial.q_uv);
        if (r >= 0)
            r = hlv1_encoder_encode(trial.encoder, input, &trial.packet,
                                    &trial.reconstructed);
        if (r < 0) {
            cq_trial_free(&trial);
            cq_trial_free(&best_bad);
            return r;
        }
        trial.psnr = frame_psnr_yuv420(input, trial.reconstructed);
        if (trial.psnr + 0.01 >= target_psnr) {
            hlv1_encoder_set_rd_parameters(trial.encoder,
                                           base_lambda_scale,
                                           luma_weight);
            best_good = trial;
            memset(&trial, 0, sizeof trial);
        } else if (!best_bad.encoder || trial.psnr > best_bad.psnr) {
            cq_trial_free(&best_bad);
            best_bad = trial;
            memset(&trial, 0, sizeof trial);
        }
        cq_trial_free(&trial);
    }

    if (best_good.encoder) {
        *chosen = best_good;
        memset(&best_good, 0, sizeof best_good);
    } else if (best_bad.encoder) {
        *chosen = best_bad;
        memset(&best_bad, 0, sizeof best_bad);
    } else {
        return HLV1_ERR_RANGE;
    }
    cq_trial_free(&best_good);
    cq_trial_free(&best_bad);
    return HLV1_OK;
}


/* --- Temporary raw-YUV storage for local two-pass windows -------------- */
static int write_raw_plane(FILE *file, const uint8_t *plane,
                           int stride, int width, int height) {
    for (int y = 0; y < height; ++y) {
        if (fwrite(plane + y * stride, 1, (size_t)width, file) != (size_t)width)
            return HLV1_ERR_IO;
    }
    return HLV1_OK;
}

static int read_raw_plane(FILE *file, uint8_t *plane,
                          int stride, int width, int height) {
    for (int y = 0; y < height; ++y) {
        if (fread(plane + y * stride, 1, (size_t)width, file) != (size_t)width)
            return feof(file) ? HLV1_EOF : HLV1_ERR_IO;
    }
    return HLV1_OK;
}

static int write_raw420_frame(FILE *file, const HLV1Frame *frame) {
    int cw = (frame->width + 1) / 2;
    int ch = (frame->height + 1) / 2;
    int r = write_raw_plane(file, frame->y, frame->stride_y,
                            frame->width, frame->height);
    if (r >= 0) r = write_raw_plane(file, frame->u, frame->stride_u, cw, ch);
    if (r >= 0) r = write_raw_plane(file, frame->v, frame->stride_v, cw, ch);
    return r;
}

static int read_raw420_frame(FILE *file, HLV1Frame *frame) {
    int cw = (frame->width + 1) / 2;
    int ch = (frame->height + 1) / 2;
    int r = read_raw_plane(file, frame->y, frame->stride_y,
                           frame->width, frame->height);
    if (r >= 0) r = read_raw_plane(file, frame->u, frame->stride_u, cw, ch);
    if (r >= 0) r = read_raw_plane(file, frame->v, frame->stride_v, cw, ch);
    return r;
}

static int clamp_qstep(int q, int maximum) {
    if (q < 1) return 1;
    if (q > maximum) return maximum;
    return q;
}

/* Encode a bounded raw-YUV fragment from a clone of the exact predictive
   state at the start of the fragment.  No packets are written. */
/* Encode one buffered window from a clone of the exact predictive state and
 * return only aggregate bytes.  The real encoder remains untouched during
 * first-pass quantizer probes. */
static int analyse_window(HLV1Encoder *base, FILE *spool,
                          HLV1Frame *scratch, int frame_count,
                          int q_y, double chroma_scale,
                          uint64_t *total_bytes) {
    HLV1Encoder *probe = hlv1_encoder_clone(base);
    if (!probe) return HLV1_ERR_MEMORY;
    int maximum_q = HLV1_MAX_QSTEP;
    int q_uv = clamp_qstep((int)llround(q_y * chroma_scale), maximum_q);
    int r = hlv1_encoder_set_quantization(probe, q_y, q_uv);
    if (r < 0) {
        hlv1_encoder_destroy(probe);
        return r;
    }
    rewind(spool);
    *total_bytes = 0;
    for (int i = 0; i < frame_count; ++i) {
        r = read_raw420_frame(spool, scratch);
        if (r < 0) break;
        HLV1Packet packet = {0};
        const HLV1Frame *reconstructed = NULL;
        r = hlv1_encoder_encode(probe, scratch, &packet, &reconstructed);
        (void)reconstructed;
        if (r < 0) {
            hlv1_packet_free(&packet);
            break;
        }
        *total_bytes += packet.payload_size + HLV1_FRAME_HEADER_SIZE;
        hlv1_packet_free(&packet);
    }
    hlv1_encoder_destroy(probe);
    rewind(spool);
    return r < 0 ? r : HLV1_OK;
}

/* --- CLI policy -------------------------------------------------------- */
static int apply_preset(const char *name, int *gop, int *search, int *motion_candidates) {
    if (!strcmp(name, "fast")) {
        *gop = 24; *search = 0; *motion_candidates = 1; return 0;
    }
    if (!strcmp(name, "balanced")) {
        *gop = 30; *search = 4; *motion_candidates = 4; return 0;
    }
    if (!strcmp(name, "slow")) {
        *gop = 45; *search = 8; *motion_candidates = 8; return 0;
    }
    return -1;
}

/* Parse options, initialize the common library, then run either frame-wise
 * encoding or the bounded-window two-pass pipeline. */
int main(int argc, char **argv) {
    binary_stdio();
    if (argc < 3) { usage(argv[0]); return 2; }

    int quality = 55, qstep_y = 0, qstep_uv = 0, bitrate_kbps = 0;
    double two_pass_window_seconds = 0.0;
    int adaptive_quality = 0, adaptive_gop = 0;
    int cq_trials = 5, two_pass_trials = 5, min_key_interval = 8;
    double target_psnr = 0.0, psnr_min = 30.0, psnr_max = 35.0;
    int gop = 30, search = 4, max_frames = 0, syntax = HLV1_VERSION, motion_candidates = 1;
    int gop_set = 0, search_set = 0, motion_candidates_set = 0;
    double scene_cut = 38.0, keyframe_bias = 1.00;
    double chroma_scale = 1.35, rd_lambda_scale = 1.0;
    double decode_cycle_weight = 0.0, ac_deadzone = 0.5;
    int rd_luma_weight = 4;
    const char *preset = "balanced";
    const char *recon_path = NULL;
    const char *audio_path = NULL;
    int audio_rate = 16000;
    const char *cq_log_path = NULL;
    const char *two_pass_log_path = NULL;

    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--quality") && i + 1 < argc) quality = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--qstep-y") && i + 1 < argc) qstep_y = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--qstep-uv") && i + 1 < argc) qstep_uv = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bitrate") && i + 1 < argc) bitrate_kbps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--two-pass-window") && i + 1 < argc) two_pass_window_seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--two-pass-trials") && i + 1 < argc) two_pass_trials = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--two-pass-log") && i + 1 < argc) two_pass_log_path = argv[++i];
        else if (!strcmp(argv[i], "--target-psnr") && i + 1 < argc) target_psnr = atof(argv[++i]);
        else if (!strcmp(argv[i], "--adaptive-quality")) adaptive_quality = 1;
        else if (!strcmp(argv[i], "--psnr-min") && i + 1 < argc) psnr_min = atof(argv[++i]);
        else if (!strcmp(argv[i], "--psnr-max") && i + 1 < argc) psnr_max = atof(argv[++i]);
        else if (!strcmp(argv[i], "--cq-trials") && i + 1 < argc) cq_trials = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cq-log") && i + 1 < argc) cq_log_path = argv[++i];
        else if (!strcmp(argv[i], "--syntax") && i + 1 < argc) syntax = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gop") && i + 1 < argc) { gop = atoi(argv[++i]); gop_set = 1; }
        else if (!strcmp(argv[i], "--adaptive-gop")) adaptive_gop = 1;
        else if (!strcmp(argv[i], "--min-key-interval") && i + 1 < argc) min_key_interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--keyframe-bias") && i + 1 < argc) keyframe_bias = atof(argv[++i]);
        else if (!strcmp(argv[i], "--search") && i + 1 < argc) { search = atoi(argv[++i]); search_set = 1; }
        else if (!strcmp(argv[i], "--scene-cut") && i + 1 < argc) scene_cut = atof(argv[++i]);
        else if (!strcmp(argv[i], "--chroma-scale") && i + 1 < argc) chroma_scale = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rd-luma-weight") && i + 1 < argc) rd_luma_weight = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rd-lambda-scale") && i + 1 < argc) rd_lambda_scale = atof(argv[++i]);
        else if (!strcmp(argv[i], "--decode-cycle-weight") && i + 1 < argc) decode_cycle_weight = atof(argv[++i]);
        else if (!strcmp(argv[i], "--ac-deadzone") && i + 1 < argc) ac_deadzone = atof(argv[++i]);
        else if (!strcmp(argv[i], "--motion-candidates") && i + 1 < argc) { motion_candidates = atoi(argv[++i]); motion_candidates_set = 1; }
        else if (!strcmp(argv[i], "--max-frames") && i + 1 < argc) max_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--recon") && i + 1 < argc) recon_path = argv[++i];
        else if (!strcmp(argv[i], "--audio-u8") && i + 1 < argc) audio_path = argv[++i];
        else if (!strcmp(argv[i], "--audio-rate") && i + 1 < argc) audio_rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--preset") && i + 1 < argc) preset = argv[++i];
        else { usage(argv[0]); return 2; }
    }

    int pgop, psearch, pmotion_candidates;
    if (apply_preset(preset, &pgop, &psearch, &pmotion_candidates) < 0) {
        fprintf(stderr, "Unknown preset: %s\n", preset); return 2;
    }
    if (!gop_set) gop = pgop;
    if (!search_set) search = psearch;
    if (!motion_candidates_set) motion_candidates = pmotion_candidates;

    if (quality < 1 || quality > 100 || qstep_y < 0 || qstep_y > HLV1_MAX_QSTEP ||
        qstep_uv < 0 || qstep_uv > HLV1_MAX_QSTEP || (qstep_uv && !qstep_y) ||
        gop < 1 || search < 0 || search > 64 || max_frames < 0 ||
        min_key_interval < 1 || min_key_interval >= gop ||
        keyframe_bias < 1.0 || keyframe_bias > 1.25 ||
        bitrate_kbps < 0 || bitrate_kbps > 100000 ||
        syntax < HLV1_STREAM_VERSION_1 || syntax > HLV1_VERSION ||
        chroma_scale < 0.25 || chroma_scale > 4.0 ||
        rd_luma_weight < 1 || rd_luma_weight > 16 ||
        rd_lambda_scale <= 0.0 || rd_lambda_scale > 16.0 ||
        decode_cycle_weight < 0.0 || decode_cycle_weight > 4.0 ||
        ac_deadzone < 0.5 || ac_deadzone > 2.0 ||
        motion_candidates < 1 || motion_candidates > 8 ||
        target_psnr < 0.0 || target_psnr > 70.0 ||
        psnr_min < 20.0 || psnr_min > 60.0 ||
        psnr_max < 20.0 || psnr_max > 60.0 || psnr_min > psnr_max ||
        cq_trials < 2 || cq_trials > 10 ||
        two_pass_window_seconds < 0.0 || two_pass_window_seconds > 120.0 ||
        two_pass_trials < 2 || two_pass_trials > 10 ||
        audio_rate < 1000 || audio_rate > 65535 ||
        (audio_path && !strcmp(audio_path, "-")) ||
        (two_pass_window_seconds > 0.0 && !bitrate_kbps) ||
        (two_pass_log_path && two_pass_window_seconds <= 0.0) ||
        (adaptive_quality && target_psnr > 0.0) ||
        (bitrate_kbps && (adaptive_quality || target_psnr > 0.0)) ||
        (syntax < HLV1_STREAM_VERSION_4 && (qstep_y > 255 || qstep_uv > 255))) {
        fprintf(stderr, "Invalid encoder option\n"); return 2;
    }

    FILE *in = open_input(argv[1]);
    if (!in) { perror(argv[1]); return 1; }
    HLV1Y4M y4m;
    int r = hlv1_y4m_open_read(&y4m, in);
    if (r < 0) {
        fprintf(stderr, "Y4M: %s\n", hlv1_strerror(r));
        close_if_file(in, stdin); return 1;
    }

    AudioInput audio = {0};
    if (audio_path) {
        audio.file = fopen(audio_path, "rb");
        if (!audio.file) {
            perror(audio_path);
            close_if_file(in, stdin);
            return 1;
        }
        audio.sample_rate = (uint32_t)audio_rate;
    }

    int output_seekable = 0;
    FILE *out = open_output(argv[2], &output_seekable);
    if (!out) { perror(argv[2]); close_if_file(in, stdin); return 1; }

    FILE *recon = NULL; HLV1Y4M ry4m;
    if (recon_path) {
        if (!strcmp(recon_path, "-") && out == stdout) {
            fprintf(stderr, "Cannot write both HLV and reconstruction to stdout\n");
            close_if_file(in, stdin); close_if_file(out, stdout); return 2;
        }
        recon = !strcmp(recon_path, "-") ? stdout : fopen(recon_path, "wb");
        if (!recon) { perror(recon_path); close_if_file(in, stdin); close_if_file(out, stdout); return 1; }
        r = hlv1_y4m_open_write(&ry4m, recon, y4m.width, y4m.height, y4m.fps_num, y4m.fps_den);
        if (r < 0) { fprintf(stderr, "Recon Y4M: %s\n", hlv1_strerror(r)); return 1; }
    }

    int cq_mode = adaptive_quality || target_psnr > 0.0;
    FILE *cq_log = NULL;
    FILE *two_pass_log = NULL;
    if (cq_log_path) {
        cq_log = fopen(cq_log_path, "w");
        if (!cq_log) {
            perror(cq_log_path);
            close_if_file(in, stdin); close_if_file(out, stdout);
            if (recon && recon != stdout) fclose(recon);
            return 1;
        }
        fprintf(cq_log,
                "frame,target_psnr,actual_psnr,motion_mad,raw_motion_mad,"
                "detail,q_y,q_uv,packet_bytes,frame_type\n");
    }
    if (two_pass_log_path) {
        two_pass_log = fopen(two_pass_log_path, "w");
        if (!two_pass_log) {
            perror(two_pass_log_path);
            close_if_file(in, stdin); close_if_file(out, stdout);
            if (recon && recon != stdout) fclose(recon);
            if (cq_log) fclose(cq_log);
            return 1;
        }
        fprintf(two_pass_log,
                "window,first_frame,frames,trials,chosen_q,probe_bytes,"
                "target_bytes,actual_bytes,actual_kbps,relative_error,debt_bytes\n");
    }

    /* frame_count=0 is valid for a non-seekable/streaming output. */
    HLV1Header h = {(uint16_t)y4m.width, (uint16_t)y4m.height,
                    (uint16_t)y4m.fps_num, (uint16_t)y4m.fps_den,
                    0, (uint16_t)gop, (uint8_t)quality, (uint8_t)search, 0, (uint8_t)syntax};
    if (audio.file) {
        h.flags |= HLV1_FLAG_AUDIO;
        h.audio_codec = HLV1_AUDIO_PCM_U8;
        h.audio_sample_rate = (uint16_t)audio.sample_rate;
        h.audio_channels = 1;
    }
    if ((r = hlv1_header_write(out, &h)) < 0) {
        fprintf(stderr, "%s\n", hlv1_strerror(r)); return 1;
    }

    HLV1Encoder *enc = hlv1_encoder_create(&h, scene_cut);
    int configured_q_uv = qstep_uv;
    int rate_q_y = qstep_y, rate_q_uv = configured_q_uv;
    if ((bitrate_kbps || cq_mode) && !rate_q_y)
        hlv1_quality_to_qsteps(quality, &rate_q_y, &rate_q_uv);
    if ((qstep_y || bitrate_kbps || cq_mode) && !rate_q_uv) {
        rate_q_uv = (int)(rate_q_y * chroma_scale + 0.5);
        if (rate_q_uv < 1) rate_q_uv = 1;
        if (rate_q_uv > HLV1_MAX_QSTEP) rate_q_uv = HLV1_MAX_QSTEP;
    }
    configured_q_uv = rate_q_uv;
    if (enc && (hlv1_encoder_set_chroma_scale(enc, chroma_scale) < 0 ||
                ((qstep_y || bitrate_kbps || cq_mode) &&
                 hlv1_encoder_set_quantization(enc, rate_q_y, rate_q_uv) < 0) ||
                hlv1_encoder_set_rd_parameters(enc, rd_lambda_scale, rd_luma_weight) < 0 ||
                hlv1_encoder_set_decode_cycle_weight(enc,
                                                     decode_cycle_weight) < 0 ||
                hlv1_encoder_set_ac_deadzone(enc, ac_deadzone) < 0 ||
                hlv1_encoder_set_motion_candidates(enc, motion_candidates) < 0 ||
                (adaptive_gop &&
                 hlv1_encoder_set_adaptive_gop(enc,
                                               (unsigned)min_key_interval,
                                               keyframe_bias) < 0))) {
        fprintf(stderr, "Invalid RDO parameters\n");
        hlv1_encoder_destroy(enc);
        return 2;
    }
    HLV1Frame input;
    if (!enc || hlv1_frame_alloc(&input, y4m.width, y4m.height) < 0) {
        fprintf(stderr, "Cannot allocate encoder\n"); return 1;
    }
    HLV1Frame previous_input;
    memset(&previous_input, 0, sizeof previous_input);
    int have_previous_input = 0;
    if (adaptive_quality &&
        hlv1_frame_alloc(&previous_input, y4m.width, y4m.height) < 0) {
        fprintf(stderr, "Cannot allocate adaptive-quality analysis frame\n");
        return 1;
    }

    uint32_t encoded_frames = 0;
    double target_bytes_per_frame = bitrate_kbps
        ? (double)bitrate_kbps * 1000.0 / 8.0 * y4m.fps_den / y4m.fps_num
        : 0.0;
    double rate_buffer = 0.0;
    double rate_window_frames = 2.0 * y4m.fps_num / (double)y4m.fps_den;
    if (rate_window_frames < 12.0) rate_window_frames = 12.0;
    int min_rate_q = rate_q_y ? rate_q_y : HLV1_MAX_QSTEP;
    int max_rate_q = rate_q_y;
    uint64_t sum_rate_q = 0;
    double smoothed_target_psnr = 0.0;
    int have_smoothed_target = 0;
    double sum_target_psnr = 0.0, sum_actual_psnr = 0.0;
    double sum_motion = 0.0, sum_detail = 0.0;
    double start = now_sec();
    if (two_pass_window_seconds > 0.0) {
        int frames_per_window = (int)llround(two_pass_window_seconds *
                                                 y4m.fps_num / y4m.fps_den);
        if (frames_per_window < 1) frames_per_window = 1;
        int maximum_q = syntax < HLV1_STREAM_VERSION_4 ? 255 : HLV1_MAX_QSTEP;
        double rate_debt = 0.0;
        unsigned window_index = 0;
        int input_eof = 0;

        while ((!max_frames || (int)encoded_frames < max_frames) && !input_eof) {
            FILE *spool = tmpfile();
            if (!spool) {
                perror("tmpfile");
                return 1;
            }
            int window_count = 0;
            while (window_count < frames_per_window &&
                   (!max_frames || (int)encoded_frames + window_count < max_frames)) {
                r = hlv1_y4m_read_frame(&y4m, &input);
                if (r == HLV1_EOF) {
                    input_eof = 1;
                    break;
                }
                if (r < 0) {
                    fprintf(stderr, "Read frame: %s\n", hlv1_strerror(r));
                    fclose(spool);
                    return 1;
                }
                r = write_raw420_frame(spool, &input);
                if (r < 0) {
                    fprintf(stderr, "Window spool: %s\n", hlv1_strerror(r));
                    fclose(spool);
                    return 1;
                }
                ++window_count;
            }
            if (!window_count) {
                fclose(spool);
                break;
            }
            if (fflush(spool) != 0) {
                fprintf(stderr, "Window spool flush failed\n");
                fclose(spool);
                return 1;
            }

            double nominal_target = target_bytes_per_frame * window_count;
            double debt_correction = clamp_double(rate_debt * 0.50,
                                                   -0.25 * nominal_target,
                                                    0.25 * nominal_target);
            double adjusted_target = clamp_double(
                nominal_target - debt_correction,
                0.50 * nominal_target, 1.50 * nominal_target);

            int tried_q[16];
            int tried_count = 0;
            int q = clamp_qstep(rate_q_y, maximum_q);
            int over_q = 0;  /* output too large; q must increase */
            int under_q = 0; /* output small enough; q may decrease */
            int chosen_q = q;
            uint64_t chosen_probe_bytes = 0;
            double best_error = HUGE_VAL;

            for (int trial = 0; trial < two_pass_trials; ++trial) {
                int duplicate = 0;
                for (int i = 0; i < tried_count; ++i)
                    if (tried_q[i] == q) duplicate = 1;
                if (duplicate) break;
                tried_q[tried_count++] = q;

                uint64_t probe_bytes = 0;
                r = analyse_window(enc, spool, &input, window_count, q,
                                   chroma_scale, &probe_bytes);
                if (r < 0) {
                    fprintf(stderr, "Window first pass: %s\n", hlv1_strerror(r));
                    fclose(spool);
                    return 1;
                }
                double error = fabs(log(((double)probe_bytes + 1.0) /
                                        (adjusted_target + 1.0)));
                if (error < best_error) {
                    best_error = error;
                    chosen_q = q;
                    chosen_probe_bytes = probe_bytes;
                }
                double relative = fabs((double)probe_bytes - adjusted_target) /
                                  (adjusted_target + 1.0);
                if (relative <= 0.02) break;

                if ((double)probe_bytes > adjusted_target) {
                    if (q > over_q) over_q = q;
                } else {
                    if (!under_q || q < under_q) under_q = q;
                }

                int next_q;
                if (over_q && under_q && under_q - over_q > 1) {
                    next_q = over_q + (under_q - over_q) / 2;
                } else if (over_q && !under_q) {
                    next_q = q <= maximum_q / 2 ? q * 2 : maximum_q;
                } else if (under_q && !over_q) {
                    next_q = q > 1 ? (q + 1) / 2 : 1;
                } else {
                    break;
                }
                q = clamp_qstep(next_q, maximum_q);
            }

            int chosen_q_uv = clamp_qstep(
                (int)llround(chosen_q * chroma_scale), maximum_q);
            r = hlv1_encoder_set_quantization(enc, chosen_q, chosen_q_uv);
            if (r < 0) {
                fprintf(stderr, "Window quantizer: %s\n", hlv1_strerror(r));
                fclose(spool);
                return 1;
            }

            rewind(spool);
            uint64_t actual_total = 0;
            uint32_t first_frame = encoded_frames;
            for (int i = 0; i < window_count; ++i) {
                r = read_raw420_frame(spool, &input);
                if (r < 0) {
                    fprintf(stderr, "Window second pass read: %s\n",
                            hlv1_strerror(r));
                    fclose(spool);
                    return 1;
                }
                HLV1Packet packet = {0};
                const HLV1Frame *rec = NULL;
                r = hlv1_encoder_encode(enc, &input, &packet, &rec);
                if (r < 0) {
                    fprintf(stderr, "Window second pass encode: %s\n",
                            hlv1_strerror(r));
                    hlv1_packet_free(&packet);
                    fclose(spool);
                    return 1;
                }
                actual_total += packet.payload_size + HLV1_FRAME_HEADER_SIZE;
                r = append_audio_interval(&audio, y4m.fps_num, y4m.fps_den,
                                          &packet);
                if (r < 0) {
                    fprintf(stderr, "Audio: %s\n", hlv1_strerror(r));
                    hlv1_packet_free(&packet);
                    fclose(spool);
                    return 1;
                }
                r = hlv1_packet_write(out, &packet);
                hlv1_packet_free(&packet);
                if (r < 0) {
                    fprintf(stderr, "Write: %s\n", hlv1_strerror(r));
                    fclose(spool);
                    return 1;
                }
                if (recon && (r = hlv1_y4m_write_frame(&ry4m, rec)) < 0) {
                    fprintf(stderr, "Recon write: %s\n", hlv1_strerror(r));
                    fclose(spool);
                    return 1;
                }
                if (chosen_q < min_rate_q) min_rate_q = chosen_q;
                if (chosen_q > max_rate_q) max_rate_q = chosen_q;
                sum_rate_q += (uint64_t)chosen_q;
                ++encoded_frames;
                if ((encoded_frames % 100) == 0)
                    fprintf(stderr, "\rEncoded %u frames", encoded_frames);
            }

            rate_debt += (double)actual_total - nominal_target;
            rate_debt = clamp_double(rate_debt,
                                     -2.0 * nominal_target,
                                      2.0 * nominal_target);
            rate_q_y = chosen_q;
            rate_q_uv = chosen_q_uv;

            double seconds = window_count * (double)y4m.fps_den / y4m.fps_num;
            double actual_kbps = seconds > 0.0 ?
                actual_total * 8.0 / seconds / 1000.0 : 0.0;
            double relative_error = ((double)actual_total - nominal_target) /
                                    (nominal_target + 1.0);
            fprintf(stderr,
                    "\nWindow %u: frames %u..%u, %d probes, q=%d, "
                    "probe %.1f kB, target %.1f kB, actual %.1f kB "
                    "(%.1f kbit/s, %+.2f%%)\n",
                    window_index, first_frame, encoded_frames - 1,
                    tried_count, chosen_q, chosen_probe_bytes / 1000.0,
                    nominal_target / 1000.0, actual_total / 1000.0,
                    actual_kbps, relative_error * 100.0);
            if (two_pass_log) {
                fprintf(two_pass_log,
                        "%u,%u,%d,%d,%d,%llu,%.0f,%llu,%.3f,%.6f,%.0f\n",
                        window_index, first_frame, window_count, tried_count,
                        chosen_q, (unsigned long long)chosen_probe_bytes,
                        nominal_target, (unsigned long long)actual_total,
                        actual_kbps, relative_error, rate_debt);
                fflush(two_pass_log);
            }
            ++window_index;
            fclose(spool);
        }
    } else {
    while (!max_frames || (int)encoded_frames < max_frames) {
        r = hlv1_y4m_read_frame(&y4m, &input);
        if (r == HLV1_EOF) break;
        if (r < 0) { fprintf(stderr, "Read frame: %s\n", hlv1_strerror(r)); return 1; }

        if (bitrate_kbps) {
            rate_q_uv = (int)(rate_q_y * chroma_scale + 0.5);
            if (rate_q_uv < 1) rate_q_uv = 1;
            if (rate_q_uv > HLV1_MAX_QSTEP) rate_q_uv = HLV1_MAX_QSTEP;
            r = hlv1_encoder_set_quantization(enc, rate_q_y, rate_q_uv);
            if (r < 0) { fprintf(stderr, "Rate quantizer: %s\n", hlv1_strerror(r)); return 1; }
            if (rate_q_y < min_rate_q) min_rate_q = rate_q_y;
            if (rate_q_y > max_rate_q) max_rate_q = rate_q_y;
            sum_rate_q += (uint64_t)rate_q_y;
        }

        HLV1Packet p;
        memset(&p, 0, sizeof p);
        const HLV1Frame *rec = NULL;
        double frame_target_psnr = 0.0, frame_actual_psnr = 0.0;
        double frame_motion = 0.0, frame_detail = 0.0, frame_raw_motion = 0.0;

        if (cq_mode) {
            double raw_target = target_psnr;
            if (adaptive_quality) {
                raw_target = adaptive_psnr_target(
                    &previous_input, &input, have_previous_input,
                    psnr_min, psnr_max, scene_cut,
                    &frame_motion, &frame_detail, &frame_raw_motion);
            } else {
                frame_detail = luma_detail_score(&input);
            }

            if (!have_smoothed_target) {
                smoothed_target_psnr = raw_target;
                have_smoothed_target = 1;
            } else {
                double delta = raw_target - smoothed_target_psnr;
                /* Lower quality can react relatively quickly to motion;
                   raising quality is deliberately slower to avoid pumping. */
                double limit = delta < 0.0 ? 0.90 : 0.25;
                if (delta < -limit) delta = -limit;
                if (delta > limit) delta = limit;
                smoothed_target_psnr += delta;
            }
            frame_target_psnr = smoothed_target_psnr;

            CQTrial chosen = {0};
            int maximum_q = syntax < HLV1_STREAM_VERSION_4 ?
                            255 : HLV1_MAX_QSTEP;
            r = encode_constant_quality(enc, &input, frame_target_psnr,
                                        chroma_scale, rd_lambda_scale,
                                        rd_luma_weight, rate_q_y, maximum_q,
                                        cq_trials, &chosen);
            if (r < 0) {
                fprintf(stderr, "Constant-quality encode: %s\n",
                        hlv1_strerror(r));
                return 1;
            }
            hlv1_encoder_destroy(enc);
            enc = chosen.encoder;
            chosen.encoder = NULL;
            p = chosen.packet;
            memset(&chosen.packet, 0, sizeof chosen.packet);
            rec = chosen.reconstructed;
            frame_actual_psnr = chosen.psnr;
            rate_q_y = chosen.q_y;
            rate_q_uv = chosen.q_uv;
            if (rate_q_y < min_rate_q) min_rate_q = rate_q_y;
            if (rate_q_y > max_rate_q) max_rate_q = rate_q_y;
            sum_rate_q += (uint64_t)rate_q_y;
            cq_trial_free(&chosen);
        } else {
            r = hlv1_encoder_encode(enc, &input, &p, &rec);
            if (r < 0) {
                fprintf(stderr, "Encode: %s\n", hlv1_strerror(r));
                return 1;
            }
        }
        uint32_t encoded_packet_bytes = p.payload_size + HLV1_FRAME_HEADER_SIZE;
        int encoded_frame_type = p.frame_type;
        r = append_audio_interval(&audio, y4m.fps_num, y4m.fps_den, &p);
        if (r < 0) {
            fprintf(stderr, "Audio: %s\n", hlv1_strerror(r));
            hlv1_packet_free(&p);
            return 1;
        }
        r = hlv1_packet_write(out, &p);
        hlv1_packet_free(&p);
        if (r < 0) { fprintf(stderr, "Write: %s\n", hlv1_strerror(r)); return 1; }

        if (bitrate_kbps) {
            double error = encoded_packet_bytes - target_bytes_per_frame;
            rate_buffer += error;
            /* Leaky virtual buffer: approximately a two-second horizon. */
            rate_buffer *= 1.0 - 1.0 / rate_window_frames;
            double instant_ratio = (encoded_packet_bytes + 1.0) /
                                   (target_bytes_per_frame + 1.0);
            double buffer_ratio = rate_buffer /
                (target_bytes_per_frame * rate_window_frames + 1.0);
            double instant_gain = encoded_frame_type == HLV1_FRAME_KEY ? 0.06 : 0.16;
            double factor = exp(instant_gain * log(instant_ratio) +
                                0.22 * buffer_ratio);
            if (factor < 0.80) factor = 0.80;
            if (factor > 1.25) factor = 1.25;
            int next_q = (int)(rate_q_y * factor + 0.5);
            if (next_q == rate_q_y && error > target_bytes_per_frame * 0.08) next_q++;
            if (next_q == rate_q_y && error < -target_bytes_per_frame * 0.08) next_q--;
            int max_q = syntax < HLV1_STREAM_VERSION_4 ? 255 : HLV1_MAX_QSTEP;
            if (next_q < 1) next_q = 1;
            if (next_q > max_q) next_q = max_q;
            rate_q_y = next_q;
        }

        if (cq_mode) {
            sum_target_psnr += frame_target_psnr;
            sum_actual_psnr += frame_actual_psnr;
            sum_motion += frame_motion;
            sum_detail += frame_detail;
            if (cq_log) {
                fprintf(cq_log,
                        "%u,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%u,%c\n",
                        encoded_frames, frame_target_psnr, frame_actual_psnr,
                        frame_motion, frame_raw_motion, frame_detail,
                        rate_q_y, rate_q_uv, encoded_packet_bytes,
                        encoded_frame_type == HLV1_FRAME_KEY ? 'K' : 'P');
            }
        }
        if (recon && (r = hlv1_y4m_write_frame(&ry4m, rec)) < 0) {
            fprintf(stderr, "Recon write: %s\n", hlv1_strerror(r)); return 1;
        }
        if (adaptive_quality) {
            r = hlv1_frame_copy_visible(&previous_input, &input);
            if (r < 0) {
                fprintf(stderr, "Adaptive-quality history: %s\n",
                        hlv1_strerror(r));
                return 1;
            }
            have_previous_input = 1;
        }
        ++encoded_frames;
        if ((encoded_frames % 100) == 0)
            fprintf(stderr, "\rEncoded %u frames", encoded_frames);
    }

    }

    if (fflush(out) != 0) { fprintf(stderr, "Output flush failed\n"); return 1; }
    double elapsed = now_sec() - start;

    if (output_seekable) {
        h.frame_count = encoded_frames;
        if (fseek(out, 0, SEEK_SET) || hlv1_header_write(out, &h) < 0 || fflush(out)) {
            fprintf(stderr, "Cannot update HLV header\n"); return 1;
        }
    }

    const HLV1Stats *s = hlv1_encoder_stats(enc);
    fprintf(stderr, "\rEncoded %u frames in %.3f s (%.2f fps), payload %.3f MiB, preset %s\n",
            encoded_frames, elapsed, elapsed > 0 ? encoded_frames / elapsed : 0,
            s ? s->payload_bytes / 1048576.0 : 0.0, preset);
    if (audio.file)
        fprintf(stderr, "Audio: PCM_U8 mono %u Hz, %.3f MiB\n",
                audio.sample_rate, audio.bytes / 1048576.0);
    fprintf(stderr, "RDO: chroma-scale %.3f, luma-weight %d, lambda-scale %.3f, decode-cycle-weight %.3f, AC-deadzone %.3f, motion-candidates %d",
            chroma_scale, rd_luma_weight, rd_lambda_scale,
            decode_cycle_weight, ac_deadzone,
            motion_candidates);
    if (qstep_y && !bitrate_kbps && !cq_mode)
        fprintf(stderr, ", exact qsteps Y=%d UV=%d", qstep_y, configured_q_uv);
    if (adaptive_gop)
        fprintf(stderr, ", adaptive GOP min=%d max=%d bias=%.3f",
                min_key_interval, gop, keyframe_bias);
    if (bitrate_kbps && encoded_frames) {
        double stream_bytes = HLV1_HEADER_SIZE +
            encoded_frames * (double)HLV1_FRAME_HEADER_SIZE +
            (s ? s->payload_bytes : 0);
        double seconds = encoded_frames * (double)y4m.fps_den / y4m.fps_num;
        double actual_kbps = seconds > 0 ? stream_bytes * 8.0 / seconds / 1000.0 : 0;
        fprintf(stderr, ", target %d kbit/s actual %.1f, qY avg %.1f range %d..%d",
                bitrate_kbps, actual_kbps,
                sum_rate_q / (double)encoded_frames, min_rate_q, max_rate_q);
        if (two_pass_window_seconds > 0.0)
            fprintf(stderr, ", local two-pass window %.2f s (%d probes max)",
                    two_pass_window_seconds, two_pass_trials);
    }
    if (cq_mode && encoded_frames) {
        fprintf(stderr,
                ", %s PSNR target avg %.2f actual avg %.2f, qY avg %.1f range %d..%d",
                adaptive_quality ? "adaptive" : "constant",
                sum_target_psnr / encoded_frames,
                sum_actual_psnr / encoded_frames,
                sum_rate_q / (double)encoded_frames,
                min_rate_q, max_rate_q);
        if (adaptive_quality)
            fprintf(stderr, ", motion avg %.2f detail avg %.2f",
                    sum_motion / encoded_frames, sum_detail / encoded_frames);
    }
    fputc('\n', stderr);
    if (s && s->macroblocks) fprintf(stderr,
        "Modes: skip %.1f%%, global %.1f%%, inter %.1f%%, split8 %.1f%%, fill %.1f%%, palette %.1f%% (2/4/8 %.1f/%.1f/%.1f%%), gradient %.1f%%, literal %.1f%%, intra %.1f%%; syntax v%d\n",
        100.0*s->skipped/s->macroblocks, 100.0*s->global/s->macroblocks,
        100.0*s->inter/s->macroblocks, 100.0*s->split_inter/s->macroblocks,
        100.0*s->fill/s->macroblocks, 100.0*s->palette/s->macroblocks,
        100.0*s->palette_2/s->macroblocks,
        100.0*s->palette_4/s->macroblocks,
        100.0*s->palette_8/s->macroblocks,
        100.0*s->gradient/s->macroblocks,
        100.0*s->literal/s->macroblocks,
        100.0*(s->intra_dc+s->intra_vertical+s->intra_horizontal+s->intra_plane)/s->macroblocks,
        syntax);
    if (s && encoded_frames > 0)
        fprintf(stderr, "Estimated decoder work: %.0f cycles/frame (architecture-independent RDO model)\n",
                (double)s->estimated_decode_cycles / encoded_frames);

    hlv1_frame_free(&input);
    hlv1_frame_free(&previous_input);
    hlv1_encoder_destroy(enc);
    close_if_file(in, stdin); close_if_file(out, stdout);
    if (recon && recon != stdout) fclose(recon);
    if (cq_log) fclose(cq_log);
    if (two_pass_log) fclose(two_pass_log);
    if (audio.file) fclose(audio.file);
    return 0;
}
