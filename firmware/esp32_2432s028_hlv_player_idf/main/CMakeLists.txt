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
