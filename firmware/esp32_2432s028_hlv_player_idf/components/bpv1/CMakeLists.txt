set(BPV1_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../codecs/bpv")

idf_component_register(
    SRCS
        "${BPV1_ROOT}/src/bpv1_decode.c"
    INCLUDE_DIRS
        "${BPV1_ROOT}/include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Werror=return-type
)

# The repository v6 encoder emits RAW only for a novel full block. On the
# constrained player this lets the FIFO block dictionary append directly,
# avoiding two hash lookups for the common RAW mode while retaining the strict
# portable default and legacy stream support.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BPV1_FAST_CANONICAL_DICTIONARIES=1
)
