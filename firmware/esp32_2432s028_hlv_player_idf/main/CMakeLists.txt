set(MJPEG_HOT_IRAM ON CACHE BOOL
    "Place the hottest esp_new_jpeg decode kernels in IRAM")

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

function(schedule_mjpeg_hot_iram component_target)
    if(MJPEG_HOT_IRAM)
        cmake_language(DEFER DIRECTORY "${PROJECT_DIR}"
            CALL enable_mjpeg_hot_iram "${component_target}")
    endif()
endfunction()

if(MJPEG_QEMU_BENCHMARK)
    idf_component_register(
        SRCS
            "qemu_mjpeg_benchmark.cpp"
            "mjpeg_avi_decoder.cpp"
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
    schedule_mjpeg_hot_iram(${COMPONENT_LIB})
    target_add_binary_data(
        ${COMPONENT_LIB} "qemu_mjpeg_benchmark.avi" BINARY)
elseif(BPV_QEMU_BENCHMARK)
    idf_component_register(
        SRCS
            "qemu_bpv_render_benchmark.cpp"
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
            "qemu_h263_benchmark.cpp"
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
            "qemu_mpeg1_benchmark.cpp"
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
            "qemu_divx3_benchmark.cpp"
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
            "qemu_benchmark.cpp"
            "hlv_esp32_decoder.cpp"
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
            "cyd_display.cpp"
            "bpv_esp32_decoder.cpp"
            "hlv_esp32_decoder.cpp"
            "mjpeg_avi_decoder.cpp"
            "uart_file_upload.cpp"
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
    schedule_mjpeg_hot_iram(${COMPONENT_LIB})
endif()

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Werror=return-type
)
