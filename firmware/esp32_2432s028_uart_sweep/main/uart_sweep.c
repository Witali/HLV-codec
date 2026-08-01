#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SWEEP_UART UART_NUM_0
#define HOST_CONTROL_BAUD 1000000U
#define ESP_CONTROL_BAUD 978593U
#define PROTOCOL_VERSION 1U
#define FRAME_HEADER_BYTES 12U
#define MAX_PAYLOAD_BYTES 512U
#define MAX_FRAME_BYTES (FRAME_HEADER_BYTES + MAX_PAYLOAD_BYTES)
#define LINE_BYTES 160U
#define SWITCH_DELAY_MS 250U
#define RX_FRAME_TIMEOUT_MS 150U
#define SWEEP_TX_GPIO 1
#define SWEEP_RX_GPIO 3
#define CONTROL_FRAME_REPEATS 20U
#define PERSIST_MAGIC 0x53575031U

static const uint32_t candidates_2m[] = {
    2000000U, 2003130U, 2006270U, 2009419U, 2012579U,
    2015748U, 2018927U, 2022117U, 2025316U,
};

static const uint32_t candidates_3m[] = {
    3000000U, 2976744U, 2962963U, 2949309U, 2935780U, 2929062U,
    2922374U, 2915718U, 2909091U, 2902494U, 2895928U,
};

typedef struct {
    uint32_t nominal;
    uint32_t actual;
    uint32_t tx_valid;
    uint32_t tx_errors;
    uint32_t rx_valid;
    uint32_t rx_errors;
} sweep_result_t;

typedef struct {
    uint32_t magic;
    uint32_t active;
    uint32_t complete;
    uint32_t rate_mask;
    uint32_t blocks;
    uint32_t payload_bytes;
    uint32_t nominal;
    uint32_t candidate_index;
    uint32_t have_best_2m;
    uint32_t have_best_3m;
    sweep_result_t best_2m;
    sweep_result_t best_3m;
    uint32_t crc32;
} persistent_state_t;

RTC_NOINIT_ATTR static persistent_state_t persistent;

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size) {
    size_t index;
    crc = ~crc;
    for (index = 0; index < size; ++index) {
        unsigned bit;
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static void write_le16(uint8_t *target, uint16_t value) {
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *target, uint32_t value) {
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
    target[2] = (uint8_t)(value >> 16);
    target[3] = (uint8_t)(value >> 24);
}

static uint16_t read_le16(const uint8_t *source) {
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8);
}

static uint32_t read_le32(const uint8_t *source) {
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static bool write_all(const uint8_t *data, size_t size) {
    size_t written = 0;
    while (written < size) {
        int count = uart_write_bytes(SWEEP_UART, data + written, size - written);
        if (count <= 0) return false;
        written += (size_t)count;
    }
    return true;
}

static void write_line(const char *line) {
    write_all((const uint8_t *)line, strlen(line));
    uart_wait_tx_done(SWEEP_UART, pdMS_TO_TICKS(1000));
}

static bool read_line(char *line, size_t capacity, uint32_t timeout_ms) {
    size_t used = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        uint8_t byte;
        int count = uart_read_bytes(SWEEP_UART, &byte, 1, pdMS_TO_TICKS(20));
        if (count != 1) continue;
        if (byte == '\r') continue;
        if (byte == '\n') {
            if (used == 0) continue;
            line[used] = '\0';
            return true;
        }
        if (used + 1 < capacity) line[used++] = (char)byte;
    }
    return false;
}

static bool read_exact(uint8_t *data, size_t size, uint32_t timeout_ms) {
    size_t received = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (received < size && esp_timer_get_time() < deadline) {
        int count = uart_read_bytes(
            SWEEP_UART, data + received, size - received, pdMS_TO_TICKS(20));
        if (count > 0) received += (size_t)count;
    }
    return received == size;
}

static bool find_magic(const uint8_t magic[4], uint32_t timeout_ms) {
    uint8_t window[4] = {0};
    size_t used = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        uint8_t byte;
        if (uart_read_bytes(SWEEP_UART, &byte, 1, pdMS_TO_TICKS(20)) != 1) {
            continue;
        }
        if (used < sizeof(window)) {
            window[used++] = byte;
        } else {
            memmove(window, window + 1, sizeof(window) - 1);
            window[sizeof(window) - 1] = byte;
        }
        if (used == sizeof(window) && memcmp(window, magic, 4) == 0) return true;
    }
    return false;
}

