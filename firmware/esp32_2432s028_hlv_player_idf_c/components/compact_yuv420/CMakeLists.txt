set(COMPACT_YUV420_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../codecs/common")

idf_component_register(
    INCLUDE_DIRS
        "${COMPACT_YUV420_ROOT}/include"
)
