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
#include "freertos/queue.h"
#include "freertos/task.h"

#define UPLOAD_UART UART_NUM_0
#define UART_RX_BUFFER_BYTES 12288
#define CHUNK_TIMEOUT_MS 10000U
#define TRANSFER_BAUD_460K 460800U
#define TRANSFER_BAUD_921K 921600U
#define TRANSFER_BAUD_1000K 1000000U
#define CALIBRATED_BAUD_1000K 978593U
#define TRANSFER_BAUD_1500K 1500000U
#define TRANSFER_BAUD_2000K 2000000U
#define TRANSFER_BAUD_3000K 3000000U
#define BLOCK_HEADER_BYTES 14U
#define READ_BLOCK_BYTES 64U
#define READ_BLOCK_HEADER_BYTES 14U
#define READ_ACK_BYTES 13U
#define READ_READY_BYTES 24U
#define READ_DONE_BYTES 16U
#define READ_BLOCK_ATTEMPTS 5U
#define READ_DATA_ATTEMPTS 20U
#define READ_ACK_TIMEOUT_MS 500U
#define LIST_PACKET_HEADER_BYTES 17U
#define LIST_PACKET_ATTEMPTS 20U
#define LIST_ACK_TIMEOUT_MS 500U
#define BLOCK_CRC_PACKET_BYTES 24U
#define BLOCK_CRC_PACKET_ATTEMPTS 20U
#define BLOCK_CRC_ACK_TIMEOUT_MS 500U
#define MINIMUM_BLOCK_CRC_BYTES 4096U
#define MAXIMUM_BLOCK_CRC_BYTES (1024U * 1024U)
#define CRC_BUFFER_BYTES 4096U
#define WRITER_STACK_BYTES 4096U
#define WRITER_PRIORITY (tskIDLE_PRIORITY + 2U)
#define SD_BENCHMARK_BLOCK_BYTES (32U * 1024U)
#define MAXIMUM_SD_BENCHMARK_MIB 64U
#define SD_BENCHMARK_FILENAME ".hlv-sd-benchmark.tmp"
#define CRC_INDEX_FILENAME "crc32.txt"
#define CRC_INDEX_LINE_BYTES 128U

static const uint8_t k_block_magic[] = {'H', 'L', 'V', 'B'};
static const uint8_t k_patch_block_magic[] = {'H', 'L', 'V', 'P'};
static const uint8_t k_read_block_magic[] = {'H', 'L', 'V', 'X'};
static const uint8_t k_list_packet_magic[] = {'H', 'L', 'V', 'L'};
static const uint8_t k_block_crc_packet_magic[] = {'H', 'L', 'V', 'K'};

typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t sequence;
    uint32_t end_offset;
} upload_block_t;

typedef struct {
    FILE *output;
    QueueHandle_t ready;
    QueueHandle_t completed;
    uint32_t file_crc;
    uint32_t written;
    bool failed;
} upload_writer_t;

