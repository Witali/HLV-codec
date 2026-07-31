#include "uart_file_upload.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {

constexpr uart_port_t kUploadUart = UART_NUM_0;
constexpr int kUartRxBufferBytes = 2048;
constexpr uint32_t kChunkTimeoutMs = 10000;
constexpr uint32_t kTransferBaud460k = 460800;
constexpr uint32_t kTransferBaud921k = 921600;
constexpr uint32_t kTransferBaud1500k = 1500000;
constexpr uint32_t kTransferBaud2000k = 2000000;
constexpr uint32_t kTransferBaud3000k = 3000000;
constexpr uint8_t kBlockMagic[] = {'H', 'L', 'V', 'B'};
constexpr size_t kBlockHeaderBytes = 14;
constexpr uint32_t kWriterStackBytes = 4096;
constexpr UBaseType_t kWriterPriority = tskIDLE_PRIORITY + 2;
constexpr size_t kSdBenchmarkBlockBytes = 32 * 1024;
constexpr uint32_t kMaximumSdBenchmarkMiB = 64;
constexpr char kSdBenchmarkFilename[] = ".hlv-sd-benchmark.tmp";
constexpr char kCrcIndexFilename[] = "crc32.txt";
constexpr size_t kCrcIndexLineBytes = 128;

struct UploadBlock {
    uint8_t *data = nullptr;
    size_t size = 0;
    uint32_t sequence = 0;
    uint32_t end_offset = 0;
};

struct UploadWriter {
    FILE *output = nullptr;
    QueueHandle_t ready = nullptr;
    QueueHandle_t completed = nullptr;
    uint32_t file_crc = 0;
    uint32_t written = 0;
    bool failed = false;
};

void uploadWriterTask(void *opaque) {
    auto *writer = static_cast<UploadWriter *>(opaque);
    for (;;) {
        UploadBlock *block = nullptr;
        if (xQueueReceive(
                writer->ready, &block, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!block) {
            xQueueSend(
                writer->completed, &block, portMAX_DELAY);
            vTaskDelete(nullptr);
            return;
        }
        if (!writer->failed) {
            if (std::fwrite(
                    block->data, 1, block->size,
                    writer->output) != block->size) {
                writer->failed = true;
            } else {
                writer->file_crc = esp_rom_crc32_le(
                    writer->file_crc, block->data, block->size);
                writer->written += block->size;
            }
        }
        xQueueSend(
            writer->completed, &block, portMAX_DELAY);
    }
}

uint16_t readLe16(const uint8_t *bytes) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8));
}

uint32_t readLe32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint32_t crc32(const uint8_t *bytes, size_t count) {
    return esp_rom_crc32_le(0, bytes, count);
}

bool validFilename(const char *filename) {
    const size_t length = std::strlen(filename);
    if (!length || length > UartUploadRequest::kMaximumFilenameBytes ||
        filename[0] == '.') {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        const char character = filename[i];
        const bool valid =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-';
        if (!valid) return false;
    }
    const auto endsWith = [filename, length](const char *suffix) {
        const size_t suffix_length = std::strlen(suffix);
        if (suffix_length > length) return false;
        const char *value = filename + length - suffix_length;
        for (size_t index = 0; index < suffix_length; ++index) {
            char left = value[index];
            char right = suffix[index];
            if (left >= 'A' && left <= 'Z')
                left = static_cast<char>(left - 'A' + 'a');
            if (right >= 'A' && right <= 'Z')
                right = static_cast<char>(right - 'A' + 'a');
            if (left != right) return false;
        }
        return true;
    };
    return endsWith(".hlv") || endsWith(".avi") ||
           endsWith(".3gp") ||
           endsWith(".bpv1") || endsWith(".mpg") ||
           endsWith(".mpeg") || endsWith(".txt");
}

