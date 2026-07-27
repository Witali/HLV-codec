#ifndef AMRNB_PORT_H
#define AMRNB_PORT_H

#ifndef OSCL_EXPORT_REF
#define OSCL_EXPORT_REF
#endif
#ifndef OSCL_IMPORT_REF
#define OSCL_IMPORT_REF
#endif
#ifndef OSCL_UNUSED_ARG
#define OSCL_UNUSED_ARG(x) ((void)(x))
#endif

#ifdef _MSC_VER
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

static inline bool amrnb_add_overflow_i32(int64_t left, int64_t right,
                                          int32_t *result) {
    const int64_t wide = left + right;
    *result = (int32_t)(uint32_t)wide;
    return wide < INT32_MIN || wide > INT32_MAX;
}

static inline bool amrnb_sub_overflow_i32(int64_t left, int64_t right,
                                          int32_t *result) {
    const int64_t wide = left - right;
    *result = (int32_t)(uint32_t)wide;
    return wide < INT32_MIN || wide > INT32_MAX;
}

static inline bool amrnb_mul_overflow_i32(int64_t left, int64_t right,
                                          int32_t *result) {
    const int64_t wide = left * right;
    *result = (int32_t)(uint32_t)wide;
    return wide < INT32_MIN || wide > INT32_MAX;
}

#define __builtin_add_overflow(left, right, result) \
    amrnb_add_overflow_i32((int64_t)(left), (int64_t)(right), \
                           (int32_t *)(result))
#define __builtin_sub_overflow(left, right, result) \
    amrnb_sub_overflow_i32((int64_t)(left), (int64_t)(right), \
                           (int32_t *)(result))
#define __builtin_mul_overflow(left, right, result) \
    amrnb_mul_overflow_i32((int64_t)(left), (int64_t)(right), \
                           (int32_t *)(result))
#endif

#endif
