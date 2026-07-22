idf_component_register(
    SRCS
        "src/hlv1_common.c"
        "src/hlv1_decode.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES newlib
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)
