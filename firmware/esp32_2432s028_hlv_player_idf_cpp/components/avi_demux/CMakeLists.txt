set(COMMON_CODEC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../codecs/common")

idf_component_register(
    SRCS "${COMMON_CODEC_ROOT}/src/avi_demux.c"
    INCLUDE_DIRS "${COMMON_CODEC_ROOT}/include"
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_99)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -Wall -Wextra)
