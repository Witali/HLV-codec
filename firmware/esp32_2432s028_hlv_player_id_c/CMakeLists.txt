cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Do not discover and build every ESP-IDF component. Only main, the vendored
# decoder, and their transitive hardware/runtime dependencies are included.
idf_build_set_property(MINIMAL_BUILD ON)
project(hlv_esp32_player)

# ESP-IDF declares both C and C++ project languages and otherwise lets CMake
# choose the C++ linker driver for the application ELF. All project and codec
# translation units are C, so use the C linker driver and avoid its implicit
# libstdc++ dependency.
set_target_properties(hlv_esp32_player.elf PROPERTIES LINKER_LANGUAGE C)
