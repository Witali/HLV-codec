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
constexpr int kUartRxBufferBytes = 12288;
constexpr uint32_t kChunkTimeoutMs = 10000;
constexpr uint32_t kTransferBaud460k = 460800;
constexpr uint32_t kTransferBaud921k = 921600;
constexpr uint32_t kTransferBaud1000k = 1000000;
constexpr uint32_t kCalibratedBaud1000k = 978593;
constexpr uint32_t kTransferBaud1500k = 1500000;
constexpr uint32_t kTransferBaud2000k = 2000000;
constexpr uint32_t kTransferBaud3000k = 3000000;
constexpr uint32_t kCalibratedBaud2000k = UART_CALIBRATED_BAUD_2000K;
constexpr uint32_t kCalibratedBaud3000k = UART_CALIBRATED_BAUD_3000K;
constexpr uint8_t kBlockMagic[] = {'H', 'L', 'V', 'B'};
constexpr uint8_t kPatchBlockMagic[] = {'H', 'L', 'V', 'P'};
constexpr size_t kBlockHeaderBytes = 14;
constexpr uint8_t kReadBlockMagic[] = {'H', 'L', 'V', 'X'};
constexpr uint8_t kListPacketMagic[] = {'H', 'L', 'V', 'L'};
constexpr uint8_t kBlockCrcPacketMagic[] = {'H', 'L', 'V', 'K'};
constexpr size_t kReadBlockBytes = 64;
constexpr size_t kReadBlockHeaderBytes = 14;
constexpr size_t kReadAckBytes = 13;
constexpr size_t kReadReadyBytes = 24;
constexpr size_t kReadDoneBytes = 16;
constexpr unsigned kReadBlockAttempts = 5;
constexpr unsigned kReadDataAttempts = 20;
constexpr uint32_t kReadAckTimeoutMs = 500;
constexpr size_t kListPacketHeaderBytes = 17;
constexpr unsigned kListPacketAttempts = 20;
constexpr unsigned kListAckTimeoutMs = 500;
constexpr size_t kBlockCrcPacketBytes = 24;
constexpr unsigned kBlockCrcPacketAttempts = 20;
constexpr uint32_t kBlockCrcAckTimeoutMs = 500;
constexpr uint32_t kMinimumBlockCrcBytes = 4096;
constexpr uint32_t kMaximumBlockCrcBytes = 1024 * 1024;
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
           baud == kTransferBaud1000k || baud == kTransferBaud1500k ||
           baud == kTransferBaud2000k ||
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

void writeLe16(uint8_t *bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}