static bool set_rate(uint32_t rate) {
    return uart_wait_tx_done(SWEEP_UART, pdMS_TO_TICKS(1000)) == ESP_OK &&
           uart_set_baudrate(SWEEP_UART, rate) == ESP_OK;
}

static void make_payload(uint8_t *payload, size_t size, uint16_t sequence,
                         uint32_t actual) {
    size_t index;
    for (index = 0; index < size; ++index) {
        payload[index] = (uint8_t)(sequence * 131U + (uint32_t)index * 17U +
                                   actual + ((uint32_t)index >> 3));
    }
}

static size_t make_frame(uint8_t *frame, uint16_t sequence,
                         uint16_t payload_bytes, uint32_t actual) {
    memcpy(frame, "SWPB", 4);
    write_le16(frame + 4, sequence);
    write_le16(frame + 6, payload_bytes);
    make_payload(frame + FRAME_HEADER_BYTES, payload_bytes, sequence, actual);
    write_le32(frame + 8, crc32_update(
        0, frame + FRAME_HEADER_BYTES, payload_bytes));
    return FRAME_HEADER_BYTES + payload_bytes;
}

static bool valid_frame(const uint8_t *frame, uint16_t sequence,
                        uint16_t payload_bytes, uint32_t actual) {
    uint8_t expected[MAX_PAYLOAD_BYTES];
    if (memcmp(frame, "SWPB", 4) != 0 || read_le16(frame + 4) != sequence ||
        read_le16(frame + 6) != payload_bytes ||
        read_le32(frame + 8) != crc32_update(
            0, frame + FRAME_HEADER_BYTES, payload_bytes)) {
        return false;
    }
    make_payload(expected, payload_bytes, sequence, actual);
    return memcmp(expected, frame + FRAME_HEADER_BYTES, payload_bytes) == 0;
}

static uint32_t result_errors(const sweep_result_t *result) {
    return result->tx_errors + result->rx_errors;
}

static uint32_t result_valid(const sweep_result_t *result) {
    return result->tx_valid + result->rx_valid;
}

static bool result_better(const sweep_result_t *candidate,
                          const sweep_result_t *best) {
    uint32_t candidate_distance;
    uint32_t best_distance;
    if (result_errors(candidate) != result_errors(best)) {
        return result_errors(candidate) < result_errors(best);
    }
    if (result_valid(candidate) != result_valid(best)) {
        return result_valid(candidate) > result_valid(best);
    }
    candidate_distance = candidate->actual > candidate->nominal
        ? candidate->actual - candidate->nominal
        : candidate->nominal - candidate->actual;
    best_distance = best->actual > best->nominal
        ? best->actual - best->nominal
        : best->nominal - best->actual;
    return candidate_distance < best_distance;
}

static void save_state(void) {
    persistent.crc32 = 0;
    persistent.crc32 = crc32_update(
        0, (const uint8_t *)&persistent, sizeof(persistent));
}

static bool state_valid(void) {
    uint32_t stored;
    uint32_t calculated;
    if (persistent.magic != PERSIST_MAGIC) return false;
    stored = persistent.crc32;
    persistent.crc32 = 0;
    calculated = crc32_update(
        0, (const uint8_t *)&persistent, sizeof(persistent));
    persistent.crc32 = stored;
    return stored == calculated;
}

static void clear_state(void) {
    memset(&persistent, 0, sizeof(persistent));
}

static const uint32_t *candidate_table(uint32_t nominal, size_t *count) {
    if (nominal == 2000000U) {
        *count = sizeof(candidates_2m) / sizeof(candidates_2m[0]);
        return candidates_2m;
    }
    *count = sizeof(candidates_3m) / sizeof(candidates_3m[0]);
    return candidates_3m;
}

static void select_first_rate(void) {
    persistent.nominal = (persistent.rate_mask & 1U) != 0
        ? 2000000U : 3000000U;
    persistent.candidate_index = 0;
}

