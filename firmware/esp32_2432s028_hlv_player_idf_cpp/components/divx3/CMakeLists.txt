set(DIVX3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../codecs/divx3")

idf_component_register(
    SRCS
        "${DIVX3_ROOT}/src/divx3_decode.c"
        "${DIVX3_ROOT}/src/divx3_avi.c"
    INCLUDE_DIRS
        "${DIVX3_ROOT}/include"
    REQUIRES avi_demux compact_yuv420 ima_adpcm
)

set(DIVX3_IRAM_BITREADER ON CACHE BOOL
    "Place the DivX bitreader and VLC traversal helpers in IRAM")
if(DIVX3_IRAM_BITREADER)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        DIVX3_IRAM_BITREADER=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        DIVX3_IRAM_BITREADER=0
    )
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)
