idf_component_register(
    SRCS
        "../../../../codecs/mpeg1/src/pl_mpeg.c"
    INCLUDE_DIRS
        "../../../../third_party/pl_mpeg"
    REQUIRES compact_yuv420
)

set(PLM_MPEG_IRAM_BITREADER ON CACHE BOOL
    "Place the MPEG bitreader and VLC traversal helpers in IRAM")
set(PLM_MPEG_FAST_DCT_PREFIX ON CACHE BOOL
    "Use a six-bit lookup for frequent MPEG DCT coefficient VLCs")
if(PLM_MPEG_IRAM_BITREADER)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PLM_MPEG_IRAM_BITREADER=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PLM_MPEG_IRAM_BITREADER=0
    )
endif()

if(PLM_MPEG_FAST_DCT_PREFIX)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PLM_MPEG_FAST_DCT_PREFIX=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PLM_MPEG_FAST_DCT_PREFIX=0
    )
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)
