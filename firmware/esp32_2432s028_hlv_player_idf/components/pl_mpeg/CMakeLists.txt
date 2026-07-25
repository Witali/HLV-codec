idf_component_register(
    SRCS
        "../../../../codecs/mpeg1/src/pl_mpeg.c"
    INCLUDE_DIRS
        "../../../../third_party/pl_mpeg"
    REQUIRES compact_yuv420
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)
