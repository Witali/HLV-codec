cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(hlv_uart_sweep)
set_target_properties(hlv_uart_sweep.elf PROPERTIES LINKER_LANGUAGE C)