static void upload_writer_task(void *opaque) {
    upload_writer_t *writer = (upload_writer_t *)(opaque);
    for (;;) {
        upload_block_t *block = NULL;
        if (xQueueReceive(
                writer->ready, &block, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (block == NULL) {
            xQueueSend(writer->completed, &block, portMAX_DELAY);
            vTaskDelete(NULL);
            return;
        }
        if (!writer->failed) {
            if (fwrite(
                    block->data, 1, block->size,
                    writer->output) != block->size) {
                writer->failed = true;
            } else {
                writer->file_crc = esp_rom_crc32_le(
                    writer->file_crc, block->data, block->size);
                writer->written += (uint32_t)(block->size);
            }
        }
        xQueueSend(writer->completed, &block, portMAX_DELAY);
    }
}

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

static bool valid_delete_filename(const char *filename) {
    char destination[UART_UPLOAD_MAX_FILENAME_BYTES + 1U];
    size_t length;
    size_t destination_length;

    if (valid_filename(filename)) {
        return true;
    }
    if (filename == NULL) {
        return false;
    }
    length = strlen(filename);
    if (length <= 5U ||
        !ends_with_ignore_case(filename, length, ".part")) {
        return false;
    }
    destination_length = length - 5U;
    memcpy(destination, filename, destination_length);
    destination[destination_length] = '\0';
    return valid_filename(destination);
}

static bool supported_data_baud(uint32_t baud) {
    return baud == TRANSFER_BAUD_460K ||
           baud == TRANSFER_BAUD_921K ||
           baud == TRANSFER_BAUD_1000K ||
           baud == TRANSFER_BAUD_1500K ||
           baud == TRANSFER_BAUD_2000K ||
           baud == TRANSFER_BAUD_3000K;
}

static bool require_session(uart_file_upload_t *upload,
                            const char *command) {
    if (!upload->session_active) {
        uart_file_upload_reject(upload, "SESSION_REQUIRED");
        return false;
    }
    if (strcmp(upload->active_session_command, command) != 0) {
        uart_file_upload_reject(upload, "SESSION_MISMATCH");
        return false;
    }
    return true;
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

static bool find_cached_crc(const char *directory,
                            const char *filename,
                            uint32_t file_size,
                            uint32_t *file_crc) {
    char path[128];
    char line[CRC_INDEX_LINE_BYTES];
    FILE *index;
    bool found = false;

    if (file_crc == NULL ||
        !build_path(path, sizeof path, directory, CRC_INDEX_FILENAME, "")) {
        return false;
    }
    index = fopen(path, "rb");
    if (index == NULL) {
        return false;
    }
    while (fgets(line, sizeof line, index) != NULL) {
        unsigned cached_crc;
        unsigned long cached_size;
        char cached_filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U] = {0};
        int fields = sscanf(line, "%8x,%lu,%48[^\r\n]",
                            &cached_crc, &cached_size, cached_filename);
        if (fields == 3 && cached_size == (unsigned long)file_size &&
            strcmp(cached_filename, filename) == 0) {
            *file_crc = (uint32_t)cached_crc;
            found = true;
        }
    }
    fclose(index);
    return found;
}

static bool rewrite_cached_crc(const char *directory,
                               const char *filename,
                               bool include_record,
                               uint32_t file_size,
                               uint32_t file_crc) {
    char path[128];
    char temporary[128];
    char backup[128];
    char line[CRC_INDEX_LINE_BYTES];
    struct stat status = {0};
    FILE *input = NULL;
    FILE *output = NULL;
    bool had_index;
    bool success = true;

    if (directory == NULL || filename == NULL ||
        strcmp(filename, CRC_INDEX_FILENAME) == 0 ||
        !build_path(path, sizeof path, directory,
                    CRC_INDEX_FILENAME, "") ||
        !build_path(temporary, sizeof temporary, directory,
                    CRC_INDEX_FILENAME, ".part") ||
        !build_path(backup, sizeof backup, directory,
                    CRC_INDEX_FILENAME, ".bak")) {
        return false;
    }
    had_index = stat(path, &status) == 0;
    if (had_index) {
        input = fopen(path, "rb");
        if (input == NULL) {
            return false;
        }
    }
    unlink(temporary);
    output = fopen(temporary, "wb");
    if (output == NULL) {
        if (input != NULL) fclose(input);
        return false;
    }

    while (input != NULL && fgets(line, sizeof line, input) != NULL) {
        unsigned cached_crc;
        unsigned long cached_size;
        char cached_filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U] = {0};
        int fields = sscanf(line, "%8x,%lu,%48[^\r\n]",
                            &cached_crc, &cached_size, cached_filename);
        if (fields == 3 && strcmp(cached_filename, filename) == 0) {
            continue;
        }
        if (fputs(line, output) == EOF) {
            success = false;
            break;
        }
    }
    if (input != NULL) {
        if (ferror(input) != 0 || fclose(input) != 0) {
            success = false;
        }
        input = NULL;
    }
    if (success && include_record &&
        fprintf(output, "%08x,%u,%s\n",
                (unsigned)file_crc, (unsigned)file_size,
                filename) <= 0) {
        success = false;
    }
    if (success && fflush(output) != 0) {
        success = false;
    }
    if (success && fsync(fileno(output)) != 0) {
        success = false;
    }
    if (fclose(output) != 0) {
        success = false;
    }
    output = NULL;
    if (!success) {
        unlink(temporary);
        return false;
    }

    unlink(backup);
    if (had_index && rename(path, backup) != 0) {
        unlink(temporary);
        return false;
    }
    if (rename(temporary, path) != 0) {
        if (had_index) {
            (void)rename(backup, path);
        }
        unlink(temporary);
        return false;
    }
    if (had_index) {
        unlink(backup);
    }
    return true;
}

static void write_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t calibrated_baud(uint32_t baud) {
    /*
     * The onboard crystal-less CH340C is most reliable at a nominal 1 Mbaud
     * when the ESP32 uses APB divider 81.75 (978593 baud).  Keep this one
     * boot-time calibration common to both receive and transmit directions.
     */
    if (baud == TRANSFER_BAUD_1000K) return CALIBRATED_BAUD_1000K;
    return baud;
}

static bool set_baud(uint32_t baud) {
    return uart_set_baudrate(UPLOAD_UART, calibrated_baud(baud)) == ESP_OK;
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

static void stop_upload_writer(upload_writer_t *writer) {
    upload_block_t *stop = NULL;
    upload_block_t *stopped = (upload_block_t *)(1);
    xQueueSend(writer->ready, &stop, portMAX_DELAY);
    while (stopped != NULL) {
        xQueueReceive(writer->completed, &stopped, portMAX_DELAY);
    }
}

static int acknowledge_write(
    upload_writer_t *writer,
    size_t *pending_writes,
    uint32_t sequence,
    uint32_t received,
    uint32_t *last_acked_sequence,
    uint32_t *last_acked_bytes,
    const char **failure,
    TickType_t wait,
    bool send_heartbeat) {
    upload_block_t *finished = NULL;
    if (xQueueReceive(writer->completed, &finished, wait) != pdTRUE) {
        if (send_heartbeat) {
            write_response("HLVWAIT %u %u\n",
                           (unsigned)sequence, (unsigned)received);
        }
        return 0;
    }
    if (finished == NULL || *pending_writes == 0U) {
        *failure = "WRITE_FAILED";
        return -1;
    }
    --*pending_writes;
    if (writer->failed) {
        *failure = "WRITE_FAILED";
        return -1;
    }
    *last_acked_sequence = finished->sequence;
    *last_acked_bytes = finished->end_offset;
    write_response("HLVACK %u %u\n",
                   (unsigned)*last_acked_sequence,
                   (unsigned)*last_acked_bytes);
    return 1;
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

bool uart_file_upload_change_baud(uart_file_upload_t *upload,
                                  uint32_t baud) {
    static const uint8_t k_switch[16] = {
        'H', 'L', 'V', 'B', 'A', 'U', 'D', 'S',
        'W', 'I', 'T', 'C', 'H', ' ', '1', '\n'};
    uint8_t request[sizeof k_switch];
    uint32_t previous_baud;
    uint32_t attempt;
    bool valid_frame = false;

    if (upload == NULL || !supported_data_baud(baud)) {
        uart_file_upload_reject(upload, "BAD_REQUEST");
        return false;
    }
    previous_baud = upload->control_baud;
    uart_flush_input(UPLOAD_UART);
    for (attempt = 0; attempt < READ_BLOCK_ATTEMPTS; ++attempt) {
        write_response("HLVBAUDREADY 1 %u\n", (unsigned)baud);
    }
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    if (!read_exact(request, sizeof k_switch, CHUNK_TIMEOUT_MS) ||
        memcmp(request, k_switch, sizeof k_switch) != 0) {
        finish_response(upload, "HLVERR 1 BAUD_HANDSHAKE\n");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!set_baud(baud)) {
        finish_response(upload, "HLVERR 1 BAUD_FAILED\n");
        return false;
    }
    for (attempt = 0; attempt < READ_BLOCK_ATTEMPTS; ++attempt) {
        if (read_exact(request, 12, 2000U) &&
            memcmp(request, "HLVG", 4) == 0 &&
            read_le32(request + 4) == baud &&
            read_le32(request + 8) == crc32(request, 8)) {
            valid_frame = true;
            break;
        }
    }
    if (!valid_frame) {
        set_baud(previous_baud);
        vTaskDelay(pdMS_TO_TICKS(50));
        finish_response(upload, "HLVERR 1 BAUD_HANDSHAKE\n");
        return false;
    }
    memcpy(request, "HLVA", 4);
    write_le32(request + 4, baud);
    write_le32(request + 8, crc32(request, 8));
    for (attempt = 0; attempt < READ_BLOCK_ATTEMPTS; ++attempt) {
        uart_write_bytes(UPLOAD_UART, request, 12);
    }
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));

    valid_frame = false;
    for (attempt = 0; attempt < READ_BLOCK_ATTEMPTS; ++attempt) {
        if (read_exact(request, 12, 2000U) &&
            memcmp(request, "HLVD", 4) == 0 &&
            read_le32(request + 4) == baud &&
            read_le32(request + 8) == crc32(request, 8)) {
            valid_frame = true;
            break;
        }
    }
    if (!valid_frame) {
        set_baud(previous_baud);
        vTaskDelay(pdMS_TO_TICKS(50));
        finish_response(upload, "HLVERR 1 BAUD_HANDSHAKE\n");
        return false;
    }
    uart_flush_input(UPLOAD_UART);
    upload->control_baud = baud;
    memcpy(request, "HLVF", 4);
    write_le32(request + 4, baud);
    write_le32(request + 8, crc32(request, 8));
    for (attempt = 0; attempt < READ_BLOCK_ATTEMPTS; ++attempt) {
        uart_write_bytes(UPLOAD_UART, request, 12);
    }
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    return true;
}

esp_err_t uart_file_upload_begin(uart_file_upload_t *upload,
                                 uint32_t control_baud) {
    uart_config_t config = {0};
    esp_err_t result;

    if (upload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    upload->control_baud = control_baud;
    config.baud_rate = (int)calibrated_baud(control_baud);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_2;
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
    if (strncmp(line, "HLVSESSION ", 11) == 0) {
        char command[UART_SESSION_COMMAND_BYTES] = {0};
        char trailing = '\0';
        int fields = sscanf(
            line, "HLVSESSION 1 %15s %c", command, &trailing);
        if (fields != 1 ||
            (strcmp(command, "UPLOAD") != 0 &&
             strcmp(command, "LIST") != 0 &&
             strcmp(command, "READ") != 0 &&
             strcmp(command, "PATCH") != 0 &&
             strcmp(command, "CRC32") != 0 &&
             strcmp(command, "BAUD") != 0 &&
             strcmp(command, "DELETE") != 0 &&
             strcmp(command, "SDBENCH") != 0)) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        snprintf(upload->session_command,
                 sizeof upload->session_command, "%s", command);
        upload->session_requested = true;
        return false;
    }
    if (strncmp(line, "HLVMONITOR ", 11) == 0) {
        if (strcmp(line, "HLVMONITOR 1 ON") != 0) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        upload->monitoring_requested = true;
        return false;
    }
    if (strcmp(line, "HLVLIST 2") == 0) {
        if (!require_session(upload, "LIST")) {
            return false;
        }
        upload->list_requested = true;
        return false;
    }
    if (strncmp(line, "HLVBAUD ", 8) == 0) {
        unsigned baud = 0;
        char trailing = '\0';
        int fields = sscanf(line, "HLVBAUD 1 %u %c", &baud, &trailing);
        if (fields != 1 || !supported_data_baud(baud)) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        if (!require_session(upload, "BAUD")) {
            return false;
        }
        upload->requested_control_baud = baud;
        upload->control_baud_requested = true;
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
        if (!require_session(upload, "CRC32")) {
            return false;
        }
        snprintf(upload->crc_filename, sizeof upload->crc_filename,
                 "%s", filename);
        upload->crc_requested = true;
        return false;
    }
    if (strncmp(line, "HLVBLOCKCRC ", 12) == 0) {
        uart_block_crc_request_t parsed = {0};
        unsigned block_size = 0;
        char trailing = '\0';
        int fields = sscanf(
            line, "HLVBLOCKCRC 1 %48s %u %c", parsed.filename,
            &block_size, &trailing);
        if (fields != 2 || !valid_filename(parsed.filename) ||
            block_size < MINIMUM_BLOCK_CRC_BYTES ||
            block_size > MAXIMUM_BLOCK_CRC_BYTES ||
            (block_size & (block_size - 1U)) != 0U) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        if (!require_session(upload, "CRC32")) {
            return false;
        }
        parsed.block_size = block_size;
        upload->block_crc_request = parsed;
        upload->block_crc_requested = true;
        return false;
    }
    if (strncmp(line, "HLVREAD ", 8) == 0) {
        uart_read_request_t parsed = {0};
        unsigned long offset = 0;
        unsigned long size = 0;
        unsigned baud = 0;
        char trailing = '\0';
        int fields = sscanf(
            line, "HLVREAD 2 %48s %lu %lu %u %c", parsed.filename,
            &offset, &size, &baud, &trailing);
        if (fields != 4 || offset > UINT32_MAX || size == 0U ||
            size > UINT32_MAX || !valid_filename(parsed.filename) ||
            !supported_data_baud(baud)) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        if (!require_session(upload, "READ")) {
            return false;
        }
        parsed.offset = (uint32_t)offset;
        parsed.size = (uint32_t)size;
        parsed.data_baud = baud;
        upload->read_request = parsed;
        upload->read_requested = true;
        return false;
    }
    if (strncmp(line, "HLVPATCH ", 9) == 0) {
        uart_patch_request_t parsed = {0};
        unsigned long offset = 0;
        unsigned long size = 0;
        unsigned crc = 0;
        unsigned baud = 0;
        char trailing = '\0';
        int fields = sscanf(
            line, "HLVPATCH 1 %48s %lu %lu %x %u %c",
            parsed.filename, &offset, &size, &crc, &baud, &trailing);
        if (fields != 5 || offset > UINT32_MAX || size == 0U ||
            size > UINT32_MAX || !valid_filename(parsed.filename) ||
            !supported_data_baud(baud)) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        if (!require_session(upload, "PATCH")) {
            return false;
        }
        parsed.offset = (uint32_t)offset;
        parsed.size = (uint32_t)size;
        parsed.crc32 = (uint32_t)crc;
        parsed.data_baud = baud;
        upload->patch_request = parsed;
        upload->patch_requested = true;
        return false;
    }
    if (strncmp(line, "HLVDELETE ", 10) == 0) {
        char filename[UART_UPLOAD_MAX_FILENAME_BYTES + 1U] = {0};
        char trailing = '\0';
        int fields =
            sscanf(line, "HLVDELETE 1 %48s %c", filename, &trailing);
        if (fields != 1 || !valid_delete_filename(filename)) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        if (!require_session(upload, "DELETE")) {
            return false;
        }
        snprintf(upload->delete_filename,
                 sizeof upload->delete_filename, "%s", filename);
        upload->delete_requested = true;
        return false;
    }
    if (strncmp(line, "HLVSDBENCH ", 11) == 0) {
        char pattern[8] = {0};
        unsigned size_mib = 0;
        char trailing = '\0';
        int fields = sscanf(
            line, "HLVSDBENCH 1 %7s %u %c",
            pattern, &size_mib, &trailing);
        if (fields != 2 || size_mib == 0U ||
            size_mib > MAXIMUM_SD_BENCHMARK_MIB ||
            (strcmp(pattern, "zero") != 0 &&
             strcmp(pattern, "random") != 0)) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        if (!require_session(upload, "SDBENCH")) {
            return false;
        }
        upload->sd_benchmark_request.pattern =
            strcmp(pattern, "zero") == 0
                ? UART_SD_BENCHMARK_ZEROS
                : UART_SD_BENCHMARK_PSEUDO_RANDOM;
        upload->sd_benchmark_request.size_mib = size_mib;
        upload->sd_benchmark_requested = true;
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
            line, "HLVPUT 2 %48s %lu %x %u %c", parsed.filename,
            &size, &crc, &baud, &trailing);
        if (fields != 4 || size == 0U || size > UINT32_MAX ||
            !valid_filename(parsed.filename) ||
            !supported_data_baud(baud)) {
            uart_file_upload_reject(upload, "BAD_REQUEST");
            return false;
        }
        if (!require_session(upload, "UPLOAD")) {
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

bool uart_file_upload_take_session_request(uart_file_upload_t *upload,
                                           char *command,
                                           size_t command_bytes) {
    if (upload == NULL || command == NULL || command_bytes == 0U ||
        !upload->session_requested) {
        return false;
    }
    snprintf(command, command_bytes, "%s", upload->session_command);
    upload->session_command[0] = '\0';
    upload->session_requested = false;
    return true;
}

void uart_file_upload_session_ready(uart_file_upload_t *upload,
                                    const char *command) {
    if (upload == NULL) {
        return;
    }
    snprintf(upload->active_session_command,
             sizeof upload->active_session_command, "%s",
             command != NULL ? command : "TRANSFER");
    upload->session_active = true;
    upload->monitoring_requested = false;
    write_response("HLVSESSIONREADY 1 %s\n",
                   command != NULL ? command : "TRANSFER");
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
}

bool uart_file_upload_take_monitoring_request(
    uart_file_upload_t *upload) {
    bool requested;

    if (upload == NULL) {
        return false;
    }
    requested = upload->monitoring_requested;
    upload->monitoring_requested = false;
    return requested;
}

void uart_file_upload_monitoring_ready(uart_file_upload_t *upload) {
    if (upload == NULL) {
        return;
    }
    write_response("HLVMONITORREADY 1 ON\n");
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    upload->session_active = false;
    upload->active_session_command[0] = '\0';
}

bool uart_file_upload_take_read_request(uart_file_upload_t *upload,
                                        uart_read_request_t *request) {
    if (upload == NULL || request == NULL || !upload->read_requested) {
        return false;
    }
    *request = upload->read_request;
    memset(&upload->read_request, 0, sizeof upload->read_request);
    upload->read_requested = false;
    return true;
}

bool uart_file_upload_take_patch_request(uart_file_upload_t *upload,
                                         uart_patch_request_t *request) {
    if (upload == NULL || request == NULL || !upload->patch_requested) {
        return false;
    }
    *request = upload->patch_request;
    memset(&upload->patch_request, 0, sizeof upload->patch_request);
    upload->patch_requested = false;
    return true;
}

bool uart_file_upload_take_block_crc_request(
    uart_file_upload_t *upload,
    uart_block_crc_request_t *request) {
    if (upload == NULL || request == NULL ||
        !upload->block_crc_requested) {
        return false;
    }
    *request = upload->block_crc_request;
    memset(&upload->block_crc_request, 0,
           sizeof upload->block_crc_request);
    upload->block_crc_requested = false;
    return true;
}

bool uart_file_upload_take_baud_request(uart_file_upload_t *upload,
                                        uint32_t *baud) {
    if (upload == NULL || baud == NULL ||
        !upload->control_baud_requested) {
        return false;
    }
    *baud = upload->requested_control_baud;
    upload->requested_control_baud = 0;
    upload->control_baud_requested = false;
    return true;
}

bool uart_file_upload_take_delete_request(uart_file_upload_t *upload,
                                          char *filename,
                                          size_t filename_bytes) {
    if (upload == NULL || !upload->delete_requested ||
        filename == NULL || filename_bytes == 0U) {
        return false;
    }
    snprintf(filename, filename_bytes, "%s", upload->delete_filename);
    upload->delete_requested = false;
    upload->delete_filename[0] = '\0';
    return true;
}

bool uart_file_upload_take_sd_benchmark_request(
    uart_file_upload_t *upload,
    uart_sd_benchmark_request_t *request) {
    if (upload == NULL || !upload->sd_benchmark_requested ||
        request == NULL) {
        return false;
    }
    *request = upload->sd_benchmark_request;
    memset(&upload->sd_benchmark_request, 0,
           sizeof upload->sd_benchmark_request);
    upload->sd_benchmark_requested = false;
    return true;
}

static bool send_list_packet(uint32_t sequence, uint32_t file_size,
                             const char *name) {
    uint8_t packet[LIST_PACKET_HEADER_BYTES + 255U];
    size_t name_bytes = name != NULL ? strlen(name) : 0U;
    uint32_t checksum;
    uint32_t attempt;
    if (name_bytes > 255U) return false;
    memcpy(packet, k_list_packet_magic, sizeof k_list_packet_magic);
    write_le32(packet + 4, sequence);
    write_le32(packet + 8, file_size);
    packet[12] = (uint8_t)name_bytes;
    if (name_bytes != 0U) {
        memcpy(packet + LIST_PACKET_HEADER_BYTES, name, name_bytes);
    }
    checksum = esp_rom_crc32_le(0, packet, 13U);
    if (name_bytes != 0U) {
        checksum = esp_rom_crc32_le(
            checksum, packet + LIST_PACKET_HEADER_BYTES, name_bytes);
    }
    write_le32(packet + 13, checksum);
    for (attempt = 0; attempt < LIST_PACKET_ATTEMPTS; ++attempt) {
        uint8_t acknowledgment[READ_ACK_BYTES];
        int written = uart_write_bytes(
            UPLOAD_UART, packet, LIST_PACKET_HEADER_BYTES + name_bytes);
        if (written != (int)(LIST_PACKET_HEADER_BYTES + name_bytes)) {
            continue;
        }
        if (!read_exact(acknowledgment, sizeof acknowledgment,
                        LIST_ACK_TIMEOUT_MS) ||
            memcmp(acknowledgment, "HLVA", 4) != 0 ||
            read_le32(acknowledgment + 4) != sequence ||
            read_le32(acknowledgment + 9) !=
                crc32(acknowledgment, 9)) {
            continue;
        }
        if (acknowledgment[8] == 1U) return true;
    }
    return false;
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
        if (!send_list_packet(count, (uint32_t)status.st_size,
                              entry->d_name)) {
            closedir(handle);
            finish_response(upload, "HLVERR 2 LIST_FAILED\n");
            return false;
        }
        ++count;
    }
    closedir(handle);
    if (!send_list_packet(count, count, NULL)) {
        finish_response(upload, "HLVERR 2 LIST_FAILED\n");
        return false;
    }
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    return true;
}

static bool send_block_crc_packet(uint32_t sequence,
                                  uint32_t offset,
                                  uint32_t size,
                                  uint32_t block_crc) {
    uint8_t packet[BLOCK_CRC_PACKET_BYTES];
    uint32_t attempt;
    memcpy(packet, k_block_crc_packet_magic,
           sizeof k_block_crc_packet_magic);
    write_le32(packet + 4, sequence);
    write_le32(packet + 8, offset);
    write_le32(packet + 12, size);
    write_le32(packet + 16, block_crc);
    write_le32(packet + 20, crc32(packet, 20));
    for (attempt = 0; attempt < BLOCK_CRC_PACKET_ATTEMPTS; ++attempt) {
        uint8_t acknowledgment[READ_ACK_BYTES];
        int written = uart_write_bytes(
            UPLOAD_UART, packet, sizeof packet);
        if (written != (int)sizeof packet) {
            continue;
        }
        if (!read_exact(acknowledgment, sizeof acknowledgment,
                        BLOCK_CRC_ACK_TIMEOUT_MS) ||
            memcmp(acknowledgment, "HLVA", 4) != 0 ||
            read_le32(acknowledgment + 4) != sequence ||
            read_le32(acknowledgment + 9) !=
                crc32(acknowledgment, 9)) {
            continue;
        }
        if (acknowledgment[8] == 1U) {
            return true;
        }
    }
    return false;
}

bool uart_file_upload_checksum_blocks(
    uart_file_upload_t *upload,
    const char *directory,
    const uart_block_crc_request_t *request) {
    char path[128];
    struct stat status = {0};
    FILE *input;
    uint8_t *buffer;
    uint32_t file_size;
    uint32_t file_crc = 0;
    uint32_t offset = 0;
    uint32_t sequence = 0;
    bool success = true;

    if (request == NULL ||
        !build_path(path, sizeof path, directory, request->filename, "")) {
        uart_file_upload_reject(upload, "BAD_PATH");
        return false;
    }
    if (stat(path, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < 0 || (uint64_t)status.st_size > UINT32_MAX) {
        uart_file_upload_reject(upload, "NOT_FOUND");
        return false;
    }
    file_size = (uint32_t)status.st_size;
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

    uart_flush_input(UPLOAD_UART);
    while (offset < file_size) {
        uint32_t block_size = file_size - offset;
        uint32_t consumed = 0;
        uint32_t block_crc = 0;
        if (block_size > request->block_size) {
            block_size = request->block_size;
        }
        while (consumed < block_size) {
            size_t wanted = block_size - consumed;
            size_t received;
            if (wanted > CRC_BUFFER_BYTES) {
                wanted = CRC_BUFFER_BYTES;
            }
            received = fread(buffer, 1, wanted, input);
            if (received != wanted) {
                success = false;
                break;
            }
            block_crc = esp_rom_crc32_le(block_crc, buffer, received);
            file_crc = esp_rom_crc32_le(file_crc, buffer, received);
            consumed += (uint32_t)received;
        }
        if (!success ||
            !send_block_crc_packet(sequence, offset, block_size, block_crc)) {
            success = false;
            break;
        }
        offset += block_size;
        ++sequence;
    }
    if (success &&
        !send_block_crc_packet(sequence, file_size, 0, file_crc)) {
        success = false;
    }
    if (fclose(input) != 0) {
        success = false;
    }
    heap_caps_free(buffer);
    if (!success) {
        finish_response(upload, "HLVERR 1 BLOCK_CRC_FAILED\n");
        return false;
    }
    (void)rewrite_cached_crc(directory, request->filename, true,
                             file_size, file_crc);
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    return true;
}

bool uart_file_upload_checksum_file(uart_file_upload_t *upload,
                                    const char *directory,
                                    const char *filename) {
    char path[128];
    struct stat status = {0};
    FILE *input;
    uint8_t *buffer;
    uint32_t file_crc = 0;
    uint32_t file_size;
    bool success = true;

    if (!build_path(path, sizeof path, directory, filename, "")) {
        uart_file_upload_reject(upload, "BAD_PATH");
        return false;
    }
    if (stat(path, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < 0 || (uint64_t)status.st_size > UINT32_MAX) {
        uart_file_upload_reject(upload, "NOT_FOUND");
        return false;
    }
    file_size = (uint32_t)status.st_size;
    if (strcmp(filename, CRC_INDEX_FILENAME) != 0 &&
        find_cached_crc(directory, filename, file_size, &file_crc)) {
        finish_response(upload, "HLVCRC 1 %u %08x %s\n",
                        (unsigned)file_size, (unsigned)file_crc, filename);
        return true;
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
    (void)rewrite_cached_crc(
        directory, filename, true, file_size, file_crc);
    finish_response(upload, "HLVCRC 1 %u %08x %s\n",
                    (unsigned)file_size, (unsigned)file_crc, filename);
    return true;
}

bool uart_file_upload_read_file(uart_file_upload_t *upload,
                                const char *directory,
                                const uart_read_request_t *request) {
    char path[128];
    struct stat status = {0};
    FILE *input;
    uint8_t *packet;
    uint32_t file_size;
    uint32_t remaining;
    uint32_t sent = 0;
    uint32_t sequence = 0;
    uint32_t range_crc = 0;
    bool success = true;

    if (request == NULL ||
        !build_path(path, sizeof path, directory, request->filename, "")) {
        uart_file_upload_reject(upload, "BAD_PATH");
        return false;
    }
    if (stat(path, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < 0 || (uint64_t)status.st_size > UINT32_MAX) {
        uart_file_upload_reject(upload, "NOT_FOUND");
        return false;
    }
    file_size = (uint32_t)status.st_size;
    if (request->offset > file_size) {
        uart_file_upload_reject(upload, "BAD_RANGE");
        return false;
    }
    remaining = file_size - request->offset;
    if (remaining > request->size) {
        remaining = request->size;
    }
    input = fopen(path, "rb");
    if (input == NULL) {
        uart_file_upload_reject(upload, "OPEN_FAILED");
        return false;
    }
    if (fseek(input, (long)request->offset, SEEK_SET) != 0) {
        fclose(input);
        uart_file_upload_reject(upload, "SEEK_FAILED");
        return false;
    }
    packet = (uint8_t *)heap_caps_malloc(
        READ_BLOCK_HEADER_BYTES + READ_BLOCK_BYTES, MALLOC_CAP_8BIT);
    if (packet == NULL) {
        fclose(input);
        uart_file_upload_reject(upload, "NO_MEMORY");
        return false;
    }

    uart_flush_input(UPLOAD_UART);
    {
        uint8_t ready[READ_READY_BYTES];
        uint32_t ready_attempt;
        memcpy(ready, "HLVR", 4);
        write_le32(ready + 4, file_size);
        write_le32(ready + 8, request->offset);
        write_le32(ready + 12, remaining);
        write_le32(ready + 16, request->data_baud);
        write_le32(ready + 20, crc32(ready, 20));
        for (ready_attempt = 0; ready_attempt < READ_BLOCK_ATTEMPTS;
             ++ready_attempt) {
            uart_write_bytes(UPLOAD_UART, ready, sizeof ready);
        }
    }
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    if (request->data_baud != upload->control_baud) {
        uint8_t go[8];
        static const uint8_t k_go[8] = {
            'H', 'L', 'V', 'G', 'O', ' ', '2', '\n'};
        if (!set_baud(request->data_baud)) {
            heap_caps_free(packet);
            fclose(input);
            finish_response(upload, "HLVERR 2 BAUD_FAILED\n");
            return false;
        }
        if (!read_exact(go, sizeof go, CHUNK_TIMEOUT_MS) ||
            memcmp(go, k_go, sizeof k_go) != 0) {
            heap_caps_free(packet);
            fclose(input);
            set_baud(upload->control_baud);
            vTaskDelay(pdMS_TO_TICKS(50));
            finish_response(upload, "HLVERR 2 HANDSHAKE_FAILED\n");
            return false;
        }
    }

    memcpy(packet, k_read_block_magic, sizeof k_read_block_magic);
    while (sent < remaining) {
        uint32_t bytes = remaining - sent;
        uint32_t block_crc;
        bool acknowledged = false;
        uint32_t attempt;
        if (bytes > READ_BLOCK_BYTES) {
            bytes = READ_BLOCK_BYTES;
        }
        if (fread(packet + READ_BLOCK_HEADER_BYTES, 1, bytes, input) !=
            bytes) {
            success = false;
            break;
        }
        block_crc = crc32(packet + READ_BLOCK_HEADER_BYTES, bytes);
        range_crc = esp_rom_crc32_le(
            range_crc, packet + READ_BLOCK_HEADER_BYTES, bytes);
        write_le32(packet + 4, sequence);
        write_le16(packet + 8, (uint16_t)bytes);
        write_le32(packet + 10, block_crc);
        for (attempt = 0; attempt < READ_DATA_ATTEMPTS; ++attempt) {
            uint8_t acknowledgment[READ_ACK_BYTES];
            int written = uart_write_bytes(
                UPLOAD_UART, packet, READ_BLOCK_HEADER_BYTES + bytes);
            if (written != (int)(READ_BLOCK_HEADER_BYTES + bytes)) {
                continue;
            }
            if (uart_wait_tx_done(UPLOAD_UART,
                                  pdMS_TO_TICKS(CHUNK_TIMEOUT_MS)) !=
                ESP_OK) {
                continue;
            }
            if (!read_exact(acknowledgment, sizeof acknowledgment,
                            READ_ACK_TIMEOUT_MS) ||
                memcmp(acknowledgment, "HLVA", 4) != 0 ||
                read_le32(acknowledgment + 4) != sequence ||
                read_le32(acknowledgment + 9) !=
                    crc32(acknowledgment, 9)) {
                continue;
            }
            if (acknowledgment[8] == 1U) {
                acknowledged = true;
                break;
            }
        }
        if (!acknowledged) {
            success = false;
            break;
        }
        sent += bytes;
        ++sequence;
    }
    if (fclose(input) != 0) {
        success = false;
    }
    heap_caps_free(packet);
    if (!success || sent != remaining) {
        set_baud(upload->control_baud);
        vTaskDelay(pdMS_TO_TICKS(50));
        finish_response(upload, "HLVERR 2 READ_FAILED\n");
        return false;
    }
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    set_baud(upload->control_baud);
    vTaskDelay(pdMS_TO_TICKS(50));
    {
        uint8_t done[READ_DONE_BYTES];
        memcpy(done, "HLVE", 4);
        write_le32(done + 4, sent);
        write_le32(done + 8, range_crc);
        write_le32(done + 12, crc32(done, 12));
        for (sequence = 0; sequence < READ_BLOCK_ATTEMPTS; ++sequence) {
            uart_write_bytes(UPLOAD_UART, done, sizeof done);
        }
        uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    }
    return true;
}

bool uart_file_upload_delete_file(uart_file_upload_t *upload,
                                  const char *directory,
                                  const char *filename) {
    char path[128];
    struct stat status = {0};

    if (!build_path(path, sizeof path, directory, filename, "")) {
        uart_file_upload_reject(upload, "BAD_PATH");
        return false;
    }
    if (stat(path, &status) != 0 || !S_ISREG(status.st_mode)) {
        uart_file_upload_reject(upload, "NOT_FOUND");
        return false;
    }
    if (remove(path) != 0) {
        uart_file_upload_reject(upload, "DELETE_FAILED");
        return false;
    }
    (void)rewrite_cached_crc(directory, filename, false, 0U, 0U);
    finish_response(upload, "HLVDELETE 1 %s\n", filename);
    return true;
}

static bool copy_file_bytes(FILE *source, FILE *destination,
                            uint32_t bytes, uint32_t *checksum) {
    uint8_t buffer[1024];
    uint32_t copied = 0;
    uint32_t crc = 0;

    while (copied < bytes) {
        size_t remaining = (size_t)(bytes - copied);
        size_t wanted = remaining < sizeof buffer ? remaining : sizeof buffer;
        if (fread(buffer, 1, wanted, source) != wanted) {
            return false;
        }
        if (destination != NULL &&
            fwrite(buffer, 1, wanted, destination) != wanted) {
            return false;
        }
        crc = esp_rom_crc32_le(crc, buffer, wanted);
        copied += (uint32_t)wanted;
    }
    if (checksum != NULL) {
        *checksum = crc;
    }
    return true;
}

static bool sync_file(FILE *file) {
    return file != NULL && fflush(file) == 0 &&
           fsync(fileno(file)) == 0;
}

bool uart_file_upload_patch_file(uart_file_upload_t *upload,
                                 const char *directory,
                                 const uart_patch_request_t *request) {
    char target_path[128];
    char patch_path[128];
    char backup_path[128];
    struct stat status = {0};
    FILE *patch = NULL;
    FILE *target = NULL;
    FILE *backup = NULL;
    uint8_t block[UART_PATCH_CHUNK_BYTES];
    uint32_t received = 0;
    uint32_t sequence = 0;
    uint32_t patch_crc = 0;
    uint32_t original_crc = 0;
    uint32_t verified_crc = 0;
    const char *failure = "PATCH_FAILED";
    bool backup_ready = false;
    bool applied = false;
    bool restored = false;
    size_t attempt;

    if (upload == NULL || request == NULL ||
        !build_path(target_path, sizeof target_path, directory,
                    request->filename, "") ||
        !build_path(patch_path, sizeof patch_path, directory,
                    request->filename, ".patch") ||
        !build_path(backup_path, sizeof backup_path, directory,
                    request->filename, ".patchbak")) {
        uart_file_upload_reject(upload, "PATH_TOO_LONG");
        return false;
    }
    if (stat(target_path, &status) != 0 || status.st_size < 0 ||
        (uint64_t)status.st_size > UINT32_MAX) {
        uart_file_upload_reject(upload, "OPEN_FAILED");
        return false;
    }
    if (request->offset > (uint32_t)status.st_size ||
        request->size > (uint32_t)status.st_size - request->offset) {
        uart_file_upload_reject(upload, "RANGE");
        return false;
    }

    unlink(patch_path);
    unlink(backup_path);
    patch = fopen(patch_path, "wb");
    if (patch == NULL) {
        uart_file_upload_reject(upload, "OPEN_FAILED");
        return false;
    }

    uart_flush_input(UPLOAD_UART);
    for (attempt = 0; attempt < READ_BLOCK_ATTEMPTS; ++attempt) {
        write_response("HLVPATCHREADY 1 %u %u\n",
                       (unsigned)UART_PATCH_CHUNK_BYTES,
                       (unsigned)request->data_baud);
    }
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    if (request->data_baud != upload->control_baud) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (!set_baud(request->data_baud)) {
            failure = "BAUD_FAILED";
            goto receive_failed;
        }
    }

    while (received < request->size) {
        uint8_t header[BLOCK_HEADER_BYTES];
        uint32_t block_sequence;
        uint16_t block_bytes;
        uint32_t block_crc;

        if (!read_exact(header, sizeof header, CHUNK_TIMEOUT_MS)) {
            failure = "TIMEOUT";
            goto receive_failed;
        }
        block_sequence = read_le32(header + 4);
        block_bytes = read_le16(header + 8);
        block_crc = read_le32(header + 10);
        if (memcmp(header, k_patch_block_magic,
                   sizeof k_patch_block_magic) != 0 ||
            block_bytes == 0U ||
            block_bytes > UART_PATCH_CHUNK_BYTES ||
            block_bytes > request->size - received) {
            failure = "BAD_BLOCK";
            goto receive_failed;
        }
        if (!read_exact(block, block_bytes, CHUNK_TIMEOUT_MS)) {
            failure = "TIMEOUT";
            goto receive_failed;
        }
        if (block_sequence != sequence) {
            if (block_sequence < sequence && sequence != 0U) {
                write_response("HLVPATCHACK %u %u\n",
                               (unsigned)(sequence - 1U),
                               (unsigned)received);
            } else {
                write_response("HLVPATCHNAK %u ORDER\n",
                               (unsigned)sequence);
            }
            continue;
        }
        if (crc32(block, block_bytes) != block_crc) {
            write_response("HLVPATCHNAK %u CRC\n",
                           (unsigned)sequence);
            continue;
        }
        if (fwrite(block, 1, block_bytes, patch) != block_bytes) {
            failure = "WRITE_FAILED";
            goto receive_failed;
        }
        patch_crc = esp_rom_crc32_le(patch_crc, block, block_bytes);
        received += block_bytes;
        write_response("HLVPATCHACK %u %u\n",
                       (unsigned)sequence, (unsigned)received);
        ++sequence;
    }
    if (patch_crc != request->crc32) {
        failure = "PATCH_CRC";
        goto receive_failed;
    }
    if (!sync_file(patch)) {
        fclose(patch);
        patch = NULL;
        failure = "FLUSH_FAILED";
        goto receive_failed;
    }
    if (fclose(patch) != 0) {
        patch = NULL;
        failure = "CLOSE_FAILED";
        goto receive_failed;
    }
    patch = NULL;

    target = fopen(target_path, "rb");
    backup = fopen(backup_path, "wb");
    if (target == NULL || backup == NULL ||
        fseek(target, (long)request->offset, SEEK_SET) != 0 ||
        !copy_file_bytes(target, backup, request->size, &original_crc) ||
        !sync_file(backup)) {
        failure = "BACKUP_FAILED";
        goto apply_failed;
    }
    if (fclose(backup) != 0) {
        backup = NULL;
        failure = "BACKUP_FAILED";
        goto apply_failed;
    }
    backup = NULL;
    if (fclose(target) != 0) {
        target = NULL;
        failure = "BACKUP_FAILED";
        goto apply_failed;
    }
    target = NULL;
    backup_ready = true;

    target = fopen(target_path, "r+b");
    patch = fopen(patch_path, "rb");
    if (target == NULL || patch == NULL ||
        fseek(target, (long)request->offset, SEEK_SET) != 0 ||
        !copy_file_bytes(patch, target, request->size, NULL) ||
        !sync_file(target) ||
        fseek(target, (long)request->offset, SEEK_SET) != 0 ||
        !copy_file_bytes(target, NULL, request->size, &verified_crc) ||
        verified_crc != request->crc32) {
        failure = "VERIFY_FAILED";
        goto apply_failed;
    }
    applied = true;

apply_failed:
    if (!applied && backup_ready) {
        if (patch != NULL) {
            fclose(patch);
            patch = NULL;
        }
        if (backup != NULL) {
            fclose(backup);
            backup = NULL;
        }
        if (target == NULL) {
            target = fopen(target_path, "r+b");
        }
        backup = fopen(backup_path, "rb");
        if (target != NULL && backup != NULL &&
            fseek(target, (long)request->offset, SEEK_SET) == 0 &&
            copy_file_bytes(backup, target, request->size, NULL) &&
            sync_file(target) &&
            fseek(target, (long)request->offset, SEEK_SET) == 0 &&
            copy_file_bytes(target, NULL, request->size, &verified_crc) &&
            verified_crc == original_crc) {
            restored = true;
        }
    }
    if (backup != NULL) fclose(backup);
    if (patch != NULL) fclose(patch);
    if (target != NULL) fclose(target);
    unlink(patch_path);
    if (applied || restored) unlink(backup_path);
    if (!applied) {
        finish_response(upload, "HLVERR 1 %s%s\n", failure,
                        restored ? "_RESTORED" : "");
        return false;
    }

    (void)rewrite_cached_crc(directory, request->filename,
                             false, 0U, 0U);
    for (attempt = 1; attempt < READ_BLOCK_ATTEMPTS; ++attempt) {
        write_response("HLVPATCHDONE 1 %u %u %08x %s\n",
                       (unsigned)request->offset,
                       (unsigned)request->size,
                       (unsigned)request->crc32,
                       request->filename);
    }
    finish_response(upload, "HLVPATCHDONE 1 %u %u %08x %s\n",
                    (unsigned)request->offset,
                    (unsigned)request->size,
                    (unsigned)request->crc32,
                    request->filename);
    return true;

receive_failed:
    if (patch != NULL) fclose(patch);
    unlink(patch_path);
    finish_response(upload, "HLVERR 1 %s\n", failure);
    return false;
}

bool uart_file_upload_benchmark_sd(
    uart_file_upload_t *upload,
    const char *directory,
    const uart_sd_benchmark_request_t *request) {
    char path[384];
    uint8_t *buffer;
    const char *pattern_name = "zero";
    FILE *output;
    uint32_t total_bytes;
    uint32_t written = 0;
    int64_t started_us;
    int64_t elapsed_us;
    bool success = true;
    bool removed;

    if (directory == NULL || request == NULL ||
        request->size_mib == 0U ||
        request->size_mib > MAXIMUM_SD_BENCHMARK_MIB) {
        uart_file_upload_reject(upload, "BAD_REQUEST");
        return false;
    }
    if (!build_path(path, sizeof path, directory,
                    SD_BENCHMARK_FILENAME, "")) {
        uart_file_upload_reject(upload, "PATH_TOO_LONG");
        return false;
    }
    unlink(path);

    buffer = (uint8_t *)heap_caps_malloc(
        SD_BENCHMARK_BLOCK_BYTES, MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        uart_file_upload_reject(upload, "NO_MEMORY");
        return false;
    }
    if (request->pattern == UART_SD_BENCHMARK_ZEROS) {
        memset(buffer, 0, SD_BENCHMARK_BLOCK_BYTES);
    } else {
        uint32_t state = 0x9e3779b9U;
        size_t index;
        pattern_name = "random";
        for (index = 0; index < SD_BENCHMARK_BLOCK_BYTES; ++index) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            buffer[index] = (uint8_t)state;
        }
    }

    output = fopen(path, "wb");
    if (output == NULL) {
        heap_caps_free(buffer);
        uart_file_upload_reject(upload, "OPEN_FAILED");
        return false;
    }
    setvbuf(output, NULL, _IONBF, 0);

    total_bytes = request->size_mib * 1024U * 1024U;
    write_response("HLVSDBENCHBEGIN 1 %s %u %u\n",
                   pattern_name, (unsigned)total_bytes,
                   (unsigned)SD_BENCHMARK_BLOCK_BYTES);
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));

    started_us = esp_timer_get_time();
    while (written < total_bytes) {
        size_t remaining = (size_t)(total_bytes - written);
        size_t bytes =
            remaining < SD_BENCHMARK_BLOCK_BYTES
                ? remaining
                : SD_BENCHMARK_BLOCK_BYTES;
        if (fwrite(buffer, 1, bytes, output) != bytes) {
            success = false;
            break;
        }
        written += (uint32_t)bytes;
    }
    if (success &&
        (fflush(output) != 0 || fsync(fileno(output)) != 0)) {
        success = false;
    }
    if (fclose(output) != 0) success = false;
    elapsed_us = esp_timer_get_time() - started_us;
    removed = unlink(path) == 0;
    heap_caps_free(buffer);

    if (!success || written != total_bytes) {
        finish_response(upload, "HLVERR 1 SD_WRITE_FAILED\n");
        return false;
    }
    if (!removed) {
        finish_response(upload, "HLVERR 1 SD_CLEANUP_FAILED\n");
        return false;
    }
    {
        uint32_t kib_per_second = (uint32_t)(
            ((uint64_t)written * 1000000ULL) /
            (uint64_t)elapsed_us / 1024ULL);
        finish_response(
            upload, "HLVSDBENCHRESULT 1 %s %u %u %u\n",
            pattern_name, (unsigned)written,
            (unsigned)elapsed_us, (unsigned)kib_per_second);
    }
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
    QueueHandle_t ready;
    QueueHandle_t completed;
    FILE *output;
    FILE *verification;
    upload_block_t blocks[UART_UPLOAD_BUFFER_COUNT] = {0};
    upload_writer_t writer = {0};
    uint32_t received = 0;
    uint32_t verified_file_crc = 0;
    uint32_t sequence = 0;
    size_t pending_writes = 0;
    bool success = true;
    const char *failure = "TRANSFER_FAILED";
    uint32_t last_acked_sequence = UINT32_MAX;
    uint32_t last_acked_bytes = 0;
    bool nak_outstanding = false;
    size_t index;

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

    buffer = (uint8_t *)heap_caps_malloc(
        UART_UPLOAD_CHUNK_BYTES * UART_UPLOAD_BUFFER_COUNT,
        MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        uart_file_upload_reject(upload, "NO_MEMORY");
        return false;
    }
    ready = xQueueCreate(
        UART_UPLOAD_BUFFER_COUNT, sizeof(upload_block_t *));
    completed = xQueueCreate(
        UART_UPLOAD_BUFFER_COUNT, sizeof(upload_block_t *));
    if (ready == NULL || completed == NULL) {
        if (ready != NULL) vQueueDelete(ready);
        if (completed != NULL) vQueueDelete(completed);
        heap_caps_free(buffer);
        uart_file_upload_reject(upload, "NO_MEMORY");
        return false;
    }
    unlink(temporary);
    output = fopen(temporary, "wb");
    if (output == NULL) {
        vQueueDelete(ready);
        vQueueDelete(completed);
        heap_caps_free(buffer);
        uart_file_upload_reject(upload, "OPEN_FAILED");
        return false;
    }
    setvbuf(output, NULL, _IONBF, 0);

    for (index = 0; index < UART_UPLOAD_BUFFER_COUNT; ++index) {
        blocks[index].data =
            buffer + index * UART_UPLOAD_CHUNK_BYTES;
    }
    writer.output = output;
    writer.ready = ready;
    writer.completed = completed;
    if (xTaskCreatePinnedToCore(
            upload_writer_task, "uart_sd_writer",
            WRITER_STACK_BYTES, &writer, WRITER_PRIORITY,
            NULL, 1) != pdPASS) {
        fclose(output);
        unlink(temporary);
        vQueueDelete(ready);
        vQueueDelete(completed);
        heap_caps_free(buffer);
        uart_file_upload_reject(upload, "NO_MEMORY");
        return false;
    }

    uart_flush_input(UPLOAD_UART);
    for (index = 0; index < READ_BLOCK_ATTEMPTS; ++index) {
        write_response("HLVREADY 2 %u %u %u\n",
                       (unsigned)UART_UPLOAD_CHUNK_BYTES,
                       (unsigned)request->data_baud,
                       (unsigned)UART_UPLOAD_BUFFER_COUNT);
    }
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(1000));
    if (request->data_baud != upload->control_baud) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (request->data_baud != upload->control_baud &&
        !set_baud(request->data_baud)) {
        stop_upload_writer(&writer);
        fclose(output);
        unlink(temporary);
        vQueueDelete(ready);
        vQueueDelete(completed);
        heap_caps_free(buffer);
        uart_file_upload_reject(upload, "BAUD_FAILED");
        return false;
    }

    while (received < request->size) {
        uint8_t header[BLOCK_HEADER_BYTES];
        uint32_t block_sequence;
        uint16_t block_bytes;
        uint32_t block_crc;
        upload_block_t *block;
        int completed_result;

        for (;;) {
            while ((completed_result = acknowledge_write(
                        &writer, &pending_writes,
                        sequence, received,
                        &last_acked_sequence,
                        &last_acked_bytes,
                        &failure, 0, false)) > 0) {
            }
            if (completed_result < 0) {
                success = false;
                break;
            }
            if (pending_writes == UART_UPLOAD_BUFFER_COUNT) {
                completed_result = acknowledge_write(
                    &writer, &pending_writes,
                    sequence, received,
                    &last_acked_sequence,
                    &last_acked_bytes,
                    &failure, pdMS_TO_TICKS(250), true);
                if (completed_result < 0) {
                    success = false;
                    break;
                }
                continue;
            }
            break;
        }
        if (!success) break;

        if (!read_exact(header, sizeof header, CHUNK_TIMEOUT_MS)) {
            failure = "TIMEOUT";
            success = false;
            break;
        }
        block_sequence = read_le32(header + 4);
        block_bytes = read_le16(header + 8);
        block_crc = read_le32(header + 10);
        if (memcmp(header, k_block_magic, sizeof k_block_magic) != 0 ||
            block_bytes == 0U ||
            block_bytes > UART_UPLOAD_CHUNK_BYTES) {
            failure = "BAD_BLOCK";
            success = false;
            break;
        }
        block = &blocks[sequence % UART_UPLOAD_BUFFER_COUNT];
        if (!read_exact(block->data, block_bytes, CHUNK_TIMEOUT_MS)) {
            failure = "TIMEOUT";
            success = false;
            break;
        }
        if (block_sequence != sequence) {
            if (block_sequence < sequence &&
                last_acked_sequence != UINT32_MAX) {
                write_response(
                    "HLVACK %u %u\n",
                    (unsigned)last_acked_sequence,
                    (unsigned)last_acked_bytes);
            } else if (!nak_outstanding) {
                write_response("HLVNAK %u ORDER\n",
                               (unsigned)sequence);
                nak_outstanding = true;
            }
            continue;
        }
        if (block_bytes > request->size - received) {
            failure = "BAD_BLOCK";
            success = false;
            break;
        }
        if (crc32(block->data, block_bytes) != block_crc) {
            write_response("HLVNAK %u CRC\n", (unsigned)sequence);
            nak_outstanding = true;
            continue;
        }
        nak_outstanding = false;
        block->size = block_bytes;
        block->sequence = sequence;
        block->end_offset = received + block_bytes;
        if (xQueueSend(ready, &block, portMAX_DELAY) != pdTRUE) {
            failure = "WRITE_FAILED";
            success = false;
            break;
        }
        ++pending_writes;
        received += block_bytes;
        if (progress != NULL) {
            progress(received, request->size, progress_context);
        }
        ++sequence;
    }

    while (pending_writes != 0U && success) {
        int result = acknowledge_write(
            &writer, &pending_writes,
            sequence, received,
            &last_acked_sequence, &last_acked_bytes,
            &failure, pdMS_TO_TICKS(250), true);
        if (result < 0) success = false;
    }
    if (writer.failed || writer.written != received) {
        failure = "WRITE_FAILED";
        success = false;
    }
    stop_upload_writer(&writer);

    if (success && (fflush(output) != 0 || fsync(fileno(output)) != 0)) {
        failure = "FLUSH_FAILED";
        success = false;
    }
    if (fclose(output) != 0) {
        failure = "CLOSE_FAILED";
        success = false;
    }
    vQueueDelete(ready);
    vQueueDelete(completed);
    heap_caps_free(buffer);

    if (success && writer.file_crc != request->crc32) {
        failure = "FILE_CRC";
        success = false;
    }
    if (success) {
        bool verification_success;
        verification = fopen(temporary, "rb");
        verification_success = verification != NULL;
        if (verification_success) {
            verification_success = copy_file_bytes(
                verification, NULL, request->size, &verified_file_crc);
            if (fclose(verification) != 0) {
                verification_success = false;
            }
        }
        if (!verification_success ||
            verified_file_crc != request->crc32) {
            failure = "FILE_VERIFY_CRC";
            success = false;
        }
    }
    if (!success) {
        unlink(temporary);
        finish_response(upload, "HLVERR 2 %s\n", failure);
        return false;
    }

    {
        struct stat target_status = {0};
        bool had_target = stat(target, &target_status) == 0;
        unlink(backup);
        if (had_target && rename(target, backup) != 0) {
            unlink(temporary);
            finish_response(upload, "HLVERR 2 BACKUP_FAILED\n");
            return false;
        }
        if (rename(temporary, target) != 0) {
            if (had_target) {
                rename(backup, target);
            }
            unlink(temporary);
            finish_response(upload, "HLVERR 2 COMMIT_FAILED\n");
            return false;
        }
        if (had_target) {
            unlink(backup);
        }
    }

    if (stored_path != NULL && stored_path_bytes != 0U) {
        snprintf(stored_path, stored_path_bytes, "%s", target);
    }
    (void)rewrite_cached_crc(
        directory, request->filename, true,
        request->size, writer.file_crc);
    for (index = 1; index < READ_BLOCK_ATTEMPTS; ++index) {
        write_response("HLVDONE 2 %u %08x %s\n",
                       (unsigned)request->size,
                       (unsigned)writer.file_crc,
                       request->filename);
    }
    finish_response(upload, "HLVDONE 2 %u %08x %s\n",
                    (unsigned)request->size,
                    (unsigned)writer.file_crc,
                    request->filename);
    return true;
}
