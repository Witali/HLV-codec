idf_component_register(
    SRCS
        "src/hlv1_common.c"
        "src/hlv1_decode.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES compact_yuv420 newlib
)

set(HLV1_OPTIMIZATION "O3" CACHE STRING
    "HLV decoder optimization level (O2, O3 or Os)")
set(HLV1_IRAM_BITREADER ON CACHE BOOL
    "Place the out-of-line ESP32 bitreader helpers in IRAM")
set(HLV1_IRAM_INVERSE_WHT ON CACHE BOOL
    "Place the inverse-WHT add kernel in IRAM")
set(HLV1_STAGE_PROFILE OFF CACHE BOOL
    "Collect ESP32 cycle counts for HLV decoder stages")
if(NOT "${HLV1_OPTIMIZATION}" MATCHES "^(O2|O3|Os)$")
    message(FATAL_ERROR "HLV1_OPTIMIZATION must be O2, O3 or Os")
endif()

if(HLV1_IRAM_BITREADER)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        HLV1_IRAM_BITREADER=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        HLV1_IRAM_BITREADER=0
    )
endif()

if(HLV1_IRAM_INVERSE_WHT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        HLV1_IRAM_INVERSE_WHT=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        HLV1_IRAM_INVERSE_WHT=0
    )
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE
    -${HLV1_OPTIMIZATION}
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)

set(HLV1_BITREADER_BITS "32" CACHE STRING
    "HLV decoder bitreader cache width (32 or 64)")
if(HLV1_BITREADER_BITS STREQUAL "32")
    set(HLV1_FAST_32BIT_BITREADER 1)
elseif(HLV1_BITREADER_BITS STREQUAL "64")
    set(HLV1_FAST_32BIT_BITREADER 0)
else()
    message(FATAL_ERROR "HLV1_BITREADER_BITS must be 32 or 64")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    HLV1_ENABLE_DECODER_STATS=0
    HLV1_FAST_32BIT_BITREADER=${HLV1_FAST_32BIT_BITREADER}
)

if(HLV1_STAGE_PROFILE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HLV1_ENABLE_STAGE_PROFILE=1
    )
else()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HLV1_ENABLE_STAGE_PROFILE=0
    )
endif()
