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
