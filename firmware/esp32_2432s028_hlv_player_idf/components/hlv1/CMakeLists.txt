idf_component_register(
    SRCS
        "src/hlv1_common.c"
        "src/hlv1_decode.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES newlib
)

set(HLV1_OPTIMIZATION "O3" CACHE STRING
    "HLV decoder optimization level (O2, O3 or Os)")
if(NOT "${HLV1_OPTIMIZATION}" MATCHES "^(O2|O3|Os)$")
    message(FATAL_ERROR "HLV1_OPTIMIZATION must be O2, O3 or Os")
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
