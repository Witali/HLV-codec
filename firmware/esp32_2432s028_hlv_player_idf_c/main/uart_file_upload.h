#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_UPLOAD_MAX_FILENAME_BYTES 48U
#define UART_SESSION_COMMAND_BYTES 16U
#define UART_UPLOAD_CHUNK_BYTES 1024U
#define UART_PATCH_CHUNK_BYTES 1024U
#define UART_UPLOAD_BUFFER_COUNT 2U
#define UART_UPLOAD_LINE_BYTES 128U

typedef struct {
    char filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U];
    uint32_t size;
    uint32_t crc32;
    uint32_t data_baud;
} uart_upload_request_t;

typedef struct {
    char filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U];
    uint32_t offset;
    uint32_t size;
    uint32_t data_baud;
} uart_read_request_t;

typedef struct {
    char filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U];
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
    uint32_t data_baud;
} uart_patch_request_t;

typedef struct {
    char filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U];
    uint32_t block_size;
} uart_block_crc_request_t;

typedef enum {
    UART_SD_BENCHMARK_ZEROS,
    UART_SD_BENCHMARK_PSEUDO_RANDOM
} uart_sd_benchmark_pattern_t;

typedef struct {
    uart_sd_benchmark_pattern_t pattern;
    uint32_t size_mib;
} uart_sd_benchmark_request_t;

typedef void (*uart_upload_progress_callback_t)(uint32_t received,
                                                uint32_t total,
                                                void *context);

typedef struct {
    uint32_t control_baud;
    char line[UART_UPLOAD_LINE_BYTES];
    size_t line_size;
    bool ready;
    char session_command[UART_SESSION_COMMAND_BYTES];
    bool session_requested;
    char active_session_command[UART_SESSION_COMMAND_BYTES];
    bool session_active;
    bool monitoring_requested;
    bool list_requested;
    char crc_filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U];
    bool crc_requested;
    uart_read_request_t read_request;
    bool read_requested;
    uart_patch_request_t patch_request;
    bool patch_requested;
    uart_block_crc_request_t block_crc_request;
    bool block_crc_requested;
    uint32_t requested_control_baud;
    bool control_baud_requested;
    char delete_filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U];
    bool delete_requested;
    uart_sd_benchmark_request_t sd_benchmark_request;
    bool sd_benchmark_requested;
} uart_file_upload_t;

esp_err_t uart_file_upload_begin(uart_file_upload_t *upload,
                                 uint32_t control_baud);
bool uart_file_upload_poll_request(uart_file_upload_t *upload,
                                   uart_upload_request_t *request);
bool uart_file_upload_take_session_request(uart_file_upload_t *upload,
                                           char *command,
                                           size_t command_bytes);
void uart_file_upload_session_ready(uart_file_upload_t *upload,
                                    const char *command);
bool uart_file_upload_take_monitoring_request(
    uart_file_upload_t *upload);
void uart_file_upload_monitoring_ready(uart_file_upload_t *upload);
bool uart_file_upload_take_list_request(uart_file_upload_t *upload);
bool uart_file_upload_take_crc_request(uart_file_upload_t *upload,
                                       char *filename,
                                       size_t filename_bytes);
bool uart_file_upload_take_read_request(uart_file_upload_t *upload,
                                        uart_read_request_t *request);
bool uart_file_upload_take_patch_request(uart_file_upload_t *upload,
                                         uart_patch_request_t *request);
bool uart_file_upload_take_block_crc_request(
    uart_file_upload_t *upload,
    uart_block_crc_request_t *request);
bool uart_file_upload_take_baud_request(uart_file_upload_t *upload,
                                        uint32_t *baud);
bool uart_file_upload_take_delete_request(uart_file_upload_t *upload,
                                          char *filename,
                                          size_t filename_bytes);
bool uart_file_upload_take_sd_benchmark_request(
    uart_file_upload_t *upload,
    uart_sd_benchmark_request_t *request);
bool uart_file_upload_list_directory(uart_file_upload_t *upload,
                                     const char *directory);
bool uart_file_upload_checksum_file(uart_file_upload_t *upload,
                                    const char *directory,
                                    const char *filename);
bool uart_file_upload_read_file(uart_file_upload_t *upload,
                                const char *directory,
                                const uart_read_request_t *request);
bool uart_file_upload_patch_file(uart_file_upload_t *upload,
                                 const char *directory,
                                 const uart_patch_request_t *request);
bool uart_file_upload_checksum_blocks(
    uart_file_upload_t *upload,
    const char *directory,
    const uart_block_crc_request_t *request);
bool uart_file_upload_change_baud(uart_file_upload_t *upload,
                                  uint32_t baud);
bool uart_file_upload_delete_file(uart_file_upload_t *upload,
                                  const char *directory,
                                  const char *filename);
bool uart_file_upload_benchmark_sd(
    uart_file_upload_t *upload,
    const char *directory,
    const uart_sd_benchmark_request_t *request);
bool uart_file_upload_receive(
    uart_file_upload_t *upload,
    const uart_upload_request_t *request,
    const char *directory,
    char *stored_path,
    size_t stored_path_bytes,
    uart_upload_progress_callback_t progress,
    void *progress_context);
void uart_file_upload_reject(uart_file_upload_t *upload,
                             const char *reason);

#ifdef __cplusplus
}
#endif