bool validDeleteFilename(const char *filename) {
    if (validFilename(filename)) return true;
    const size_t length = std::strlen(filename);
    constexpr char kPartSuffix[] = ".part";
    constexpr size_t kPartSuffixLength = sizeof kPartSuffix - 1;
    if (length <= kPartSuffixLength) return false;
    const char *suffix = filename + length - kPartSuffixLength;
    for (size_t index = 0; index < kPartSuffixLength; ++index) {
        char left = suffix[index];
        char right = kPartSuffix[index];
        if (left >= 'A' && left <= 'Z')
            left = static_cast<char>(left - 'A' + 'a');
        if (left != right) return false;
    }
    char destination[UartUploadRequest::kMaximumFilenameBytes + 1]{};
    const size_t destination_length = length - kPartSuffixLength;
    std::memcpy(destination, filename, destination_length);
    destination[destination_length] = '\0';
    return validFilename(destination);
}

bool supportedDataBaud(uint32_t baud) {
    return baud == kTransferBaud460k || baud == kTransferBaud921k ||
           baud == kTransferBaud1500k || baud == kTransferBaud2000k ||
           baud == kTransferBaud3000k;
}

bool buildPath(char *destination, size_t destination_bytes,
               const char *directory, const char *filename,
               const char *suffix) {
    const int result = std::snprintf(destination, destination_bytes,
                                     "%s/%s%s", directory, filename, suffix);
    return result >= 0 && static_cast<size_t>(result) < destination_bytes;
}

bool findCachedCrc(const char *directory, const char *filename,
                   uint32_t file_size, uint32_t *file_crc) {
    char path[128];
    if (!file_crc ||
        !buildPath(path, sizeof path, directory, kCrcIndexFilename, "")) {
        return false;
    }
    FILE *index = std::fopen(path, "rb");
    if (!index) return false;

    char line[kCrcIndexLineBytes];
    bool found = false;
    while (std::fgets(line, sizeof line, index)) {
        unsigned cached_crc;
        unsigned long cached_size;
        char cached_filename[UartUploadRequest::kMaximumFilenameBytes + 1]{};
        const int fields = std::sscanf(
            line, "%8x,%lu,%48[^\r\n]",
            &cached_crc, &cached_size, cached_filename);
        if (fields == 3 &&
            cached_size == static_cast<unsigned long>(file_size) &&
            !std::strcmp(cached_filename, filename)) {
            *file_crc = static_cast<uint32_t>(cached_crc);
            found = true;
        }
    }
    std::fclose(index);
    return found;
}

bool rewriteCachedCrc(const char *directory, const char *filename,
                      bool include_record, uint32_t file_size,
                      uint32_t file_crc) {
    char path[128];
    char temporary[128];
    char backup[128];
    if (!directory || !filename ||
        !std::strcmp(filename, kCrcIndexFilename) ||
        !buildPath(path, sizeof path, directory, kCrcIndexFilename, "") ||
        !buildPath(temporary, sizeof temporary, directory,
                   kCrcIndexFilename, ".part") ||
        !buildPath(backup, sizeof backup, directory,
                   kCrcIndexFilename, ".bak")) {
        return false;
    }

    struct stat status {};
    const bool had_index = stat(path, &status) == 0;
    FILE *input = had_index ? std::fopen(path, "rb") : nullptr;
    if (had_index && !input) return false;
    unlink(temporary);
    FILE *output = std::fopen(temporary, "wb");
    if (!output) {
        if (input) std::fclose(input);
        return false;
    }

    bool success = true;
    char line[kCrcIndexLineBytes];
    while (input && std::fgets(line, sizeof line, input)) {
        unsigned cached_crc;
        unsigned long cached_size;
        char cached_filename[
            UartUploadRequest::kMaximumFilenameBytes + 1]{};
        const int fields = std::sscanf(
            line, "%8x,%lu,%48[^\r\n]",
            &cached_crc, &cached_size, cached_filename);
        if (fields == 3 && !std::strcmp(cached_filename, filename)) {
            continue;
        }
        if (std::fputs(line, output) == EOF) {
            success = false;
            break;
        }
    }
    if (input) {
        if (std::ferror(input) || std::fclose(input)) success = false;
        input = nullptr;
    }
    if (success && include_record &&
        std::fprintf(output, "%08x,%u,%s\n",
                     static_cast<unsigned>(file_crc),
                     static_cast<unsigned>(file_size), filename) <= 0) {
        success = false;
    }
    if (success && std::fflush(output)) success = false;
    if (success && fsync(fileno(output))) success = false;
    if (std::fclose(output)) success = false;
    output = nullptr;
    if (!success) {
        unlink(temporary);
        return false;
    }

    unlink(backup);
    if (had_index && rename(path, backup)) {
        unlink(temporary);
        return false;
    }
    if (rename(temporary, path)) {
        if (had_index) {
            (void)rename(backup, path);
        }
        unlink(temporary);
        return false;
    }
    if (had_index) unlink(backup);
    return true;
}

}  // namespace