void writeLe32(uint8_t *bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
    bytes[2] = static_cast<uint8_t>(value >> 16);
    bytes[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t calibratedBaud(uint32_t baud) {
    // One boot-time calibration is shared by the ESP32 RX and TX paths.
    if (baud == kTransferBaud1000k) return kCalibratedBaud1000k;
    if (baud == kTransferBaud2000k) return kCalibratedBaud2000k;
    if (baud == kTransferBaud3000k) return kCalibratedBaud3000k;
    return baud;
}

bool copyFileBytes(FILE *source, FILE *destination,
                   uint32_t bytes, uint32_t *checksum) {
    uint8_t buffer[1024];
    uint32_t copied = 0;
    uint32_t value = 0;
    while (copied < bytes) {
        const size_t remaining = static_cast<size_t>(bytes - copied);
        const size_t wanted =
            remaining < sizeof buffer ? remaining : sizeof buffer;
        if (std::fread(buffer, 1, wanted, source) != wanted) return false;
        if (destination &&
            std::fwrite(buffer, 1, wanted, destination) != wanted) {
            return false;
        }
        value = esp_rom_crc32_le(value, buffer, wanted);
        copied += static_cast<uint32_t>(wanted);
    }
    if (checksum) *checksum = value;
    return true;
}

bool syncFile(FILE *file) {
    return file && !std::fflush(file) && !fsync(fileno(file));
}

}  // namespace

esp_err_t UartFileUpload::begin(uint32_t control_baud) {
    control_baud_ = control_baud;
    uart_config_t config{};
    config.baud_rate = static_cast<int>(calibratedBaud(control_baud));
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_2;
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

bool UartFileUpload::requireSession(const char *command) {
    if (!session_active_) {
        reject("SESSION_REQUIRED");
        return false;
    }
    if (std::strcmp(active_session_command_, command)) {
        reject("SESSION_MISMATCH");
        return false;
    }
    return true;
}

bool UartFileUpload::parseRequest(const char *line,
                                  UartUploadRequest *request) {
    if (!std::strncmp(line, "HLVSESSION ", 11)) {
        char command[sizeof session_command_]{};
        char trailing = '\0';
        const int fields = std::sscanf(
            line, "HLVSESSION 1 %15s %c", command, &trailing);
        if (fields != 1 ||
            (std::strcmp(command, "UPLOAD") &&
             std::strcmp(command, "LIST") &&
             std::strcmp(command, "READ") &&
             std::strcmp(command, "PATCH") &&
             std::strcmp(command, "CRC32") &&
             std::strcmp(command, "BAUD") &&
             std::strcmp(command, "DELETE") &&
             std::strcmp(command, "SDBENCH"))) {
            reject("BAD_REQUEST");
            return false;
        }
        std::snprintf(session_command_, sizeof session_command_, "%s",
                      command);
        session_requested_ = true;
        return false;
    }
    if (!std::strncmp(line, "HLVMONITOR ", 11)) {
        if (std::strcmp(line, "HLVMONITOR 1 ON")) {
            reject("BAD_REQUEST");
            return false;
        }
        monitoring_requested_ = true;
        return false;
    }
    if (!std::strcmp(line, "HLVLIST 2")) {
        if (!requireSession("LIST")) return false;
        list_requested_ = true;
        return false;
    }
    if (!std::strncmp(line, "HLVBAUD ", 8)) {
        unsigned baud = 0;
        char trailing = '\0';
        const int fields = std::sscanf(
            line, "HLVBAUD 1 %u %c", &baud, &trailing);
        if (fields != 1 || !supportedDataBaud(baud)) {
            reject("BAD_REQUEST");
            return false;
        }
        if (!requireSession("BAUD")) return false;
        requested_control_baud_ = baud;
        control_baud_requested_ = true;
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
        if (!requireSession("CRC32")) return false;
        std::snprintf(crc_filename_, sizeof crc_filename_, "%s", filename);
        crc_requested_ = true;
        return false;
    }
    if (!std::strncmp(line, "HLVBLOCKCRC ", 12)) {
        UartBlockCrcRequest parsed{};
        unsigned block_size = 0;
        char trailing = '\0';
        const int fields = std::sscanf(
            line, "HLVBLOCKCRC 1 %48s %u %c", parsed.filename,
            &block_size, &trailing);
        if (fields != 2 || !validFilename(parsed.filename) ||
            block_size < kMinimumBlockCrcBytes ||
            block_size > kMaximumBlockCrcBytes ||
            (block_size & (block_size - 1U))) {
            reject("BAD_REQUEST");
            return false;
        }
        if (!requireSession("CRC32")) return false;
        parsed.block_size = block_size;
        block_crc_request_ = parsed;
        block_crc_requested_ = true;
        return false;
    }
    if (!std::strncmp(line, "HLVREAD ", 8)) {
        UartReadRequest parsed{};
        unsigned long offset = 0;
        unsigned long size = 0;
        unsigned baud = 0;
        char trailing = '\0';
        const int fields = std::sscanf(
            line, "HLVREAD 2 %48s %lu %lu %u %c", parsed.filename,
            &offset, &size, &baud, &trailing);
        if (fields != 4 || offset > UINT32_MAX || !size ||
            size > UINT32_MAX || !validFilename(parsed.filename) ||
            !supportedDataBaud(baud)) {
            reject("BAD_REQUEST");
            return false;
        }
        if (!requireSession("READ")) return false;
        parsed.offset = static_cast<uint32_t>(offset);
        parsed.size = static_cast<uint32_t>(size);
        parsed.data_baud = baud;
        read_request_ = parsed;
        read_requested_ = true;
        return false;
    }
    if (!std::strncmp(line, "HLVPATCH ", 9)) {
        UartPatchRequest parsed{};
        unsigned long offset = 0;
        unsigned long size = 0;
        unsigned crc = 0;
        unsigned baud = 0;
        char trailing = '\0';
        const int fields = std::sscanf(
            line, "HLVPATCH 1 %48s %lu %lu %x %u %c",
            parsed.filename, &offset, &size, &crc, &baud, &trailing);
        if (fields != 5 || offset > UINT32_MAX || !size ||
            size > UINT32_MAX || !validFilename(parsed.filename) ||
            !supportedDataBaud(baud)) {
            reject("BAD_REQUEST");
            return false;
        }
        if (!requireSession("PATCH")) return false;
        parsed.offset = static_cast<uint32_t>(offset);
        parsed.size = static_cast<uint32_t>(size);
        parsed.crc32 = static_cast<uint32_t>(crc);
        parsed.data_baud = baud;
        patch_request_ = parsed;
        patch_requested_ = true;
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
        if (!requireSession("DELETE")) return false;
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
        if (!requireSession("SDBENCH")) return false;
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
    if (!requireSession("UPLOAD")) return false;
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

bool UartFileUpload::takeSessionRequest(char *command,
                                        size_t command_bytes) {
    if (!session_requested_ || !command || !command_bytes) return false;
    std::snprintf(command, command_bytes, "%s", session_command_);
    session_command_[0] = '\0';
    session_requested_ = false;
    return true;
}

void UartFileUpload::sessionReady(const char *command) {
    std::snprintf(active_session_command_,
                  sizeof active_session_command_, "%s",
                  command ? command : "TRANSFER");
    session_active_ = true;
    monitoring_requested_ = false;
    writeResponse("HLVSESSIONREADY 1 %s\n",
                  command ? command : "TRANSFER");
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
}

bool UartFileUpload::takeMonitoringRequest() {
    const bool requested = monitoring_requested_;
    monitoring_requested_ = false;
    return requested;
}

void UartFileUpload::monitoringReady() {
    writeResponse("HLVMONITORREADY 1 ON\n");
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
    session_active_ = false;
    active_session_command_[0] = '\0';
}

bool UartFileUpload::takeReadRequest(UartReadRequest *request) {
    if (!request || !read_requested_) return false;
    *request = read_request_;
    read_request_ = {};
    read_requested_ = false;
    return true;
}

bool UartFileUpload::takePatchRequest(UartPatchRequest *request) {
    if (!request || !patch_requested_) return false;
    *request = patch_request_;
    patch_request_ = {};
    patch_requested_ = false;
    return true;
}

bool UartFileUpload::takeBlockCrcRequest(UartBlockCrcRequest *request) {
    if (!request || !block_crc_requested_) return false;
    *request = block_crc_request_;
    block_crc_request_ = {};
    block_crc_requested_ = false;
    return true;
}

bool UartFileUpload::takeBaudRequest(uint32_t *baud) {
    if (!baud || !control_baud_requested_) return false;
    *baud = requested_control_baud_;
    requested_control_baud_ = 0;
    control_baud_requested_ = false;
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

bool UartFileUpload::sendListPacket(uint32_t sequence, uint32_t file_size,
                                    const char *name) {
    uint8_t packet[kListPacketHeaderBytes + 255]{};
    const size_t name_bytes = name ? std::strlen(name) : 0;
    if (name_bytes > 255) return false;
    std::memcpy(packet, kListPacketMagic, sizeof kListPacketMagic);
    writeLe32(packet + 4, sequence);
    writeLe32(packet + 8, file_size);
    packet[12] = static_cast<uint8_t>(name_bytes);
    if (name_bytes) {
        std::memcpy(packet + kListPacketHeaderBytes, name, name_bytes);
    }
    uint32_t checksum = esp_rom_crc32_le(0, packet, 13);
    if (name_bytes) {
        checksum = esp_rom_crc32_le(
            checksum, packet + kListPacketHeaderBytes, name_bytes);
    }
    writeLe32(packet + 13, checksum);
    for (unsigned attempt = 0; attempt < kListPacketAttempts; ++attempt) {
        uint8_t acknowledgment[kReadAckBytes]{};
        const int written = uart_write_bytes(
            kUploadUart, packet, kListPacketHeaderBytes + name_bytes);
        if (written != static_cast<int>(
                           kListPacketHeaderBytes + name_bytes)) {
            continue;
        }
        if (!readExact(acknowledgment, sizeof acknowledgment,
                       kListAckTimeoutMs) ||
            std::memcmp(acknowledgment, "HLVA", 4) ||
            readLe32(acknowledgment + 4) != sequence ||
            readLe32(acknowledgment + 9) != crc32(acknowledgment, 9)) {
            continue;
        }
        if (acknowledgment[8] == 1) return true;
    }
    return false;
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
        if (!sendListPacket(count, static_cast<uint32_t>(status.st_size),
                            entry->d_name)) {
            closedir(handle);
            finishResponse("HLVERR 2 LIST_FAILED\n");
            return false;
        }
        ++count;
    }
    closedir(handle);
    if (!sendListPacket(count, count, nullptr)) {
        finishResponse("HLVERR 2 LIST_FAILED\n");
        return false;
    }
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
    return true;
}

bool UartFileUpload::checksumBlocks(
    const char *directory, const UartBlockCrcRequest &request) {
    char path[128];
    if (!directory ||
        !buildPath(path, sizeof path, directory, request.filename, "")) {
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

    const auto send_packet = [this](uint32_t sequence, uint32_t offset,
                                    uint32_t size, uint32_t block_crc) {
        uint8_t packet[kBlockCrcPacketBytes]{};
        std::memcpy(packet, kBlockCrcPacketMagic,
                    sizeof kBlockCrcPacketMagic);
        writeLe32(packet + 4, sequence);
        writeLe32(packet + 8, offset);
        writeLe32(packet + 12, size);
        writeLe32(packet + 16, block_crc);
        writeLe32(packet + 20, crc32(packet, 20));
        for (unsigned attempt = 0; attempt < kBlockCrcPacketAttempts;
             ++attempt) {
            uint8_t acknowledgment[kReadAckBytes]{};
            const int written = uart_write_bytes(
                kUploadUart, packet, sizeof packet);
            if (written != static_cast<int>(sizeof packet)) continue;
            if (!readExact(acknowledgment, sizeof acknowledgment,
                           kBlockCrcAckTimeoutMs)) continue;
            if (std::memcmp(acknowledgment, "HLVA", 4) ||
                readLe32(acknowledgment + 4) != sequence ||
                readLe32(acknowledgment + 9) !=
                    crc32(acknowledgment, 9)) {
                continue;
            }
            if (acknowledgment[8] == 1) return true;
        }
        return false;
    };

    uart_flush_input(kUploadUart);
    uint32_t file_crc = 0;
    uint32_t offset = 0;
    uint32_t sequence = 0;
    bool success = true;
    while (offset < file_size) {
        const uint32_t block_size = std::min(
            request.block_size, file_size - offset);
        uint32_t consumed = 0;
        uint32_t block_crc = 0;
        while (consumed < block_size) {
            const size_t wanted = std::min<size_t>(
                kCrcBufferBytes, block_size - consumed);
            const size_t received = std::fread(buffer, 1, wanted, input);
            if (received != wanted) {
                success = false;
                break;
            }
            block_crc = esp_rom_crc32_le(block_crc, buffer, received);
            file_crc = esp_rom_crc32_le(file_crc, buffer, received);
            consumed += static_cast<uint32_t>(received);
        }
        if (!success ||
            !send_packet(sequence, offset, block_size, block_crc)) {
            success = false;
            break;
        }
        offset += block_size;
        ++sequence;
    }
    if (success && !send_packet(sequence, file_size, 0, file_crc)) {
        success = false;
    }
    if (std::fclose(input)) success = false;
    heap_caps_free(buffer);
    if (!success) {
        finishResponse("HLVERR 1 BLOCK_CRC_FAILED\n");
        return false;
    }
    (void)rewriteCachedCrc(directory, request.filename, true,
                           file_size, file_crc);
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
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

bool UartFileUpload::readFile(const char *directory,
                              const UartReadRequest &request) {
    char path[128];
    if (!directory ||
        !buildPath(path, sizeof path, directory, request.filename, "")) {
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
    if (request.offset > file_size) {
        reject("BAD_RANGE");
        return false;
    }
    uint32_t remaining = file_size - request.offset;
    remaining = std::min(remaining, request.size);
    FILE *input = std::fopen(path, "rb");
    if (!input) {
        reject("OPEN_FAILED");
        return false;
    }
    if (std::fseek(input, static_cast<long>(request.offset), SEEK_SET)) {
        std::fclose(input);
        reject("SEEK_FAILED");
        return false;
    }
    auto *packet = static_cast<uint8_t *>(heap_caps_malloc(
        kReadBlockHeaderBytes + kReadBlockBytes, MALLOC_CAP_8BIT));
    if (!packet) {
        std::fclose(input);
        reject("NO_MEMORY");
        return false;
    }

    uart_flush_input(kUploadUart);
    uint8_t ready[kReadReadyBytes];
    std::memcpy(ready, "HLVR", 4);
    writeLe32(ready + 4, file_size);
    writeLe32(ready + 8, request.offset);
    writeLe32(ready + 12, remaining);
    writeLe32(ready + 16, request.data_baud);
    writeLe32(ready + 20, crc32(ready, 20));
    for (unsigned attempt = 0; attempt < kReadBlockAttempts; ++attempt) {
        uart_write_bytes(kUploadUart, ready, sizeof ready);
    }
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
    if (request.data_baud != control_baud_) {
        uint8_t go[8];
        constexpr uint8_t kGo[8] = {
            'H', 'L', 'V', 'G', 'O', ' ', '2', '\n'};
        if (!setBaud(request.data_baud)) {
            heap_caps_free(packet);
            std::fclose(input);
            finishResponse("HLVERR 2 BAUD_FAILED\n");
            return false;
        }
        if (!readExact(go, sizeof go, kChunkTimeoutMs) ||
            std::memcmp(go, kGo, sizeof kGo)) {
            heap_caps_free(packet);
            std::fclose(input);
            setBaud(control_baud_);
            vTaskDelay(pdMS_TO_TICKS(50));
            finishResponse("HLVERR 2 HANDSHAKE_FAILED\n");
            return false;
        }
    }

    std::memcpy(packet, kReadBlockMagic, sizeof kReadBlockMagic);
    uint32_t sent = 0;
    uint32_t sequence = 0;
    uint32_t range_crc = 0;
    bool success = true;
    while (sent < remaining) {
        const uint32_t bytes = std::min<uint32_t>(
            remaining - sent, kReadBlockBytes);
        if (std::fread(packet + kReadBlockHeaderBytes, 1, bytes, input) !=
            bytes) {
            success = false;
            break;
        }
        const uint32_t block_crc = crc32(
            packet + kReadBlockHeaderBytes, bytes);
        range_crc = esp_rom_crc32_le(
            range_crc, packet + kReadBlockHeaderBytes, bytes);
        writeLe32(packet + 4, sequence);
        writeLe16(packet + 8, static_cast<uint16_t>(bytes));
        writeLe32(packet + 10, block_crc);
        bool acknowledged = false;
        for (unsigned attempt = 0; attempt < kReadDataAttempts; ++attempt) {
            uint8_t acknowledgment[kReadAckBytes];
            const int written = uart_write_bytes(
                kUploadUart, packet, kReadBlockHeaderBytes + bytes);
            if (written !=
                static_cast<int>(kReadBlockHeaderBytes + bytes)) {
                continue;
            }
            if (uart_wait_tx_done(
                    kUploadUart, pdMS_TO_TICKS(kChunkTimeoutMs)) != ESP_OK) {
                continue;
            }
            if (!readExact(acknowledgment, sizeof acknowledgment,
                           kReadAckTimeoutMs) ||
                std::memcmp(acknowledgment, "HLVA", 4) ||
                readLe32(acknowledgment + 4) != sequence ||
                readLe32(acknowledgment + 9) !=
                    crc32(acknowledgment, 9)) {
                continue;
            }
            if (acknowledgment[8] == 1) {
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
    if (std::fclose(input)) success = false;
    heap_caps_free(packet);
    if (!success || sent != remaining) {
        setBaud(control_baud_);
        vTaskDelay(pdMS_TO_TICKS(50));
        finishResponse("HLVERR 2 READ_FAILED\n");
        return false;
    }
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
    setBaud(control_baud_);
    vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t done[kReadDoneBytes];
    std::memcpy(done, "HLVE", 4);
    writeLe32(done + 4, sent);
    writeLe32(done + 8, range_crc);
    writeLe32(done + 12, crc32(done, 12));
    for (unsigned attempt = 0; attempt < kReadBlockAttempts; ++attempt) {
        uart_write_bytes(kUploadUart, done, sizeof done);
    }
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
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

bool UartFileUpload::patchFile(
        const char *directory, const UartPatchRequest &request) {
    char target_path[128];
    char patch_path[128];
    char backup_path[128];
    struct stat status{};
    FILE *patch = nullptr;
    FILE *target = nullptr;
    FILE *backup = nullptr;
    uint8_t block[kPatchChunkBytes];
    uint32_t received = 0;
    uint32_t sequence = 0;
    uint32_t patch_crc = 0;
    uint32_t original_crc = 0;
    uint32_t verified_crc = 0;
    const char *failure = "PATCH_FAILED";
    bool backup_ready = false;
    bool applied = false;
    bool restored = false;

    if (!directory ||
        !buildPath(target_path, sizeof target_path, directory,
                   request.filename, "") ||
        !buildPath(patch_path, sizeof patch_path, directory,
                   request.filename, ".patch") ||
        !buildPath(backup_path, sizeof backup_path, directory,
                   request.filename, ".patchbak")) {
        reject("PATH_TOO_LONG");
        return false;
    }
    if (stat(target_path, &status) || status.st_size < 0 ||
        static_cast<uint64_t>(status.st_size) > UINT32_MAX) {
        reject("OPEN_FAILED");
        return false;
    }
    if (request.offset > static_cast<uint32_t>(status.st_size) ||
        request.size > static_cast<uint32_t>(status.st_size) -
                           request.offset) {
        reject("RANGE");
        return false;
    }

    unlink(patch_path);
    unlink(backup_path);
    patch = std::fopen(patch_path, "wb");
    if (!patch) {
        reject("OPEN_FAILED");
        return false;
    }

    uart_flush_input(kUploadUart);
    for (unsigned attempt = 0; attempt < kReadBlockAttempts; ++attempt) {
        writeResponse("HLVPATCHREADY 1 %u %u\n",
                      static_cast<unsigned>(kPatchChunkBytes),
                      static_cast<unsigned>(request.data_baud));
    }
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
    if (request.data_baud != control_baud_) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (!setBaud(request.data_baud)) {
            failure = "BAUD_FAILED";
            goto receive_failed;
        }
    }

    while (received < request.size) {
        uint8_t header[kBlockHeaderBytes];
        if (!readExact(header, sizeof header, kChunkTimeoutMs)) {
            failure = "TIMEOUT";
            goto receive_failed;
        }
        const uint32_t block_sequence = readLe32(header + 4);
        const uint16_t block_bytes = readLe16(header + 8);
        const uint32_t block_crc = readLe32(header + 10);
        if (std::memcmp(header, kPatchBlockMagic,
                        sizeof kPatchBlockMagic) ||
            !block_bytes || block_bytes > kPatchChunkBytes ||
            block_bytes > request.size - received) {
            failure = "BAD_BLOCK";
            goto receive_failed;
        }
        if (!readExact(block, block_bytes, kChunkTimeoutMs)) {
            failure = "TIMEOUT";
            goto receive_failed;
        }
        if (block_sequence != sequence) {
            if (block_sequence < sequence && sequence) {
                writeResponse("HLVPATCHACK %u %u\n",
                              static_cast<unsigned>(sequence - 1),
                              static_cast<unsigned>(received));
            } else {
                writeResponse("HLVPATCHNAK %u ORDER\n",
                              static_cast<unsigned>(sequence));
            }
            continue;
        }
        if (crc32(block, block_bytes) != block_crc) {
            writeResponse("HLVPATCHNAK %u CRC\n",
                          static_cast<unsigned>(sequence));
            continue;
        }
        if (std::fwrite(block, 1, block_bytes, patch) != block_bytes) {
            failure = "WRITE_FAILED";
            goto receive_failed;
        }
        patch_crc = esp_rom_crc32_le(patch_crc, block, block_bytes);
        received += block_bytes;
        writeResponse("HLVPATCHACK %u %u\n",
                      static_cast<unsigned>(sequence),
                      static_cast<unsigned>(received));
        ++sequence;
    }
    if (patch_crc != request.crc32) {
        failure = "PATCH_CRC";
        goto receive_failed;
    }
    if (!syncFile(patch)) {
        std::fclose(patch);
        patch = nullptr;
        failure = "FLUSH_FAILED";
        goto receive_failed;
    }
    if (std::fclose(patch)) {
        patch = nullptr;
        failure = "CLOSE_FAILED";
        goto receive_failed;
    }
    patch = nullptr;

    target = std::fopen(target_path, "rb");
    backup = std::fopen(backup_path, "wb");
    if (!target || !backup ||
        std::fseek(target, static_cast<long>(request.offset), SEEK_SET) ||
        !copyFileBytes(target, backup, request.size, &original_crc) ||
        !syncFile(backup)) {
        failure = "BACKUP_FAILED";
        goto apply_failed;
    }
    if (std::fclose(backup)) {
        backup = nullptr;
        failure = "BACKUP_FAILED";
        goto apply_failed;
    }
    backup = nullptr;
    if (std::fclose(target)) {
        target = nullptr;
        failure = "BACKUP_FAILED";
        goto apply_failed;
    }
    target = nullptr;
    backup_ready = true;

    target = std::fopen(target_path, "r+b");
    patch = std::fopen(patch_path, "rb");
    if (!target || !patch ||
        std::fseek(target, static_cast<long>(request.offset), SEEK_SET) ||
        !copyFileBytes(patch, target, request.size, nullptr) ||
        !syncFile(target) ||
        std::fseek(target, static_cast<long>(request.offset), SEEK_SET) ||
        !copyFileBytes(target, nullptr, request.size, &verified_crc) ||
        verified_crc != request.crc32) {
        failure = "VERIFY_FAILED";
        goto apply_failed;
    }
    applied = true;

apply_failed:
    if (!applied && backup_ready) {
        if (patch) {
            std::fclose(patch);
            patch = nullptr;
        }
        if (backup) {
            std::fclose(backup);
            backup = nullptr;
        }
        if (!target) target = std::fopen(target_path, "r+b");
        backup = std::fopen(backup_path, "rb");
        if (target && backup &&
            !std::fseek(target, static_cast<long>(request.offset), SEEK_SET) &&
            copyFileBytes(backup, target, request.size, nullptr) &&
            syncFile(target) &&
            !std::fseek(target, static_cast<long>(request.offset), SEEK_SET) &&
            copyFileBytes(target, nullptr, request.size, &verified_crc) &&
            verified_crc == original_crc) {
            restored = true;
        }
    }
    if (backup) std::fclose(backup);
    if (patch) std::fclose(patch);
    if (target) std::fclose(target);
    unlink(patch_path);
    if (applied || restored) unlink(backup_path);
    if (!applied) {
        finishResponse("HLVERR 1 %s%s\n", failure,
                       restored ? "_RESTORED" : "");
        return false;
    }

    rewriteCachedCrc(directory, request.filename, false, 0, 0);
    for (unsigned attempt = 1; attempt < kReadBlockAttempts; ++attempt) {
        writeResponse("HLVPATCHDONE 1 %u %u %08x %s\n",
                      static_cast<unsigned>(request.offset),
                      static_cast<unsigned>(request.size),
                      static_cast<unsigned>(request.crc32),
                      request.filename);
    }
    finishResponse("HLVPATCHDONE 1 %u %u %08x %s\n",
                   static_cast<unsigned>(request.offset),
                   static_cast<unsigned>(request.size),
                   static_cast<unsigned>(request.crc32),
                   request.filename);
    return true;

receive_failed:
    if (patch) std::fclose(patch);
    unlink(patch_path);
    finishResponse("HLVERR 1 %s\n", failure);
    return false;
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
    return uart_set_baudrate(kUploadUart, calibratedBaud(baud)) == ESP_OK;
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

bool UartFileUpload::changeBaud(uint32_t baud) {
    constexpr uint8_t kSwitch[16] = {
        'H', 'L', 'V', 'B', 'A', 'U', 'D', 'S',
        'W', 'I', 'T', 'C', 'H', ' ', '1', '\n'};
    uint8_t request[sizeof kSwitch];
    const uint32_t previous_baud = control_baud_;

    if (!supportedDataBaud(baud)) {
        reject("BAD_REQUEST");
        return false;
    }
    uart_flush_input(kUploadUart);
    for (unsigned attempt = 0; attempt < kReadBlockAttempts; ++attempt) {
        writeResponse("HLVBAUDREADY 1 %u\n",
                      static_cast<unsigned>(baud));
    }
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
    if (!readExact(request, sizeof kSwitch, kChunkTimeoutMs) ||
        std::memcmp(request, kSwitch, sizeof kSwitch)) {
        finishResponse("HLVERR 1 BAUD_HANDSHAKE\n");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!setBaud(baud)) {
        finishResponse("HLVERR 1 BAUD_FAILED\n");
        return false;
    }
    bool valid_frame = false;
    for (unsigned attempt = 0; attempt < kReadBlockAttempts; ++attempt) {
        if (readExact(request, 12, 2000) &&
            !std::memcmp(request, "HLVG", 4) &&
            readLe32(request + 4) == baud &&
            readLe32(request + 8) == crc32(request, 8)) {
            valid_frame = true;
            break;
        }
    }
    if (!valid_frame) {
        setBaud(previous_baud);
        vTaskDelay(pdMS_TO_TICKS(50));
        finishResponse("HLVERR 1 BAUD_HANDSHAKE\n");
        return false;
    }
    std::memcpy(request, "HLVA", 4);
    writeLe32(request + 4, baud);
    writeLe32(request + 8, crc32(request, 8));
    for (unsigned attempt = 0; attempt < kReadBlockAttempts; ++attempt) {
        uart_write_bytes(kUploadUart, request, 12);
    }
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));

    valid_frame = false;
    for (unsigned attempt = 0; attempt < kReadBlockAttempts; ++attempt) {
        if (readExact(request, 12, 2000) &&
            !std::memcmp(request, "HLVD", 4) &&
            readLe32(request + 4) == baud &&
            readLe32(request + 8) == crc32(request, 8)) {
            valid_frame = true;
            break;
        }
    }
    if (!valid_frame) {
        setBaud(previous_baud);
        vTaskDelay(pdMS_TO_TICKS(50));
        finishResponse("HLVERR 1 BAUD_HANDSHAKE\n");
        return false;
    }
    uart_flush_input(kUploadUart);
    control_baud_ = baud;
    std::memcpy(request, "HLVF", 4);
    writeLe32(request + 4, baud);
    writeLe32(request + 8, crc32(request, 8));
    for (unsigned attempt = 0; attempt < kReadBlockAttempts; ++attempt) {
        uart_write_bytes(kUploadUart, request, 12);
    }
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
    return true;
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
    for (size_t attempt = 0; attempt < kReadBlockAttempts; ++attempt) {
        writeResponse("HLVREADY 2 %u %u %u\n",
                      static_cast<unsigned>(kChunkBytes),
                      static_cast<unsigned>(request.data_baud),
                      static_cast<unsigned>(kBufferCount));
    }
    uart_wait_tx_done(kUploadUart, pdMS_TO_TICKS(1000));
    if (request.data_baud != control_baud_) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (request.data_baud != control_baud_ &&
        !setBaud(request.data_baud)) {
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
    if (success) {
        uint32_t verified_file_crc = 0;
        FILE *verification = std::fopen(temporary, "rb");
        bool verification_success = verification != nullptr;
        if (verification_success) {
            verification_success = copyFileBytes(
                verification, nullptr, request.size, &verified_file_crc);
            if (std::fclose(verification)) verification_success = false;
        }
        if (!verification_success || verified_file_crc != request.crc32) {
            failure = "FILE_VERIFY_CRC";
            success = false;
        }
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
    for (size_t attempt = 1; attempt < kReadBlockAttempts; ++attempt) {
        writeResponse("HLVDONE 2 %u %08x %s\n",
                      static_cast<unsigned>(request.size),
                      static_cast<unsigned>(actual_crc), request.filename);
    }
    finishResponse("HLVDONE 2 %u %08x %s\n",
                   static_cast<unsigned>(request.size),
                   static_cast<unsigned>(actual_crc), request.filename);
    return true;
}
