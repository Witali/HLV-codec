cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Do not discover and build every ESP-IDF component. Only main, the vendored
# decoder, and their transitive hardware/runtime dependencies are included.
idf_build_set_property(MINIMAL_BUILD ON)
project(hlv_esp32_player)
