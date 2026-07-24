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

class UartFileUpload {
public:
    static constexpr size_t kChunkBytes = 61440;
    using ProgressCallback =
        void (*)(uint32_t received, uint32_t total, void *context);

    esp_err_t begin(uint32_t control_baud);
    bool pollRequest(UartUploadRequest *request);
    bool takeListRequest();
    bool listDirectory(const char *directory);
    bool receive(const UartUploadRequest &request, const char *directory,
                 char *stored_path, size_t stored_path_bytes,
                 ProgressCallback progress = nullptr,
                 void *progress_context = nullptr);
    void reject(const char *reason);

private:
    static constexpr size_t kLineBytes = 128;

    bool parseRequest(const char *line, UartUploadRequest *request);
    bool setBaud(uint32_t baud);
    bool readExact(uint8_t *destination, size_t bytes, uint32_t timeout_ms);
    void writeResponse(const char *format, ...);
    void finishResponse(const char *format, ...);

    uint32_t control_baud_ = 0;
    char line_[kLineBytes]{};
    size_t line_size_ = 0;
    bool ready_ = false;
    bool list_requested_ = false;
};