esp_err_t UartFileUpload::begin(uint32_t control_baud) {
    control_baud_ = control_baud;
    uart_config_t config{};
    config.baud_rate = static_cast<int>(control_baud);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    esp_err_t result = uart_param_config(kUploadUart, &config);
    if (result != ESP_OK) return result;
    result = uart_set_pin(kUploadUart, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (result != ESP_OK) return result;
    if (!uart_is_driver_installed(kUploadUart)) {
        result = uart_driver_install(kUploadUart, kUartRxBufferBytes, 0, 0,
                                     nullptr, 0);
        if (result != ESP_OK) return result;
    }
    uart_flush_input(kUploadUart);
    ready_ = true;
    writeResponse("HLVUART 1 READY %u\n",
                  static_cast<unsigned>(control_baud_));
    return uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
}

bool UartFileUpload::parseRequest(const char *line,
                                  UartUploadRequest *request) {
    if (!std::strcmp(line, "HLVLIST 1")) {
        list_requested_ = true;
        return false;
    }
    if (!std::strncmp(line, "HLVCRC ", 7)) {
        char filename[UartUploadRequest::kMaximumFilenameBytes + 1]{};
        char trailing = '\0';
        const int fields = std::sscanf(
            line, "HLVCRC 1 %48s %c", filename, &trailing);
        if (fields != 1 || !validFilename(filename)) {
            reject("BAD_REQUEST");
            return false;
        }
        std::snprintf(crc_filename_, sizeof crc_filename_, "%s", filename);
        crc_requested_ = true;
        return false;
    }
    if (!std::strncmp(line, "HLVDELETE ", 10)) {
        char filename[UartUploadRequest::kMaximumFilenameBytes + 1]{};
        char trailing = '\0';
        const int fields = std::sscanf(
            line, "HLVDELETE 1 %48s %c", filename, &trailing);
        if (fields != 1 || !validDeleteFilename(filename)) {
            reject("BAD_REQUEST");
            return false;
        }
        std::snprintf(
            delete_filename_, sizeof delete_filename_, "%s", filename);
        delete_requested_ = true;
        return false;
    }
    if (!std::strncmp(line, "HLVSDBENCH ", 11)) {
        char pattern[8]{};
        unsigned size_mib = 0;
        char trailing = '\0';
        const int fields = std::sscanf(
            line, "HLVSDBENCH 1 %7s %u %c",
            pattern, &size_mib, &trailing);
        if (fields != 2 || !size_mib ||
            size_mib > kMaximumSdBenchmarkMiB ||
            (std::strcmp(pattern, "zero") &&
             std::strcmp(pattern, "random"))) {
            reject("BAD_REQUEST");
            return false;
        }
        sd_benchmark_request_.pattern =
            !std::strcmp(pattern, "zero")
                ? SdBenchmarkPattern::kZeros
                : SdBenchmarkPattern::kPseudoRandom;
        sd_benchmark_request_.size_mib = size_mib;
        sd_benchmark_requested_ = true;
        return false;
    }
    if (std::strncmp(line, "HLVPUT ", 7)) return false;

    UartUploadRequest parsed{};
    unsigned long size = 0;
    unsigned crc = 0;
    unsigned baud = 0;
    char trailing = '\0';
    const int fields = std::sscanf(
        line, "HLVPUT 2 %48s %lu %x %u %c", parsed.filename, &size, &crc,
        &baud, &trailing);
    if (fields != 4 || !size || size > UINT32_MAX ||
        !validFilename(parsed.filename) || !supportedDataBaud(baud)) {
        reject("BAD_REQUEST");
        return false;
    }
    parsed.size = static_cast<uint32_t>(size);
    parsed.crc32 = static_cast<uint32_t>(crc);
    parsed.data_baud = baud;
    *request = parsed;
    return true;
}

bool UartFileUpload::pollRequest(UartUploadRequest *request) {
    if (!ready_ || !request) return false;

    uint8_t bytes[64];
    const int count = uart_read_bytes(kUploadUart, bytes, sizeof bytes, 0);
    if (count <= 0) return false;
    for (int i = 0; i < count; ++i) {
        const uint8_t byte = bytes[i];
        if (byte == '\r') continue;
        if (byte == '\n') {
            line_[line_size_] = '\0';
            const bool parsed = parseRequest(line_, request);
            line_size_ = 0;
            if (parsed) return true;
            continue;
        }
        if (byte < 0x20 || byte > 0x7e) {
            line_size_ = 0;
            continue;
        }
        if (line_size_ + 1 < sizeof line_) {
            line_[line_size_++] = static_cast<char>(byte);
        } else {
            line_size_ = 0;
        }
    }
    return false;
}

bool UartFileUpload::takeListRequest() {
    const bool requested = list_requested_;
    list_requested_ = false;
    return requested;
}

bool UartFileUpload::takeCrcRequest(char *filename, size_t filename_bytes) {
    if (!crc_requested_ || !filename || !filename_bytes) return false;
    std::snprintf(filename, filename_bytes, "%s", crc_filename_);
    crc_requested_ = false;
    crc_filename_[0] = '\0';
    return true;
}

bool UartFileUpload::takeDeleteRequest(
        char *filename, size_t filename_bytes) {
    if (!delete_requested_ || !filename || !filename_bytes) return false;
    std::snprintf(filename, filename_bytes, "%s", delete_filename_);
    delete_requested_ = false;
    delete_filename_[0] = '\0';
    return true;
}

bool UartFileUpload::takeSdBenchmarkRequest(
        SdBenchmarkRequest *request) {
    if (!sd_benchmark_requested_ || !request) return false;
    *request = sd_benchmark_request_;
    sd_benchmark_request_ = {};
    sd_benchmark_requested_ = false;
    return true;
}

bool UartFileUpload::listDirectory(const char *directory) {
    if (!directory) {
        reject("BAD_DIRECTORY");
        return false;
    }
    DIR *handle = opendir(directory);
    if (!handle) {
        reject("LIST_FAILED");
        return false;
    }

    writeResponse("HLVLISTBEGIN 1\n");
    uint32_t count = 0;
    while (const dirent *entry = readdir(handle)) {
        if (!std::strcmp(entry->d_name, ".") ||
            !std::strcmp(entry->d_name, "..")) {
            continue;
        }
        char path[384];
        if (!buildPath(path, sizeof path, directory, entry->d_name, "")) {
            continue;
        }
        struct stat status {};
        if (stat(path, &status) || !S_ISREG(status.st_mode)) continue;
        writeResponse("HLVFILE 1 %u %s\n",
                      static_cast<unsigned>(status.st_size),
                      entry->d_name);
        ++count;
    }
    closedir(handle);
    finishResponse("HLVLISTEND 1 %u\n",
                   static_cast<unsigned>(count));
    return true;
}

bool UartFileUpload::checksumFile(const char *directory,
                                  const char *filename) {
    char path[128];
    if (!directory || !filename ||
        !buildPath(path, sizeof path, directory, filename, "")) {
        reject("BAD_PATH");
        return false;
    }
    struct stat status {};
    if (stat(path, &status) || !S_ISREG(status.st_mode) ||
        status.st_size < 0 ||
        static_cast<uint64_t>(status.st_size) > UINT32_MAX) {
        reject("NOT_FOUND");
        return false;
    }
    const uint32_t file_size = static_cast<uint32_t>(status.st_size);
    uint32_t file_crc = 0;
    if (std::strcmp(filename, kCrcIndexFilename) &&
        findCachedCrc(directory, filename, file_size, &file_crc)) {
        finishResponse("HLVCRC 1 %u %08x %s\n",
                       static_cast<unsigned>(file_size),
                       static_cast<unsigned>(file_crc), filename);
        return true;
    }
    FILE *input = std::fopen(path, "rb");
    if (!input) {
        reject("OPEN_FAILED");
        return false;
    }
    constexpr size_t kCrcBufferBytes = 4096;
    auto *buffer = static_cast<uint8_t *>(
        heap_caps_malloc(kCrcBufferBytes, MALLOC_CAP_8BIT));
    if (!buffer) {
        std::fclose(input);
        reject("NO_MEMORY");
        return false;
    }

    bool success = true;
    for (;;) {
        const size_t received =
            std::fread(buffer, 1, kCrcBufferBytes, input);
        if (received) {
            file_crc = esp_rom_crc32_le(file_crc, buffer, received);
        }
        if (received != kCrcBufferBytes) {
            success = std::feof(input) != 0 && std::ferror(input) == 0;
            break;
        }
    }
    if (std::fclose(input)) success = false;
    heap_caps_free(buffer);
    if (!success) {
        finishResponse("HLVERR 1 READ_FAILED\n");
        return false;
    }
    (void)rewriteCachedCrc(
        directory, filename, true, file_size, file_crc);
    finishResponse("HLVCRC 1 %u %08x %s\n",
                   static_cast<unsigned>(file_size),
                   static_cast<unsigned>(file_crc), filename);
    return true;
}

bool UartFileUpload::deleteFile(
        const char *directory, const char *filename) {
    char path[128];
    if (!directory || !filename ||
        !buildPath(path, sizeof path, directory, filename, "")) {
        reject("BAD_PATH");
        return false;
    }
    struct stat status {};
    if (stat(path, &status) || !S_ISREG(status.st_mode)) {
        reject("NOT_FOUND");
        return false;
    }
    if (std::remove(path)) {
        reject("DELETE_FAILED");
        return false;
    }
    (void)rewriteCachedCrc(directory, filename, false, 0, 0);
    finishResponse("HLVDELETE 1 %s\n", filename);
    return true;
}

bool UartFileUpload::benchmarkSd(
        const char *directory, const SdBenchmarkRequest &request) {
    if (!directory || !request.size_mib ||
        request.size_mib > kMaximumSdBenchmarkMiB) {
        reject("BAD_REQUEST");
        return false;
    }

    char path[384];
    if (!buildPath(path, sizeof path, directory,
                   kSdBenchmarkFilename, "")) {
        reject("PATH_TOO_LONG");
        return false;
    }
    unlink(path);

    auto *buffer = static_cast<uint8_t *>(heap_caps_malloc(
        kSdBenchmarkBlockBytes, MALLOC_CAP_8BIT));
    if (!buffer) {
        reject("NO_MEMORY");
        return false;
    }
    const char *pattern_name = "zero";
    if (request.pattern == SdBenchmarkPattern::kZeros) {
        std::memset(buffer, 0, kSdBenchmarkBlockBytes);
    } else {
        pattern_name = "random";
        uint32_t state = 0x9e3779b9U;
        for (size_t index = 0;
             index < kSdBenchmarkBlockBytes; ++index) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            buffer[index] = static_cast<uint8_t>(state);
        }
    }

    FILE *output = std::fopen(path, "wb");
    if (!output) {
        heap_caps_free(buffer);
        reject("OPEN_FAILED");
        return false;
    }
    std::setvbuf(output, nullptr, _IONBF, 0);

    const uint32_t total_bytes = request.size_mib * 1024U * 1024U;
    writeResponse("HLVSDBENCHBEGIN 1 %s %u %u\n",
                  pattern_name, static_cast<unsigned>(total_bytes),
                  static_cast<unsigned>(kSdBenchmarkBlockBytes));
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));

    bool success = true;
    uint32_t written = 0;
    const int64_t started_us = esp_timer_get_time();
    while (written < total_bytes) {
        const size_t bytes =
            std::min<size_t>(kSdBenchmarkBlockBytes,
                             total_bytes - written);
        if (std::fwrite(buffer, 1, bytes, output) != bytes) {
            success = false;
            break;
        }
        written += static_cast<uint32_t>(bytes);
    }
    if (success &&
        (std::fflush(output) || fsync(fileno(output)))) {
        success = false;
    }
    if (std::fclose(output)) success = false;
    const int64_t elapsed_us = esp_timer_get_time() - started_us;
    const bool removed = unlink(path) == 0;
    heap_caps_free(buffer);

    if (!success || written != total_bytes) {
        finishResponse("HLVERR 1 SD_WRITE_FAILED\n");
        return false;
    }
    if (!removed) {
        finishResponse("HLVERR 1 SD_CLEANUP_FAILED\n");
        return false;
    }
    const uint32_t kib_per_second = static_cast<uint32_t>(
        (static_cast<uint64_t>(written) * 1000000ULL) /
        static_cast<uint64_t>(elapsed_us) / 1024ULL);
    finishResponse("HLVSDBENCHRESULT 1 %s %u %u %u\n",
                   pattern_name, static_cast<unsigned>(written),
                   static_cast<unsigned>(elapsed_us),
                   static_cast<unsigned>(kib_per_second));
    return true;
}