static void advance_state(const sweep_result_t *result) {
    sweep_result_t *best;
    uint32_t *have_best;
    size_t count;
    candidate_table(persistent.nominal, &count);
    if (persistent.nominal == 2000000U) {
        best = &persistent.best_2m;
        have_best = &persistent.have_best_2m;
    } else {
        best = &persistent.best_3m;
        have_best = &persistent.have_best_3m;
    }
    if (*have_best == 0 || result_better(result, best)) {
        *best = *result;
        *have_best = 1;
    }
    ++persistent.candidate_index;
    if (persistent.candidate_index >= count) {
        if (persistent.nominal == 2000000U &&
            (persistent.rate_mask & 2U) != 0) {
            persistent.nominal = 3000000U;
            persistent.candidate_index = 0;
        } else {
            persistent.complete = 1;
        }
    }
    save_state();
}

static bool receive_host_report(uint32_t nominal, uint32_t actual,
                                sweep_result_t *result) {
    uint8_t frame[24];
    unsigned attempt;
    for (attempt = 0; attempt < CONTROL_FRAME_REPEATS; ++attempt) {
        if (!find_magic((const uint8_t *)"SWPH", 300)) continue;
        memcpy(frame, "SWPH", 4);
        if (!read_exact(frame + 4, sizeof(frame) - 4, 300)) continue;
        if (read_le32(frame + 4) == nominal &&
            read_le32(frame + 8) == actual &&
            read_le32(frame + 20) == crc32_update(0, frame, 20)) {
            result->tx_valid = read_le32(frame + 12);
            result->tx_errors = read_le32(frame + 16);
            /* Let the short repeated report finish before the caller clears
             * the RX FIFO.  Otherwise its tail becomes the next data frame. */
            vTaskDelay(pdMS_TO_TICKS(20));
            return true;
        }
    }
    result->tx_valid = 0;
    result->tx_errors = persistent.blocks;
    return false;
}

