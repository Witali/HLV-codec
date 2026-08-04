set(MJPEG_STREAMING_INPUT ON CACHE BOOL
    "Decode MJPEG through a bounded refill buffer in esp_new_jpeg")
set(MJPEG_INPUT_BUFFER_BYTES 8192 CACHE STRING
    "MJPEG compressed-input refill buffer size in bytes")
set(MJPEG_HOT_IRAM ON CACHE BOOL
    "Place the hottest esp_new_jpeg decode kernels in IRAM")
set(MJPEG_OPTIMIZED_IDCT ON CACHE BOOL
    "Use the bit-exact DC-only Xtensa IDCT shortcut")
set(MJPEG_IDCT_REDUCED_ROWS ON CACHE BOOL
    "Use exact one- and two-column reduced-row IDCT shortcuts")
set(MJPEG_FAST_COEFFICIENT_CLEAR ON CACHE BOOL
    "Use an unrolled aligned zero-128 memset path for JPEG coefficients")
set(MJPEG_FIXED_RGB565 ON CACHE BOOL
    "Use a fixed 16x16/stride-320 YUV420-to-RGB565LE kernel")
set(MJPEG_COLOR_TABLES ON CACHE BOOL
    "Use packed exact U/V contribution tables in the fixed RGB565 kernel")
set(MJPEG_INPUT_PREFETCH ON CACHE BOOL
    "Overlap bounded MJPEG input reads with decode on the other core")
set(MJPEG_QEMU_CALLBACK_INPUT OFF CACHE BOOL
    "Feed QEMU MJPEG packets through the generic bounded input callback")
set(HLV_PARSED_HEADER_CATCHUP ON CACHE BOOL
    "Decode a catch-up HLV keyframe after its already parsed header")
set(TONE_TEST OFF CACHE BOOL
    "Build the SD-free GPIO26 DAC sine-ramp diagnostic firmware")
set(PDM_TONE_TEST OFF CACHE BOOL
    "Build the SD-free GPIO26 I2S PDM sine-ramp diagnostic firmware")
set(COMPACT_YUV_RGB565_FAST_PATH ON CACHE BOOL
    "Fuse aligned compact Y6/U5/V5 unpacking with RGB565 conversion")
set(COMPACT_YUV_RGB565_CLAMP_TABLES ON CACHE BOOL
    "Use branch-free clamp-and-pack tables in the fused compact converter")
set(COMPACT_YUV_RGB565_HOT_IRAM ON CACHE BOOL
    "Place the fused compact YUV-to-RGB565 converter in IRAM")
set(COMPACT_YUV_Q4_LUT ON CACHE BOOL
    "Use a shared 368-byte compact YUV Q4 correction lookup table")
set(compact_yuv_rgb565_sources)
if(COMPACT_YUV_RGB565_FAST_PATH)
    list(APPEND compact_yuv_rgb565_sources
        "${CMAKE_CURRENT_LIST_DIR}/../../../codecs/common/src/y6u5v5_rgb565.c")
endif()
set(mjpeg_stream_dir
    "${CMAKE_CURRENT_LIST_DIR}/../../common/esp_new_jpeg_stream")
set(mjpeg_idct_sources)
if(MJPEG_OPTIMIZED_IDCT AND MJPEG_HOT_IRAM)
    list(APPEND mjpeg_idct_sources "mjpeg_idct_optimized.S")
    if(MJPEG_IDCT_REDUCED_ROWS)
        list(APPEND mjpeg_idct_sources "mjpeg_idct_reduced.cpp")
    endif()
endif()
set(mjpeg_clear_sources)
if(MJPEG_FAST_COEFFICIENT_CLEAR)
    list(APPEND mjpeg_clear_sources "mjpeg_memset_optimized.S")
endif()
set(mjpeg_color_sources)
if(MJPEG_FIXED_RGB565 AND MJPEG_HOT_IRAM)
    list(APPEND mjpeg_color_sources "mjpeg_color_optimized.cpp")
endif()
set(mjpeg_stream_sources)
if(MJPEG_STREAMING_INPUT)
    list(APPEND mjpeg_stream_sources
        "${mjpeg_stream_dir}/mjpeg_huffman_stream.c")
endif()