bool UartFileUpload::setBaud(uint32_t baud) {
    return uart_set_baudrate(kUploadUart, baud) == ESP_OK;
}

bool UartFileUpload::readExact(uint8_t *destination, size_t bytes,
                               uint32_t timeout_ms) {
    const int64_t deadline =
        esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
    size_t received = 0;
    while (received < bytes && esp_timer_get_time() < deadline) {
        const int count = uart_read_bytes(
            kUploadUart, destination + received, bytes - received,
            pdMS_TO_TICKS(100));
        if (count > 0) received += static_cast<size_t>(count);
    }
    return received == bytes;
}

void UartFileUpload::writeResponse(const char *format, ...) {
    char response[384];
    va_list arguments;
    va_start(arguments, format);
    const int length =
        std::vsnprintf(response, sizeof response, format, arguments);
    va_end(arguments);
    if (length <= 0) return;
    const size_t bytes =
        static_cast<size_t>(length) < sizeof response
            ? static_cast<size_t>(length)
            : sizeof response - 1;
    uart_write_bytes(kUploadUart, response, bytes);
}

void UartFileUpload::finishResponse(const char *format, ...) {
    char response[384];
    va_list arguments;
    va_start(arguments, format);
    const int length =
        std::vsnprintf(response, sizeof response, format, arguments);
    va_end(arguments);
    if (length > 0) {
        const size_t bytes =
            static_cast<size_t>(length) < sizeof response
                ? static_cast<size_t>(length)
                : sizeof response - 1;
        uart_write_bytes(kUploadUart, response, bytes);
        uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
    }
    setBaud(control_baud_);
}