static void send_control_frame(const uint8_t *frame, size_t size) {
    unsigned repeat;
    for (repeat = 0; repeat < CONTROL_FRAME_REPEATS; ++repeat) {
        write_all(frame, size);
        uart_wait_tx_done(SWEEP_UART, pdMS_TO_TICKS(1000));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void send_rx_ready(uint32_t nominal, uint32_t actual) {
    uint8_t frame[24];
    memcpy(frame, "SWPX", 4);
    write_le32(frame + 4, nominal);
    write_le32(frame + 8, actual);
    write_le32(frame + 12, persistent.blocks);
    write_le32(frame + 16, persistent.payload_bytes);
    write_le32(frame + 20, crc32_update(0, frame, 20));
    send_control_frame(frame, sizeof(frame));
    /* The host waits for this final marker, so it cannot start sending while
     * the repeated readiness frames are still in flight. */
    memcpy(frame, "SWPG", 4);
    write_le32(frame + 20, crc32_update(0, frame, 20));
    write_all(frame, sizeof(frame));
    uart_wait_tx_done(SWEEP_UART, pdMS_TO_TICKS(1000));
}

static void send_result(const sweep_result_t *result) {
    uint8_t frame[32];
    memcpy(frame, "SWPR", 4);
    write_le32(frame + 4, result->nominal);
    write_le32(frame + 8, result->actual);
    write_le32(frame + 12, result->tx_valid);
    write_le32(frame + 16, result->tx_errors);
    write_le32(frame + 20, result->rx_valid);
    write_le32(frame + 24, result->rx_errors);
    write_le32(frame + 28, crc32_update(0, frame, 28));
    send_control_frame(frame, sizeof(frame));
}

static void run_current_candidate(void) {
    size_t candidate_count;
    const uint32_t *candidates = candidate_table(
        persistent.nominal, &candidate_count);
    uint32_t actual;
    uint16_t sequence;
    uint8_t frame[MAX_FRAME_BYTES];
    size_t frame_bytes;
    sweep_result_t result;
    char line[LINE_BYTES];
    if (persistent.candidate_index >= candidate_count) {
        persistent.complete = 1;
        save_state();
        esp_restart();
    }
    actual = candidates[persistent.candidate_index];
    memset(&result, 0, sizeof(result));
    result.nominal = persistent.nominal;
    result.actual = actual;
    frame_bytes = FRAME_HEADER_BYTES + persistent.payload_bytes;

    snprintf(line, sizeof(line),
             "SWEEP CANDIDATE 1 %u %u %u %u %u\n",
             (unsigned)persistent.nominal, (unsigned)actual,
             (unsigned)persistent.blocks, (unsigned)persistent.payload_bytes,
             SWITCH_DELAY_MS);
    write_line(line);
    vTaskDelay(pdMS_TO_TICKS(SWITCH_DELAY_MS));
    if (!set_rate(actual)) esp_restart();

    for (sequence = 0; sequence < persistent.blocks; ++sequence) {
        make_frame(frame, sequence, (uint16_t)persistent.payload_bytes, actual);
        write_all(frame, frame_bytes);
    }
    uart_wait_tx_done(SWEEP_UART, pdMS_TO_TICKS(2000));
    receive_host_report(persistent.nominal, actual, &result);

    uart_flush_input(SWEEP_UART);
    send_rx_ready(persistent.nominal, actual);
    for (sequence = 0; sequence < persistent.blocks; ++sequence) {
        if (read_exact(frame, frame_bytes, RX_FRAME_TIMEOUT_MS) &&
            valid_frame(frame, sequence,
                        (uint16_t)persistent.payload_bytes, actual)) {
            ++result.rx_valid;
        } else {
            ++result.rx_errors;
            if (sequence + 1U < persistent.blocks) {
                result.rx_errors += persistent.blocks - sequence - 1U;
            }
            break;
        }
    }
    uart_flush_input(SWEEP_UART);
    advance_state(&result);
    send_result(&result);
    uart_wait_tx_done(SWEEP_UART, pdMS_TO_TICKS(2000));
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void print_best(const sweep_result_t *best) {
    char line[LINE_BYTES];
    snprintf(line, sizeof(line), "SWEEP BEST 1 %u %u %u %u\n",
             (unsigned)best->nominal, (unsigned)best->actual,
             (unsigned)result_errors(best),
             result_errors(best) == 0 ? 1U : 0U);
    write_line(line);
}

void app_main(void) {
    uart_config_t config = {
        .baud_rate = (int)ESP_CONTROL_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    char line[LINE_BYTES];
    ESP_ERROR_CHECK(uart_driver_install(SWEEP_UART, 4096, 4096, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(SWEEP_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(SWEEP_UART, SWEEP_TX_GPIO, SWEEP_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    uart_flush_input(SWEEP_UART);
    if (!state_valid()) clear_state();

    for (;;) {
        const char *status = persistent.active == 0 ? "IDLE" :
            (persistent.complete != 0 ? "COMPLETE" : "ACTIVE");
        snprintf(line, sizeof(line), "SWEEP READY 1 1000000 %s\n", status);
        write_line(line);
        if (persistent.active != 0 && persistent.complete != 0) {
            if (persistent.have_best_2m != 0) print_best(&persistent.best_2m);
            if (persistent.have_best_3m != 0) print_best(&persistent.best_3m);
            write_line("SWEEP COMPLETE 1\n");
        }
        if (!read_line(line, sizeof(line), 1000)) continue;
        if (strcmp(line, "SWEEP ACK 1") == 0 && persistent.complete != 0) {
            clear_state();
            continue;
        }
        if (strcmp(line, "SWEEP CONTINUE 1") == 0 &&
            persistent.active != 0 && persistent.complete == 0) {
            write_line("SWEEP CONTINUING 1\n");
            run_current_candidate();
        }
        if (persistent.active == 0) {
            unsigned version;
            unsigned blocks;
            unsigned payload_bytes;
            unsigned rate_mask;
            if (sscanf(line, "SWEEP START %u %u %u %u", &version, &blocks,
                       &payload_bytes, &rate_mask) == 4 &&
                version == PROTOCOL_VERSION && blocks > 0 && blocks <= 1000 &&
                payload_bytes > 0 && payload_bytes <= MAX_PAYLOAD_BYTES &&
                rate_mask > 0 && rate_mask <= 3) {
                memset(&persistent, 0, sizeof(persistent));
                persistent.magic = PERSIST_MAGIC;
                persistent.active = 1;
                persistent.rate_mask = rate_mask;
                persistent.blocks = blocks;
                persistent.payload_bytes = payload_bytes;
                select_first_rate();
                save_state();
                write_line("SWEEP STARTED 1\n");
                run_current_candidate();
            }
        }
    }
}
