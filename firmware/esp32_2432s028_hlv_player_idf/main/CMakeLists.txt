set(MJPEG_HOT_IRAM ON CACHE BOOL
    "Place the hottest esp_new_jpeg decode kernels in IRAM")
set(MJPEG_OPTIMIZED_IDCT ON CACHE BOOL
    "Use the bit-exact DC-only Xtensa IDCT shortcut")
set(MJPEG_IDCT_REDUCED_ROWS ON CACHE BOOL
    "Use exact one- and two-column reduced-row IDCT shortcuts")
set(MJPEG_FAST_COEFFICIENT_CLEAR ON CACHE BOOL
    "Use an unrolled aligned zero-128 memset path for JPEG coefficients")
set(mjpeg_idct_sources)
if(MJPEG_OPTIMIZED_IDCT AND MJPEG_HOT_IRAM)
    list(APPEND mjpeg_idct_sources "mjpeg_idct_optimized.S")
    if(MJPEG_IDCT_REDUCED_ROWS)
        list(APPEND mjpeg_idct_sources "mjpeg_idct_reduced.c")
    endif()
endif()
set(mjpeg_clear_sources)
if(MJPEG_FAST_COEFFICIENT_CLEAR)
    list(APPEND mjpeg_clear_sources "mjpeg_memset_optimized.S")
endif()

function(enable_mjpeg_hot_iram component_target)
    idf_component_get_property(jpeg_component espressif__esp_new_jpeg
        COMPONENT_LIB)
    idf_component_get_property(jpeg_component_dir espressif__esp_new_jpeg
        COMPONENT_DIR)
    idf_build_get_property(idf_target IDF_TARGET)
    set(mjpeg_archive
        "${jpeg_component_dir}/lib/${idf_target}/libesp_new_jpeg.a")

    set(hot_archive "${CMAKE_CURRENT_BINARY_DIR}/libesp_new_jpeg_hot.a")
    add_custom_command(
        OUTPUT "${hot_archive}"
        COMMAND "${CMAKE_OBJCOPY}"
            --rename-section
                .literal.jpeg_dec_huffman=.iram1.literal.jpeg_dec_huffman
            --rename-section
                .text.jpeg_dec_huffman=.iram1.text.jpeg_dec_huffman
            --rename-section
                .literal.jpeg_dec_proc_yuv420_0_block=.iram1.literal.jpeg_dec_proc_yuv420_0_block
            --rename-section
                .text.jpeg_dec_proc_yuv420_0_block=.iram1.text.jpeg_dec_proc_yuv420_0_block
            --rename-section
                .literal.jpeg_dec_process_0=.iram1.literal.jpeg_dec_process_0
            --rename-section
                .text.jpeg_dec_process_0=.iram1.text.jpeg_dec_process_0
            --rename-section
                .literal.yuv420_to_rgb565le=.iram1.literal.yuv420_to_rgb565le
            --rename-section
                .text.yuv420_to_rgb565le=.iram1.text.yuv420_to_rgb565le
            "${mjpeg_archive}" "${hot_archive}"
        DEPENDS "${mjpeg_archive}"
        VERBATIM
    )
    add_custom_target(mjpeg_hot_iram_archive DEPENDS "${hot_archive}")
    add_dependencies(${component_target} mjpeg_hot_iram_archive)
    set_target_properties(${jpeg_component} PROPERTIES
        INTERFACE_LINK_LIBRARIES
            "-Wl,--start-group;${hot_archive};-Wl,--end-group")
endfunction()

function(enable_mjpeg_optimized_idct_link)
    if(MJPEG_OPTIMIZED_IDCT)
        idf_build_set_property(
            LINK_OPTIONS "-Wl,--wrap=idct_block_8_8" APPEND)
        idf_build_set_property(
            LINK_OPTIONS "-Wl,-u,__wrap_idct_block_8_8" APPEND)
    endif()
endfunction()

function(enable_mjpeg_fast_clear_link)
    if(MJPEG_FAST_COEFFICIENT_CLEAR)
        idf_build_set_property(
            LINK_OPTIONS "-Wl,--wrap=memset" APPEND)
        idf_build_set_property(
            LINK_OPTIONS "-Wl,-u,__wrap_memset" APPEND)
    endif()
endfunction()

function(schedule_mjpeg_hot_iram component_target)
    if(MJPEG_HOT_IRAM)
        cmake_language(DEFER DIRECTORY "${PROJECT_DIR}"
            CALL enable_mjpeg_hot_iram "${component_target}")
    elseif(MJPEG_OPTIMIZED_IDCT)
        message(FATAL_ERROR
            "MJPEG_OPTIMIZED_IDCT requires MJPEG_HOT_IRAM")
    endif()