void UartFileUpload::reject(const char *reason) {
    writeResponse("HLVERR 1 %s\n", reason ? reason : "FAILED");
}

bool UartFileUpload::receive(const UartUploadRequest &request,
                             const char *directory, char *stored_path,
                             size_t stored_path_bytes,
                             ProgressCallback progress,
                             void *progress_context) {
    char target[128];
    char temporary[128];
    char backup[128];
    if (!buildPath(target, sizeof target, directory, request.filename, "") ||
        !buildPath(temporary, sizeof temporary, directory, request.filename,
                   ".part") ||
        !buildPath(backup, sizeof backup, directory, request.filename,
                   ".bak")) {
        reject("PATH_TOO_LONG");
        return false;
    }

    auto *buffer = static_cast<uint8_t *>(heap_caps_malloc(
        kChunkBytes * kBufferCount, MALLOC_CAP_8BIT));
    if (!buffer) {
        reject("NO_MEMORY");
        return false;
    }
    QueueHandle_t ready = xQueueCreate(
        kBufferCount, sizeof(UploadBlock *));
    QueueHandle_t completed = xQueueCreate(
        kBufferCount, sizeof(UploadBlock *));
    if (!ready || !completed) {
        if (ready) vQueueDelete(ready);
        if (completed) vQueueDelete(completed);
        heap_caps_free(buffer);
        reject("NO_MEMORY");
        return false;
    }
    unlink(temporary);
    FILE *output = std::fopen(temporary, "wb");
    if (!output) {
        vQueueDelete(ready);
        vQueueDelete(completed);
        heap_caps_free(buffer);
        reject("OPEN_FAILED");
        return false;
    }
    std::setvbuf(output, nullptr, _IONBF, 0);

    UploadBlock blocks[kBufferCount]{};
    for (size_t index = 0; index < kBufferCount; ++index) {
        blocks[index].data = buffer + index * kChunkBytes;
    }
    UploadWriter writer{};
    writer.output = output;
    writer.ready = ready;
    writer.completed = completed;
    if (xTaskCreatePinnedToCore(
            uploadWriterTask, "uart_sd_writer", kWriterStackBytes,
            &writer, kWriterPriority, nullptr, 1) != pdPASS) {
        std::fclose(output);
        unlink(temporary);
        vQueueDelete(ready);
        vQueueDelete(completed);
        heap_caps_free(buffer);
        reject("NO_MEMORY");
        return false;
    }
    const auto stop_writer = [&writer]() {
        UploadBlock *stop = nullptr;
        xQueueSend(writer.ready, &stop, portMAX_DELAY);
        UploadBlock *stopped = reinterpret_cast<UploadBlock *>(1);
        while (stopped) {
            xQueueReceive(
                writer.completed, &stopped, portMAX_DELAY);
        }
    };

    uart_flush_input(kUploadUart);
    writeResponse("HLVREADY 2 %u %u %u\n",
                  static_cast<unsigned>(kChunkBytes),
                  static_cast<unsigned>(request.data_baud),
                  static_cast<unsigned>(kBufferCount));
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!setBaud(request.data_baud)) {
        stop_writer();
        std::fclose(output);
        unlink(temporary);
        vQueueDelete(ready);
        vQueueDelete(completed);
        heap_caps_free(buffer);
        reject("BAUD_FAILED");
        return false;
    }

    uint32_t received = 0;
    uint32_t sequence = 0;
    size_t pending_writes = 0;
    bool success = true;
    const char *failure = "TRANSFER_FAILED";
    uint32_t last_acked_sequence = UINT32_MAX;
    uint32_t last_acked_bytes = 0;
    bool nak_outstanding = false;
    const auto acknowledge_write =
        [&](TickType_t wait, bool send_heartbeat) -> int {
        UploadBlock *finished = nullptr;
        if (xQueueReceive(completed, &finished, wait) != pdTRUE) {
            if (send_heartbeat) {
                writeResponse(
                    "HLVWAIT %u %u\n",
                    static_cast<unsigned>(sequence),
                    static_cast<unsigned>(received));
            }
            return 0;
        }
        if (!finished || !pending_writes) {
            failure = "WRITE_FAILED";
            success = false;
            return -1;
        }
        --pending_writes;
        if (writer.failed) {
            failure = "WRITE_FAILED";
            success = false;
            return -1;
        }
        last_acked_sequence = finished->sequence;
        last_acked_bytes = finished->end_offset;
        writeResponse(
            "HLVACK %u %u\n",
            static_cast<unsigned>(last_acked_sequence),
            static_cast<unsigned>(last_acked_bytes));
        return 1;
    };

    while (received < request.size) {
        for (;;) {
            int completed_result;
            while ((completed_result =
                        acknowledge_write(0, false)) > 0) {
            }
            if (completed_result < 0) break;
            if (pending_writes == kBufferCount) {
                if (acknowledge_write(
                        pdMS_TO_TICKS(250), true) < 0) {
                    break;
                }
                continue;
            }
            break;
        }
        if (!success) break;

        uint8_t header[kBlockHeaderBytes];
        if (!readExact(header, sizeof header, kChunkTimeoutMs)) {
            failure = "TIMEOUT";
            success = false;
            break;
        }
        const uint32_t block_sequence = readLe32(header + 4);
        const uint16_t block_bytes = readLe16(header + 8);
        const uint32_t block_crc = readLe32(header + 10);
        if (std::memcmp(header, kBlockMagic, sizeof kBlockMagic) ||
            !block_bytes || block_bytes > kChunkBytes) {
            failure = "BAD_BLOCK";
            success = false;
            break;
        }
        UploadBlock *block =
            &blocks[sequence % kBufferCount];
        if (!readExact(
                block->data, block_bytes, kChunkTimeoutMs)) {
            failure = "TIMEOUT";
            success = false;
            break;
        }
        if (block_sequence != sequence) {
            if (block_sequence < sequence &&
                last_acked_sequence != UINT32_MAX) {
                writeResponse(
                    "HLVACK %u %u\n",
                    static_cast<unsigned>(last_acked_sequence),
                    static_cast<unsigned>(last_acked_bytes));
            } else if (!nak_outstanding) {
                writeResponse(
                    "HLVNAK %u ORDER\n",
                    static_cast<unsigned>(sequence));
                nak_outstanding = true;
            }
            continue;
        }
        const uint32_t remaining = request.size - received;
        if (block_bytes > remaining) {
            failure = "BAD_BLOCK";
            success = false;
            break;
        }
        if (crc32(block->data, block_bytes) != block_crc) {
            writeResponse("HLVNAK %u CRC\n",
                          static_cast<unsigned>(sequence));
            nak_outstanding = true;
            continue;
        }
        nak_outstanding = false;
        block->size = block_bytes;
        block->sequence = sequence;
        block->end_offset = received + block_bytes;
        if (xQueueSend(
                ready, &block, portMAX_DELAY) != pdTRUE) {
            failure = "WRITE_FAILED";
            success = false;
            break;
        }
        ++pending_writes;
        received += block_bytes;
        if (progress) {
            progress(received, request.size, progress_context);
        }
        ++sequence;
    }

    while (pending_writes && success) {
        if (acknowledge_write(pdMS_TO_TICKS(250), true) < 0) {
            break;
        }
    }
    if (writer.failed || writer.written != received) {
        failure = "WRITE_FAILED";
        success = false;
    }
    stop_writer();

    if (success && (std::fflush(output) || fsync(fileno(output)))) {
        failure = "FLUSH_FAILED";
        success = false;
    }
    if (std::fclose(output)) {
        failure = "CLOSE_FAILED";
        success = false;
    }
    vQueueDelete(ready);
    vQueueDelete(completed);
    heap_caps_free(buffer);

    const uint32_t actual_crc = writer.file_crc;
    if (success && actual_crc != request.crc32) {
        failure = "FILE_CRC";
        success = false;
    }
    if (!success) {
        unlink(temporary);
        finishResponse("HLVERR 2 %s\n", failure);
        return false;
    }

    struct stat target_status {};
    const bool had_target = stat(target, &target_status) == 0;
    unlink(backup);
    if (had_target && rename(target, backup)) {
        unlink(temporary);
        finishResponse("HLVERR 2 BACKUP_FAILED\n");
        return false;
    }
    if (rename(temporary, target)) {
        if (had_target) rename(backup, target);
        unlink(temporary);
        finishResponse("HLVERR 2 COMMIT_FAILED\n");
        return false;
    }
    if (had_target) unlink(backup);

    if (stored_path && stored_path_bytes) {
        std::snprintf(stored_path, stored_path_bytes, "%s", target);
    }
    (void)rewriteCachedCrc(
        directory, request.filename, true,
        request.size, actual_crc);
    finishResponse("HLVDONE 2 %u %08x %s\n",
                   static_cast<unsigned>(request.size),
                   static_cast<unsigned>(actual_crc), request.filename);
    return true;
}
