#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "board_config.h"
#include "player_settings.h"

static const char *const k_tag = "qemu-sdspi-test";
static const char k_test_path[] = "/sdcard/HLV/qemu.txt";
static const char k_video_path[] = "/sdcard/HLV/bunny.avi";
static const char k_expected_text[] = "HLV ESP32 SPI3 SD test";
static uint8_t s_read_buffer[8U * 1024U];

static __attribute__((noreturn)) void finish(int code)
{
    fflush(stdout);
    esp_rom_printf("SDSPI_QEMU_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

void app_main(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_SD_MOSI,
        .miso_io_num = BOARD_SD_MISO,
        .sclk_io_num = BOARD_SD_SCK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 32 * 1024,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card = NULL;
    char contents[sizeof(k_expected_text) + 2U] = {0};
    FILE *file;
    size_t bytes;
    esp_err_t result;

    ESP_LOGI(k_tag,
             "SPI3 SCK=%d MOSI=%d MISO=%d CS=%d DMA=auto",
             BOARD_SD_SCK, BOARD_SD_MOSI, BOARD_SD_MISO, BOARD_SD_CS);
    esp_rom_printf("SDSPI_QEMU_STAGE,bus-init\n");
    result = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) {
        ESP_LOGE(k_tag, "spi_bus_initialize failed: %s",
                 esp_err_to_name(result));
        finish(1);
    }

    host.slot = SPI3_HOST;
    host.max_freq_khz = PLAYER_SD_CLOCK_KHZ;
    device.host_id = SPI3_HOST;
    device.gpio_cs = BOARD_SD_CS;

    esp_rom_printf("SDSPI_QEMU_STAGE,mount\n");
    result = esp_vfs_fat_sdspi_mount("/sdcard", &host, &device,
                                     &mount, &card);
    if (result != ESP_OK) {
        ESP_LOGE(k_tag, "esp_vfs_fat_sdspi_mount failed: %s",
                 esp_err_to_name(result));
        finish(2);
    }

    esp_rom_printf("SDSPI_QEMU_STAGE,mounted\n");
    sdmmc_card_print_info(stdout, card);
    file = fopen(k_test_path, "rb");
    if (file == NULL) {
        ESP_LOGE(k_tag, "cannot open %s", k_test_path);
        finish(3);
    }
    bytes = fread(contents, 1, sizeof(contents) - 1U, file);
    fclose(file);
    const size_t text_bytes = sizeof(k_expected_text) - 1U;
    const bool lf_ending =
        bytes == text_bytes + 1U && contents[text_bytes] == '\n';
    const bool crlf_ending =
        bytes == text_bytes + 2U &&
        contents[text_bytes] == '\r' &&
        contents[text_bytes + 1U] == '\n';
    if (memcmp(contents, k_expected_text, text_bytes) != 0 ||
        (!lf_ending && !crlf_ending)) {
        ESP_LOGE(k_tag, "unexpected contents (%u bytes)", (unsigned)bytes);
        finish(4);
    }

    ESP_LOGI(k_tag, "read %u bytes from %s", (unsigned)bytes, k_test_path);
    esp_rom_printf("SDSPI_QEMU_STAGE,marker-read,%u\n", (unsigned)bytes);
    file = fopen(k_video_path, "rb");
    if (file != NULL) {
        const size_t target_bytes = 64U * 1024U;
        size_t total_bytes = 0;
        uint32_t hash = UINT32_C(2166136261);
        bool valid_header = false;

        while (total_bytes < target_bytes) {
            size_t requested = target_bytes - total_bytes;
            if (requested > sizeof(s_read_buffer)) {
                requested = sizeof(s_read_buffer);
            }
            esp_rom_printf("SDSPI_QEMU_STAGE,video-read,%u,%u\n",
                           (unsigned)total_bytes, (unsigned)requested);
            bytes = fread(s_read_buffer, 1, requested, file);
            esp_rom_printf("SDSPI_QEMU_STAGE,video-read-done,%u\n",
                           (unsigned)bytes);
            if (bytes == 0) {
                break;
            }
            if (total_bytes == 0) {
                valid_header =
                    bytes >= 12U &&
                    memcmp(s_read_buffer, "RIFF", 4U) == 0 &&
                    memcmp(s_read_buffer + 8U, "AVI ", 4U) == 0;
            }
            for (size_t i = 0; i < bytes; ++i) {
                hash ^= s_read_buffer[i];
                hash *= UINT32_C(16777619);
            }
            total_bytes += bytes;
        }
        fclose(file);
        esp_rom_printf("SDSPI_QEMU_VIDEO,%u,%08x\n",
                       (unsigned)total_bytes, (unsigned)hash);
        if (!valid_header || total_bytes != target_bytes) {
            finish(5);
        }
    }
    finish(0);
}
