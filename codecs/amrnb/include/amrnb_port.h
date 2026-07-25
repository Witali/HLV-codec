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
#include <cstdint>
#include <limits>
#include <type_traits>

template <typename Left, typename Right, typename Result>
static inline bool amrnb_add_overflow(Left left, Right right,
                                      Result *result) {
    static_assert(std::is_signed<Result>::value &&
                      sizeof(Result) <= sizeof(int32_t),
                  "AMR-NB arithmetic expects signed 16/32-bit values");
    const int64_t wide =
        static_cast<int64_t>(left) + static_cast<int64_t>(right);
    *result = static_cast<Result>(
        static_cast<typename std::make_unsigned<Result>::type>(wide));
    return wide <
               static_cast<int64_t>(std::numeric_limits<Result>::min()) ||
           wide >
               static_cast<int64_t>(std::numeric_limits<Result>::max());
}

template <typename Left, typename Right, typename Result>
static inline bool amrnb_sub_overflow(Left left, Right right,
                                      Result *result) {
    static_assert(std::is_signed<Result>::value &&
                      sizeof(Result) <= sizeof(int32_t),
                  "AMR-NB arithmetic expects signed 16/32-bit values");
    const int64_t wide =
        static_cast<int64_t>(left) - static_cast<int64_t>(right);
    *result = static_cast<Result>(
        static_cast<typename std::make_unsigned<Result>::type>(wide));
    return wide <
               static_cast<int64_t>(std::numeric_limits<Result>::min()) ||
           wide >
               static_cast<int64_t>(std::numeric_limits<Result>::max());
}

template <typename Left, typename Right, typename Result>
static inline bool amrnb_mul_overflow(Left left, Right right,
                                      Result *result) {
    static_assert(std::is_signed<Result>::value &&
                      sizeof(Result) <= sizeof(int32_t),
                  "AMR-NB arithmetic expects signed 16/32-bit values");
    const int64_t wide =
        static_cast<int64_t>(left) * static_cast<int64_t>(right);
    *result = static_cast<Result>(
        static_cast<typename std::make_unsigned<Result>::type>(wide));
    return wide <
               static_cast<int64_t>(std::numeric_limits<Result>::min()) ||
           wide >
               static_cast<int64_t>(std::numeric_limits<Result>::max());
}

#define __builtin_add_overflow(left, right, result) \
    amrnb_add_overflow((left), (right), (result))
#define __builtin_sub_overflow(left, right, result) \
    amrnb_sub_overflow((left), (right), (result))
#define __builtin_mul_overflow(left, right, result) \
    amrnb_mul_overflow((left), (right), (result))
#endif

#endif
