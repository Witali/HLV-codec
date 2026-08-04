set(COMMON_CODEC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../codecs/common")
set(AVI_DEMUX_CURSOR_AWARE_FINISH ON CACHE BOOL
    "Avoid AVI packet completion seeks when the cursor is already sequential")
set(AVI_DEMUX_FINISH_PROFILE OFF CACHE BOOL
    "Count AVI packet completion cursor matches and fallback seeks")

idf_component_register(
    SRCS "${COMMON_CODEC_ROOT}/src/avi_demux.c"
    INCLUDE_DIRS "${COMMON_CODEC_ROOT}/include"
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_99)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -Wall -Wextra)
if(AVI_DEMUX_CURSOR_AWARE_FINISH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        AVI_DEMUX_CURSOR_AWARE_FINISH=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        AVI_DEMUX_CURSOR_AWARE_FINISH=0)
endif()
if(AVI_DEMUX_FINISH_PROFILE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        AVI_DEMUX_FINISH_PROFILE=1)
endif()