function(enable_mjpeg_hot_iram component_target)
    idf_component_get_property(jpeg_component espressif__esp_new_jpeg
        COMPONENT_LIB)
    idf_component_get_property(jpeg_component_dir espressif__esp_new_jpeg
        COMPONENT_DIR)
    idf_build_get_property(idf_target IDF_TARGET)
    set(mjpeg_archive
        "${jpeg_component_dir}/lib/${idf_target}/libesp_new_jpeg.a")
    set(mjpeg_objcopy_args
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
            .text.yuv420_to_rgb565le=.iram1.text.yuv420_to_rgb565le)
    set(hot_archive "${CMAKE_CURRENT_BINARY_DIR}/libesp_new_jpeg_hot.a")
    if(MJPEG_FIXED_RGB565)
        set(color_patch_dir
            "${CMAKE_CURRENT_BINARY_DIR}/esp_new_jpeg_color_patch")
        file(MAKE_DIRECTORY "${color_patch_dir}")
        add_custom_command(
            OUTPUT "${hot_archive}"
            COMMAND "${CMAKE_OBJCOPY}"
                ${mjpeg_objcopy_args}
                "${mjpeg_archive}" "${hot_archive}"
            COMMAND "${CMAKE_AR}" x
                "${hot_archive}" jpeg_dec_color.c.obj
            COMMAND "${CMAKE_OBJCOPY}"
                --redefine-sym
                    color_array=mjpeg_original_color_array
                jpeg_dec_color.c.obj
            COMMAND "${CMAKE_AR}" r
                "${hot_archive}" jpeg_dec_color.c.obj
            WORKING_DIRECTORY "${color_patch_dir}"
            DEPENDS "${mjpeg_archive}"
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT "${hot_archive}"
            COMMAND "${CMAKE_OBJCOPY}"
                ${mjpeg_objcopy_args}
                "${mjpeg_archive}" "${hot_archive}"
            DEPENDS "${mjpeg_archive}"
            VERBATIM
        )
    endif()
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

function(enable_mjpeg_streaming_huffman_link)
    if(MJPEG_STREAMING_INPUT)
        idf_build_set_property(
            LINK_OPTIONS "-Wl,--wrap=jpeg_dec_huffman" APPEND)
        idf_build_set_property(
            LINK_OPTIONS "-Wl,-u,__wrap_jpeg_dec_huffman" APPEND)
    endif()
endfunction()

function(schedule_mjpeg_hot_iram component_target)
    if(MJPEG_HOT_IRAM)
        cmake_language(DEFER DIRECTORY "${PROJECT_DIR}"
            CALL enable_mjpeg_hot_iram "${component_target}")
    elseif(MJPEG_OPTIMIZED_IDCT)
        message(FATAL_ERROR
            "MJPEG_OPTIMIZED_IDCT requires MJPEG_HOT_IRAM")
    elseif(MJPEG_FIXED_RGB565)
        message(FATAL_ERROR
            "MJPEG_FIXED_RGB565 requires MJPEG_HOT_IRAM")
    endif()
endfunction()

if(PDM_TONE_TEST)
    idf_component_register(
        SRCS
            "${CMAKE_CURRENT_LIST_DIR}/../../common/esp32_pdm_tone_test.c"
        INCLUDE_DIRS "."
        REQUIRES
            esp_driver_gpio
            esp_driver_i2s
            esp_rom
            freertos
    )
elseif(TONE_TEST)
    idf_component_register(
        SRCS
            "${CMAKE_CURRENT_LIST_DIR}/../../common/esp32_tone_test.c"
        INCLUDE_DIRS "."
        REQUIRES
            esp_driver_dac
            esp_rom
            freertos
    )
