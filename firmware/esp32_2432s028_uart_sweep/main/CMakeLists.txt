idf_component_register(
    SRCS "uart_sweep.c"
    REQUIRES esp_driver_uart esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)
