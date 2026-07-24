/*
 * Minimal YUV4MPEG2 adapter.
 *
 * Only planar 8-bit 4:2:0 streams are accepted because that is the normative
 * HLV-1 input/output format.  FFmpeg handles all other containers, scaling,
 * frame-rate conversion, and color-format conversion through a pipe.
 */
#include "hlv1_internal.h"

#include <ctype.h>

/* Y4M control lines are ASCII and terminate before binary plane data. */
static int read_line(FILE *f, char *buf, size_t cap) {
    if (!fgets(buf, (int)cap, f)) return feof(f) ? HLV1_EOF : HLV1_ERR_IO;
    size_t n = strlen(buf);
    if (!n || buf[n - 1] != '\n') return HLV1_ERR_FORMAT;
    return HLV1_OK;
}

int hlv1_y4m_open_read(HLV1Y4M *y4m, FILE *file) {
    if (!y4m || !file) return HLV1_ERR_ARGUMENT;
    memset(y4m, 0, sizeof *y4m);
    char line[4096];
    int r = read_line(file, line, sizeof line);
    if (r != HLV1_OK) return r;
    if (strncmp(line, "YUV4MPEG2 ", 10)) return HLV1_ERR_FORMAT;
    int width = 0, height = 0, fn = 0, fd = 0;
    int chroma_ok = 0;
    for (char *tok = strtok(line + 10, " \n"); tok; tok = strtok(NULL, " \n")) {
        if (tok[0] == 'W') width = atoi(tok + 1);
        else if (tok[0] == 'H') height = atoi(tok + 1);
        else if (tok[0] == 'F') {
            if (sscanf(tok + 1, "%d:%d", &fn, &fd) != 2) return HLV1_ERR_FORMAT;
        } else if (tok[0] == 'C') {
            chroma_ok = !strncmp(tok + 1, "420", 3);
        }
    }
    if (!width || !height || !fn || !fd || !chroma_ok || (width & 1) || (height & 1))
        return HLV1_ERR_FORMAT;
    y4m->file = file;
    y4m->width = width;
    y4m->height = height;
    y4m->fps_num = fn;
    y4m->fps_den = fd;
    return HLV1_OK;
}

int hlv1_y4m_open_write(HLV1Y4M *y4m, FILE *file,
                        int width, int height, int fps_num, int fps_den) {
    if (!y4m || !file || width <= 0 || height <= 0 || fps_num <= 0 || fps_den <= 0 ||
        (width & 1) || (height & 1)) return HLV1_ERR_ARGUMENT;
    memset(y4m, 0, sizeof *y4m);
    if (fprintf(file, "YUV4MPEG2 W%d H%d F%d:%d Ip A0:0 C420jpeg\n",
                width, height, fps_num, fps_den) < 0)
        return HLV1_ERR_IO;
    y4m->file = file;
    y4m->width = width;
    y4m->height = height;
    y4m->fps_num = fps_num;
    y4m->fps_den = fps_den;
    y4m->writing = 1;
    return HLV1_OK;
}

/* Replicate visible edge samples into the 16-pixel padding.  Predictors can
 * then process complete macroblocks without special right/bottom cases. */
static void edge_pad(HLV1Frame *f) {
    for (int y = 0; y < f->height; ++y) {
        uint8_t *row = f->y + y * f->stride_y;
        uint8_t last = row[f->width - 1];
        memset(row + f->width, last, (size_t)(f->padded_width - f->width));
    }
    for (int y = f->height; y < f->padded_height; ++y)
        memcpy(f->y + y * f->stride_y, f->y + (f->height - 1) * f->stride_y, f->padded_width);

    int cw = f->width / 2, ch = f->height / 2;
    int pcw = f->padded_width / 2, pch = f->padded_height / 2;
    for (int plane = 0; plane < 2; ++plane) {
        uint8_t *p = plane ? f->v : f->u;
        int stride = plane ? f->stride_v : f->stride_u;
        for (int y = 0; y < ch; ++y) {
            uint8_t *row = p + y * stride;
            uint8_t last = row[cw - 1];
            memset(row + cw, last, (size_t)(pcw - cw));
        }
        for (int y = ch; y < pch; ++y)
            memcpy(p + y * stride, p + (ch - 1) * stride, pcw);
    }
}

int hlv1_y4m_read_frame(HLV1Y4M *y4m, HLV1Frame *frame) {
    if (!y4m || !frame || !y4m->file || y4m->writing ||
        frame->width != y4m->width || frame->height != y4m->height)
        return HLV1_ERR_ARGUMENT;
    char line[4096];
    int r = read_line(y4m->file, line, sizeof line);
    if (r != HLV1_OK) return r;
    if (strncmp(line, "FRAME", 5)) return HLV1_ERR_FORMAT;
    for (int y = 0; y < frame->height; ++y)
        if (fread(frame->y + y * frame->stride_y, 1, frame->width, y4m->file) != (size_t)frame->width)
            return HLV1_ERR_IO;
    int cw = frame->width / 2, ch = frame->height / 2;
    for (int y = 0; y < ch; ++y)
        if (fread(frame->u + y * frame->stride_u, 1, cw, y4m->file) != (size_t)cw)
            return HLV1_ERR_IO;
    for (int y = 0; y < ch; ++y)
        if (fread(frame->v + y * frame->stride_v, 1, cw, y4m->file) != (size_t)cw)
            return HLV1_ERR_IO;
    edge_pad(frame);
    return HLV1_OK;
}

int hlv1_y4m_write_frame(HLV1Y4M *y4m, const HLV1Frame *frame) {
    if (!y4m || !frame || !y4m->file || !y4m->writing ||
        frame->width != y4m->width || frame->height != y4m->height)
        return HLV1_ERR_ARGUMENT;
    if (fwrite("FRAME\n", 1, 6, y4m->file) != 6) return HLV1_ERR_IO;
    for (int y = 0; y < frame->height; ++y)
        if (fwrite(frame->y + y * frame->stride_y, 1, frame->width, y4m->file) != (size_t)frame->width)
            return HLV1_ERR_IO;
    int cw = frame->width / 2, ch = frame->height / 2;
    for (int y = 0; y < ch; ++y)
        if (fwrite(frame->u + y * frame->stride_u, 1, cw, y4m->file) != (size_t)cw)
            return HLV1_ERR_IO;
    for (int y = 0; y < ch; ++y)
        if (fwrite(frame->v + y * frame->stride_v, 1, cw, y4m->file) != (size_t)cw)
            return HLV1_ERR_IO;
    return HLV1_OK;
}