elseif(MJPEG_QEMU_BENCHMARK)
    set(MJPEG_QEMU_FRAME_LIMIT 12 CACHE STRING
        "Number of embedded MJPEG frames decoded by the QEMU benchmark")
    idf_component_register(
        SRCS
            "qemu_mjpeg_benchmark.cpp"
            "mjpeg_avi_decoder.cpp"
            ${mjpeg_idct_sources}
            ${mjpeg_clear_sources}
            ${mjpeg_color_sources}
            ${mjpeg_stream_sources}
        INCLUDE_DIRS "." "${mjpeg_stream_dir}"
        REQUIRES
            esp_rom
            esp_system
            espressif__esp_new_jpeg
            freertos
            heap
            avi_demux
            ima_adpcm
            log
            vfs
    )
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        MJPEG_QEMU_FRAME_LIMIT=${MJPEG_QEMU_FRAME_LIMIT}
        MJPEG_PHASE_TIMING=1)
    if(MJPEG_QEMU_CALLBACK_INPUT)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            MJPEG_QEMU_CALLBACK_INPUT=1)
    endif()
    if(MJPEG_STREAMING_INPUT)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            MJPEG_STREAMING_INPUT=1
            MJPEG_INPUT_BUFFER_BYTES=${MJPEG_INPUT_BUFFER_BYTES})
        enable_mjpeg_streaming_huffman_link()
    endif()
    if(MJPEG_IDCT_REDUCED_ROWS)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            MJPEG_IDCT_REDUCED_ROWS=1)
    endif()
    if(MJPEG_FIXED_RGB565)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            MJPEG_FIXED_RGB565=1)
        if(MJPEG_COLOR_TABLES)
            target_compile_definitions(${COMPONENT_LIB} PRIVATE
                MJPEG_COLOR_TABLES=1)
        endif()
    endif()
    enable_mjpeg_optimized_idct_link()
    enable_mjpeg_fast_clear_link()
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
elseif(MPEG4_QEMU_BENCHMARK)
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
    target_compile_definitions(
        ${COMPONENT_LIB} PRIVATE MPEG4_QEMU_BENCHMARK=1)
    target_add_binary_data(
        ${COMPONENT_LIB} "qemu_mpeg4_benchmark.avi" BINARY)
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
        ${COMPONENT_LIB} "qemu_h263_benchmark.avi" BINARY)
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
            "boot_button.c"
            "cyd_display.cpp"
            "bpv_esp32_decoder.cpp"
            "hlv_esp32_decoder.cpp"
            "mjpeg_avi_decoder.cpp"
            "uart_file_upload.cpp"
            ${mjpeg_idct_sources}
            ${mjpeg_clear_sources}
            ${mjpeg_color_sources}
            ${mjpeg_stream_sources}
            ${compact_yuv_rgb565_sources}
        INCLUDE_DIRS "." "${mjpeg_stream_dir}"
        REQUIRES
            amrnb_3gp
            bpv1
            compact_yuv420
            divx3
            h263_3gp
            hlv1
            avi_demux
            ima_adpcm
            esp_driver_i2s
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
    enable_mjpeg_streaming_huffman_link()
    schedule_mjpeg_hot_iram(${COMPONENT_LIB})
    if(MJPEG_STREAMING_INPUT)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            MJPEG_STREAMING_INPUT=1
            MJPEG_INPUT_BUFFER_BYTES=${MJPEG_INPUT_BUFFER_BYTES})
    endif()
    if(MJPEG_IDCT_REDUCED_ROWS)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            MJPEG_IDCT_REDUCED_ROWS=1)
    endif()
    if(MJPEG_FIXED_RGB565)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            MJPEG_FIXED_RGB565=1)
        if(MJPEG_COLOR_TABLES)
            target_compile_definitions(${COMPONENT_LIB} PRIVATE
                MJPEG_COLOR_TABLES=1)
        endif()
    endif()
endif()

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
if(HLV_PARSED_HEADER_CATCHUP)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        HLV_PARSED_HEADER_CATCHUP=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        HLV_PARSED_HEADER_CATCHUP=0)
endif()
if(HLV_PLAYER_BARE_METAL_STYLE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        HLV_PLAYER_BARE_METAL_STYLE=1)
endif()
if(MJPEG_INPUT_PREFETCH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        MJPEG_INPUT_PREFETCH=1)
endif()
if(COMPACT_YUV_RGB565_FAST_PATH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        COMPACT_YUV_RGB565_FAST_PATH=1)
    if(COMPACT_YUV_RGB565_CLAMP_TABLES)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            COMPACT_YUV_RGB565_CLAMP_TABLES=1)
    endif()
    if(COMPACT_YUV_RGB565_HOT_IRAM)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            COMPACT_YUV_RGB565_HOT_IRAM=1)
    endif()
    if(COMPACT_YUV_Q4_LUT)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            COMPACT_YUV_Q4_LUT=1)
    endif()
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Werror=return-type
)
