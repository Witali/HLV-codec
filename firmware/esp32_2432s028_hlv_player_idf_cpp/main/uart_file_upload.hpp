#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

struct UartUploadRequest {
    static constexpr size_t kMaximumFilenameBytes = 48;

    char filename[kMaximumFilenameBytes + 1]{};
    uint32_t size = 0;
    uint32_t crc32 = 0;
    uint32_t data_baud = 0;
};

struct UartReadRequest {
    char filename[UartUploadRequest::kMaximumFilenameBytes + 1]{};
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t data_baud = 0;
};

enum class SdBenchmarkPattern : uint8_t {
    kZeros,
    kPseudoRandom,
};

struct SdBenchmarkRequest {
    SdBenchmarkPattern pattern = SdBenchmarkPattern::kZeros;
    uint32_t size_mib = 0;
};

class UartFileUpload {
public:
    static constexpr size_t kChunkBytes = 64;
    static constexpr size_t kBufferCount = 2;
    using ProgressCallback =
        void (*)(uint32_t received, uint32_t total, void *context);

    esp_err_t begin(uint32_t control_baud);
    bool pollRequest(UartUploadRequest *request);
    bool takeSessionRequest(char *command, size_t command_bytes);
    void sessionReady(const char *command);
    bool takeMonitoringRequest();
    void monitoringReady();
    bool takeListRequest();
    bool takeCrcRequest(char *filename, size_t filename_bytes);
    bool takeReadRequest(UartReadRequest *request);
    bool takeBaudRequest(uint32_t *baud);
    bool takeDeleteRequest(char *filename, size_t filename_bytes);
    bool takeSdBenchmarkRequest(SdBenchmarkRequest *request);
    bool listDirectory(const char *directory);
    bool checksumFile(const char *directory, const char *filename);
    bool readFile(const char *directory, const UartReadRequest &request);
    bool changeBaud(uint32_t baud);
    bool deleteFile(const char *directory, const char *filename);
    bool benchmarkSd(const char *directory,
                     const SdBenchmarkRequest &request);
    bool receive(const UartUploadRequest &request, const char *directory,
                 char *stored_path, size_t stored_path_bytes,
                 ProgressCallback progress = nullptr,
                 void *progress_context = nullptr);
    void reject(const char *reason);

private:
    static constexpr size_t kLineBytes = 128;

    bool parseRequest(const char *line, UartUploadRequest *request);
    bool requireSession(const char *command);
    bool setBaud(uint32_t baud);
    bool readExact(uint8_t *destination, size_t bytes, uint32_t timeout_ms);
    bool sendListPacket(uint32_t sequence, uint32_t file_size,
                        const char *name);
    void writeResponse(const char *format, ...);
    void finishResponse(const char *format, ...);

    uint32_t control_baud_ = 0;
    char line_[kLineBytes]{};
    size_t line_size_ = 0;
    bool ready_ = false;
    char session_command_[16]{};
    bool session_requested_ = false;
    char active_session_command_[16]{};
    bool session_active_ = false;
    bool monitoring_requested_ = false;
    bool list_requested_ = false;
    char crc_filename_[UartUploadRequest::kMaximumFilenameBytes + 1]{};
    bool crc_requested_ = false;
    UartReadRequest read_request_{};
    bool read_requested_ = false;
    uint32_t requested_control_baud_ = 0;
    bool control_baud_requested_ = false;
    char delete_filename_[UartUploadRequest::kMaximumFilenameBytes + 1]{};
    bool delete_requested_ = false;
    SdBenchmarkRequest sd_benchmark_request_{};
    bool sd_benchmark_requested_ = false;
};
