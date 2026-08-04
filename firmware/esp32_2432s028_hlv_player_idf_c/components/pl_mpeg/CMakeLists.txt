idf_component_register(
    SRCS
        "../../../../codecs/mpeg1/src/pl_mpeg.c"
    INCLUDE_DIRS
        "../../../../third_party/pl_mpeg"
    REQUIRES compact_yuv420
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    PLM_AUDIO_MONO_S16=1
)

set(PLM_MPEG_IRAM_BITREADER ON CACHE BOOL
    "Place the MPEG bitreader and VLC traversal helpers in IRAM")
set(PLM_MPEG_FAST_DCT_PREFIX ON CACHE BOOL
    "Use a six-bit lookup for frequent MPEG DCT coefficient VLCs")
set(PLM_MPEG_DECODE_PROFILE OFF CACHE BOOL
    "Collect MPEG-1 decoder phase cycle counters")
set(PLM_MPEG_IRAM_COMPACT_MC ON CACHE BOOL
    "Place compact MPEG-1 motion-compensation kernels in IRAM")
set(PLM_MPEG_DCT_SECOND_LEVEL ON CACHE BOOL
    "Use compact second-level MPEG-1 DCT coefficient VLC tables")
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

if(PLM_MPEG_DECODE_PROFILE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        PLM_MPEG_DECODE_PROFILE=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        PLM_MPEG_DECODE_PROFILE=0
    )
endif()

if(PLM_MPEG_IRAM_COMPACT_MC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PLM_MPEG_IRAM_COMPACT_MC=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PLM_MPEG_IRAM_COMPACT_MC=0
    )
endif()

if(PLM_MPEG_DCT_SECOND_LEVEL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PLM_MPEG_DCT_SECOND_LEVEL=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PLM_MPEG_DCT_SECOND_LEVEL=0
    )
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)
