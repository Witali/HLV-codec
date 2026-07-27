set(AMRNB_ROOT "../../../../codecs/amrnb")
set(PV_ROOT "${AMRNB_ROOT}/third_party/pv")

file(GLOB AMRNB_COMMON_SOURCES
    "${PV_ROOT}/common/src/*.cpp"
)
file(GLOB AMRNB_DECODER_SOURCES
    "${PV_ROOT}/dec/src/*.cpp"
)

idf_component_register(
    SRCS
        "${AMRNB_ROOT}/src/amrnb_3gp.cpp"
        ${AMRNB_COMMON_SOURCES}
        ${AMRNB_DECODER_SOURCES}
    INCLUDE_DIRS
        "${AMRNB_ROOT}/include"
        "${PV_ROOT}/common/include"
        "${PV_ROOT}/dec/src"
        "${PV_ROOT}/include"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -include
    "${AMRNB_ROOT}/include/amrnb_port.h"
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-unused-but-set-variable
)
