cmake_minimum_required(VERSION 3.16)

set(player_source
    "${CMAKE_CURRENT_LIST_DIR}/../esp32_2432s028_hlv_player_idf_c")
set(EXTRA_COMPONENT_DIRS
    "${player_source}/main"
    "${player_source}/components"
    "${player_source}/managed_components")
set(HLV_PLAYER_BARE_METAL_STYLE ON CACHE BOOL
    "Build the stripped cooperative player profile" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(hlv_esp32_baremetal_player)

# ESP-IDF exposes -O2 as its highest Kconfig performance level and appends
# that flag after directory-wide options. Keep framework/driver code at its
# supported performance setting, then apply -O3 as the final option to every
# first-party player and codec component in this experimental firmware.
foreach(player_component IN ITEMS
        amrnb_3gp bpv1 compact_yuv420 divx3 h263_3gp hlv1 main pl_mpeg)
    idf_component_get_property(player_component_lib
        ${player_component} COMPONENT_LIB)
    get_target_property(player_component_type
        ${player_component_lib} TYPE)
    if(NOT player_component_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_options(${player_component_lib} PRIVATE -O3)
    endif()
endforeach()

# Every project and decoder translation unit is C99. Avoid selecting the C++
# linker driver and pulling in libstdc++ implicitly.
set_target_properties(hlv_esp32_baremetal_player.elf PROPERTIES
    LINKER_LANGUAGE C)