endfunction()

if(MJPEG_QEMU_BENCHMARK)
    set(MJPEG_QEMU_FRAME_LIMIT 12 CACHE STRING
        "Number of embedded MJPEG frames decoded by the QEMU benchmark")
    idf_component_register(
        SRCS
            "qemu_mjpeg_benchmark.c"
            "mjpeg_avi_decoder.c"
            ${mjpeg_idct_sources}
            ${mjpeg_clear_sources}
        INCLUDE_DIRS "."
        REQUIRES
            esp_rom
            esp_system
            espressif__esp_new_jpeg
            freertos
            heap
            log
            vfs
    )
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        MJPEG_QEMU_FRAME_LIMIT=${MJPEG_QEMU_FRAME_LIMIT}
        MJPEG_PHASE_TIMING=1)
    if(MJPEG_IDCT_REDUCED_ROWS)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            MJPEG_IDCT_REDUCED_ROWS=1)
    endif()
    enable_mjpeg_optimized_idct_link()
    enable_mjpeg_fast_clear_link()
    schedule_mjpeg_hot_iram(${COMPONENT_LIB})
    target_add_binary_data(
        ${COMPONENT_LIB} "qemu_mjpeg_benchmark.avi" BINARY)
elseif(BPV_QEMU_BENCHMARK)
    idf_component_register(
        SRCS
            "qemu_bpv_render_benchmark.c"
        INCLUDE_DIRS "."
        REQUIRES
            bpv1
            esp_rom
            esp_system
            freertos
            heap
            log
    )
elseif(H263_QEMU_BENCHMARK)
    idf_component_register(
        SRCS
            "qemu_h263_benchmark.c"
        INCLUDE_DIRS "."
        REQUIRES
            esp_rom
            esp_system
            freertos
            h263_3gp
            heap
            log
            vfs
    )
    target_add_binary_data(
        ${COMPONENT_LIB} "qemu_h263_benchmark.3gp" BINARY)
elseif(MPEG1_QEMU_BENCHMARK)
    idf_component_register(
        SRCS
            "qemu_mpeg1_benchmark.c"
        INCLUDE_DIRS "."
        REQUIRES
            compact_yuv420
            esp_rom
            esp_system
            freertos
            heap
            log
            pl_mpeg
    )
    target_add_binary_data(
        ${COMPONENT_LIB} "qemu_mpeg1_benchmark.mpg" BINARY)
elseif(DIVX3_QEMU_BENCHMARK)
    idf_component_register(
        SRCS
            "qemu_divx3_benchmark.c"
        INCLUDE_DIRS "."
        REQUIRES
            compact_yuv420
            divx3
            esp_rom
            esp_system
            freertos
            heap
            log
            vfs
    )
    target_add_binary_data(
        ${COMPONENT_LIB} "qemu_divx3_benchmark.avi" BINARY)
elseif(HLV_QEMU_BENCHMARK)
    idf_component_register(
        SRCS
            "qemu_benchmark.c"
            "hlv_esp32_decoder.c"
        INCLUDE_DIRS "."
        REQUIRES
            hlv1
            esp_rom
            esp_system
            freertos
            heap
            log
            vfs
    )
    target_add_binary_data(${COMPONENT_LIB} "qemu_benchmark.hlv" BINARY)
else()
    idf_component_register(
        SRCS
            "hlv_player.cpp"
            "cyd_display.c"
            "bpv_esp32_decoder.c"
            "hlv_esp32_decoder.c"
            "mjpeg_avi_decoder.c"
            "uart_file_upload.c"
            ${mjpeg_idct_sources}
            ${mjpeg_clear_sources}
        INCLUDE_DIRS "."
        REQUIRES
            amrnb_3gp
            bpv1
            divx3
            h263_3gp
            hlv1
            esp_driver_dac
            esp_driver_gpio
            esp_driver_sdspi
            esp_driver_spi
            esp_driver_uart
            esp_lcd
            esp_rom
            esp_timer
            espressif__esp_new_jpeg
            fatfs
            freertos
            heap
            log
            pl_mpeg
            sdmmc
            vfs
    )
    enable_mjpeg_optimized_idct_link()
    enable_mjpeg_fast_clear_link()
    schedule_mjpeg_hot_iram(${COMPONENT_LIB})
    if(MJPEG_IDCT_REDUCED_ROWS)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            MJPEG_IDCT_REDUCED_ROWS=1)
    endif()
endif()

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_99 cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Werror=return-type
)
