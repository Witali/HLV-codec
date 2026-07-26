#include "uart_file_upload.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UPLOAD_UART UART_NUM_0
#define UART_RX_BUFFER_BYTES 2048
#define CHUNK_TIMEOUT_MS 10000U
#define TRANSFER_BAUD_460K 460800U
#define TRANSFER_BAUD_921K 921600U
#define TRANSFER_BAUD_1500K 1500000U
#define TRANSFER_BAUD_2000K 2000000U
#define BLOCK_HEADER_BYTES 14U
#define CRC_BUFFER_BYTES 4096U

static const uint8_t k_block_magic[] = {'H', 'L', 'V', 'B'};

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint32_t crc32(const uint8_t *bytes, size_t count) {
    return esp_rom_crc32_le(0, bytes, count);
}

static bool ends_with_ignore_case(const char *value,
                                  size_t value_length,
                                  const char *suffix) {
    size_t suffix_length = strlen(suffix);
    size_t index;

    if (suffix_length > value_length) {
        return false;
    }
    value += value_length - suffix_length;
    for (index = 0; index < suffix_length; ++index) {
        char left = value[index];
        char right = suffix[index];
        if (left >= 'A' && left <= 'Z') {
            left = (char)(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = (char)(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

static bool valid_filename(const char *filename) {
    size_t length;
    size_t i;

    if (filename == NULL) {
        return false;
    }
    length = strlen(filename);
    if (length == 0U || length > UART_UPLOAD_MAX_FILENAME_BYTES ||
        filename[0] == '.') {
        return false;
    }
    for (i = 0; i < length; ++i) {
        char character = filename[i];
        bool valid =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-';
        if (!valid) {
            return false;
        }
    }
    return ends_with_ignore_case(filename, length, ".hlv") ||
           ends_with_ignore_case(filename, length, ".avi") ||
           ends_with_ignore_case(filename, length, ".3gp") ||
           ends_with_ignore_case(filename, length, ".bpv1") ||
           ends_with_ignore_case(filename, length, ".mpg") ||
           ends_with_ignore_case(filename, length, ".mpeg") ||
           ends_with_ignore_case(filename, length, ".txt");
}

static bool supported_data_baud(uint32_t baud) {
    return baud == TRANSFER_BAUD_460K ||
           baud == TRANSFER_BAUD_921K ||
           baud == TRANSFER_BAUD_1500K ||
           baud == TRANSFER_BAUD_2000K;
}

static bool build_path(char *destination,
                       size_t destination_bytes,
                       const char *directory,
                       const char *filename,
                       const char *suffix) {
    int result;

    if (destination == NULL || destination_bytes == 0U ||
        directory == NULL || filename == NULL || suffix == NULL) {
        return false;
    }
    result = snprintf(destination, destination_bytes, "%s/%s%s",
                      directory, filename, suffix);
    return result >= 0 && (size_t)result < destination_bytes;
}

static bool set_baud(uint32_t baud) {
    return uart_set_baudrate(UPLOAD_UART, baud) == ESP_OK;
}

static bool read_exact(uint8_t *destination,
                       size_t bytes,
                       uint32_t timeout_ms) {
    int64_t deadline =
        esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    size_t received = 0;

    while (received < bytes && esp_timer_get_time() < deadline) {
        int count = uart_read_bytes(
            UPLOAD_UART, destination + received, bytes - received,
            pdMS_TO_TICKS(100));
        if (count > 0) {
            received += (size_t)count;
        }
    }
    return received == bytes;
}

static void write_response_v(const char *format, va_list arguments) {
    char response[384];
    int length = vsnprintf(response, sizeof response, format, arguments);

    if (length > 0) {
        size_t bytes =
            (size_t)length < sizeof response
                ? (size_t)length
                : sizeof response - 1U;
        uart_write_bytes(UPLOAD_UART, response, bytes);
    }
}

static void write_response(const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    write_response_v(format, arguments);
    va_end(arguments);
}

static void finish_response(uart_file_upload_t *upload,
                            const char *format,
                            ...) {
    va_list arguments;

    va_start(arguments, format);
    write_response_v(format, arguments);
    va_end(arguments);
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    if (upload != NULL) {
        set_baud(upload->control_baud);
    }
}

void uart_file_upload_reject(uart_file_upload_t *upload,
                             const char *reason) {
    (void)upload;
    write_response("HLVERR 1 %s\n", reason != NULL ? reason : "FAILED");
}

esp_err_t uart_file_upload_begin(uart_file_upload_t *upload,
                                 uint32_t control_baud) {
    uart_config_t config = {0};
    esp_err_t result;

    if (upload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    upload->control_baud = control_baud;
    config.baud_rate = (int)control_baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    result = uart_param_config(UPLOAD_UART, &config);
    if (result != ESP_OK) {
        return result;
    }
    result = uart_set_pin(UPLOAD_UART, UART_PIN_NO_CHANGE,
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                          UART_PIN_NO_CHANGE);
    if (result != ESP_OK) {
        return result;
    }
    if (!uart_is_driver_installed(UPLOAD_UART)) {
        result = uart_driver_install(UPLOAD_UART, UART_RX_BUFFER_BYTES,
                                     0, 0, NULL, 0);
        if (result != ESP_OK) {
            return result;
        }
    }
    uart_flush_input(UPLOAD_UART);
    upload->ready = true;
    write_response("HLVUART 1 READY %u\n",
                   (unsigned)upload->control_baud);
    return uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
}

static bool parse_request(uart_file_upload_t *upload,
                          const char *line,
                          uart_upload_request_t *request) {
    if (strcmp(line, "HLVLIST 1") == 0) {
        upload->list_requested = true;
        return false;
    }
    if (strncmp(line, "HLVCRC ", 7) == 0) {
        char filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U] = {0};
        char trailing = '\0';
        int fields =
            sscanf(line, "HLVCRC 1 %48s %c", filename, &trailing);
        if (fields != 1 || !valid_filename(filename)) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        snprintf(upload->crc_filename, sizeof upload->crc_filename,
                 "%s", filename);
        upload->crc_requested = true;
        return false;
    }
    if (strncmp(line, "HLVPUT ", 7) != 0) {
        return false;
    }

    {
        uart_upload_request_t parsed = {0};
        unsigned long size = 0;
        unsigned crc = 0;
        unsigned baud = 0;
        char trailing = '\0';
        int fields = sscanf(
            line, "HLVPUT 1 %48s %lu %x %u %c", parsed.filename,
            &size, &crc, &baud, &trailing);
        if (fields != 4 || size == 0U || size > UINT32_MAX ||
            !valid_filename(parsed.filename) ||
            !supported_data_baud(baud)) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        parsed.size = (uint32_t)size;
        parsed.crc32 = (uint32_t)crc;
        parsed.data_baud = baud;
        *request = parsed;
    }
    return true;
}

bool uart_file_upload_poll_request(uart_file_upload_t *upload,
                                   uart_upload_request_t *request) {
    uint8_t bytes[64];
    int count;
    int i;

    if (upload == NULL || !upload->ready || request == NULL) {
        return false;
    }
    count = uart_read_bytes(UPLOAD_UART, bytes, sizeof bytes, 0);
    if (count <= 0) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        uint8_t byte = bytes[i];
        if (byte == '\r') {
            continue;
        }
        if (byte == '\n') {
            bool parsed;
            upload->line[upload->line_size] = '\0';
            parsed = parse_request(upload, upload->line, request);
            upload->line_size = 0;
            if (parsed) {
                return true;
            }
            continue;
        }
        if (byte < 0x20U || byte > 0x7eU) {
            upload->line_size = 0;
            continue;
        }
        if (upload->line_size + 1U < sizeof upload->line) {
            upload->line[upload->line_size++] = (char)byte;
        } else {
            upload->line_size = 0;
        }
    }
    return false;
}

bool uart_file_upload_take_list_request(uart_file_upload_t *upload) {
    bool requested;

    if (upload == NULL) {
        return false;
    }
    requested = upload->list_requested;
    upload->list_requested = false;
    return requested;
}

bool uart_file_upload_take_crc_request(uart_file_upload_t *upload,
                                       char *filename,
                                       size_t filename_bytes) {
    if (upload == NULL || !upload->crc_requested ||
        filename == NULL || filename_bytes == 0U) {
        return false;
    }
    snprintf(filename, filename_bytes, "%s", upload->crc_filename);
    upload->crc_requested = false;
    upload->crc_filename[0] = '\0';
    return true;
}

bool uart_file_upload_list_directory(uart_file_upload_t *upload,
                                     const char *directory) {
    DIR *handle;
    uint32_t count = 0;
    const struct dirent *entry;

    if (directory == NULL) {
        uart_file_upload_reject(upload, "BAD_DIRECTORY");
        return false;
    }
    handle = opendir(directory);
    if (handle == NULL) {
        uart_file_upload_reject(upload, "LIST_FAILED");
        return false;
    }

    write_response("HLVLISTBEGIN 1\n");
    while ((entry = readdir(handle)) != NULL) {
        char path[384];
        struct stat status = {0};
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!build_path(path, sizeof path, directory,
                        entry->d_name, "")) {
            continue;
        }
        if (stat(path, &status) != 0 || !S_ISREG(status.st_mode)) {
            continue;
        }
        write_response("HLVFILE 1 %u %s\n",
                       (unsigned)status.st_size, entry->d_name);
        ++count;
    }
    closedir(handle);
    finish_response(upload, "HLVLISTEND 1 %u\n", (unsigned)count);
    return true;
}

bool uart_file_upload_checksum_file(uart_file_upload_t *upload,
                                    const char *directory,
                                    const char *filename) {
    char path[128];
    FILE *input;
    uint8_t *buffer;
    uint32_t file_crc = 0;
    uint32_t file_size = 0;
    bool success = true;

    if (!build_path(path, sizeof path, directory, filename, "")) {
        uart_file_upload_reject(upload, "BAD_PATH");
        return false;
    }
    input = fopen(path, "rb");
    if (input == NULL) {
        uart_file_upload_reject(upload, "OPEN_FAILED");
        return false;
    }
    buffer = (uint8_t *)heap_caps_malloc(CRC_BUFFER_BYTES,
                                         MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        fclose(input);
        uart_file_upload_reject(upload, "NO_MEMORY");
        return false;
    }

    for (;;) {
        size_t received = fread(buffer, 1, CRC_BUFFER_BYTES, input);
        if (received != 0U) {
            file_crc = esp_rom_crc32_le(file_crc, buffer, received);
            file_size += (uint32_t)received;
        }
        if (received != CRC_BUFFER_BYTES) {
            success = feof(input) != 0 && ferror(input) == 0;
            break;
        }
    }
    if (fclose(input) != 0) {
        success = false;
    }
    heap_caps_free(buffer);
    if (!success) {
        finish_response(upload, "HLVERR 1 READ_FAILED\n");
        return false;
    }
    finish_response(upload, "HLVCRC 1 %u %08x %s\n",
                    (unsigned)file_size, (unsigned)file_crc, filename);
    return true;
}

bool uart_file_upload_receive(
    uart_file_upload_t *upload,
    const uart_upload_request_t *request,
    const char *directory,
    char *stored_path,
    size_t stored_path_bytes,
    uart_upload_progress_callback_t progress,
    void *progress_context) {
    char target[128];
    char temporary[128];
    char backup[128];
    uint8_t *buffer;
    FILE *output;
    uint32_t received = 0;
    uint32_t sequence = 0;
    uint32_t file_crc = 0;
    bool success = true;
    const char *failure = "TRANSFER_FAILED";

    if (request == NULL ||
        !build_path(target, sizeof target, directory,
                    request->filename, "") ||
        !build_path(temporary, sizeof temporary, directory,
                    request->filename, ".part") ||
        !build_path(backup, sizeof backup, directory,
                    request->filename, ".bak")) {
        uart_file_upload_reject(upload, "PATH_TOO_LONG");
        return false;
    }

    buffer = (uint8_t *)heap_caps_malloc(UART_UPLOAD_CHUNK_BYTES,
                                         MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        uart_file_upload_reject(upload, "NO_MEMORY");
        return false;
    }
    unlink(temporary);
    output = fopen(temporary, "wb");
    if (output == NULL) {
        heap_caps_free(buffer);
        uart_file_upload_reject(upload, "OPEN_FAILED");
        return false;
    }
    setvbuf(output, NULL, _IONBF, 0);

    uart_flush_input(UPLOAD_UART);
    write_response("HLVREADY 1 %u %u\n",
                   (unsigned)UART_UPLOAD_CHUNK_BYTES,
                   (unsigned)request->data_baud);
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!set_baud(request->data_baud)) {
        fclose(output);
        unlink(temporary);
        heap_caps_free(buffer);
        uart_file_upload_reject(upload, "BAUD_FAILED");
        return false;
    }

    while (received < request->size) {
        uint8_t header[BLOCK_HEADER_BYTES];
        uint32_t block_sequence;
        uint16_t block_bytes;
        uint32_t block_crc;
        uint32_t remaining;
        if (!read_exact(header, sizeof header, CHUNK_TIMEOUT_MS)) {
            failure = "TIMEOUT";
            success = false;
            break;
        }
        block_sequence = read_le32(header + 4);
        block_bytes = read_le16(header + 8);
        block_crc = read_le32(header + 10);
        remaining = request->size - received;
        if (memcmp(header, k_block_magic, sizeof k_block_magic) != 0 ||
            block_sequence != sequence || block_bytes == 0U ||
            block_bytes > UART_UPLOAD_CHUNK_BYTES ||
            block_bytes > remaining) {
            failure = "BAD_BLOCK";
            success = false;
            break;
        }
        if (!read_exact(buffer, block_bytes, CHUNK_TIMEOUT_MS)) {
            failure = "TIMEOUT";
            success = false;
            break;
        }
        if (crc32(buffer, block_bytes) != block_crc) {
            write_response("HLVNAK %u CRC\n", (unsigned)sequence);
            continue;
        }
        if (fwrite(buffer, 1, block_bytes, output) != block_bytes) {
            failure = "WRITE_FAILED";
            success = false;
            break;
        }
        file_crc = esp_rom_crc32_le(file_crc, buffer, block_bytes);
        received += block_bytes;
        if (progress != NULL) {
            progress(received, request->size, progress_context);
        }
        write_response("HLVACK %u %u\n", (unsigned)sequence,
                       (unsigned)received);
        ++sequence;
    }

    if (success && (fflush(output) != 0 || fsync(fileno(output)) != 0)) {
        failure = "FLUSH_FAILED";
        success = false;
    }
    if (fclose(output) != 0) {
        failure = "CLOSE_FAILED";
        success = false;
    }
    heap_caps_free(buffer);

    if (success && file_crc != request->crc32) {
        failure = "FILE_CRC";
        success = false;
    }
    if (!success) {
        unlink(temporary);
        finish_response(upload, "HLVERR 1 %s\n", failure);
        return false;
    }

    {
        struct stat target_status = {0};
        bool had_target = stat(target, &target_status) == 0;
        unlink(backup);
        if (had_target && rename(target, backup) != 0) {
            unlink(temporary);
            finish_response(upload, "HLVERR 1 BACKUP_FAILED\n");
            return false;
        }
        if (rename(temporary, target) != 0) {
            if (had_target) {
                rename(backup, target);
            }
            unlink(temporary);
            finish_response(upload, "HLVERR 1 COMMIT_FAILED\n");
            return false;
        }
        if (had_target) {
            unlink(backup);
        }
    }

    if (stored_path != NULL && stored_path_bytes != 0U) {
        snprintf(stored_path, stored_path_bytes, "%s", target);
    }
    finish_response(upload, "HLVDONE 1 %u %08x %s\n",
                    (unsigned)request->size, (unsigned)file_crc,
                    request->filename);
    return true;
}
