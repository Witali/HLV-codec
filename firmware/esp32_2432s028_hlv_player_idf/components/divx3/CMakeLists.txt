set(DIVX3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../codecs/divx3")

idf_component_register(
    SRCS
        "${DIVX3_ROOT}/src/divx3_decode.c"
        "${DIVX3_ROOT}/src/divx3_avi.c"
    INCLUDE_DIRS
        "${DIVX3_ROOT}/include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)
