idf_component_register(
    SRCS
        "hlv_player.cpp"
        "cyd_display.cpp"
        "hlv_esp32_decoder.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        hlv1
        esp_driver_dac
        esp_driver_gpio
        esp_driver_sdspi
        esp_driver_spi
        esp_lcd
        esp_rom
        esp_timer
        fatfs
        freertos
        heap
        log
        sdmmc
        vfs
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Werror=return-type
)
