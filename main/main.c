/*
 * USB-HS DFU trigger demo for the Waveshare ESP32-P4-WIFI6-DEV-KIT.
 *
 * Exposes a TinyUSB CDC-ACM device on the chip's native High-Speed USB-OTG
 * port (USB_DM/DP, the USB-A connector). When the host sends the literal
 * string "DFU" over that CDC port, the firmware sets the
 * LP_SYSTEM_REG_SYS_CTRL_REG.FORCE_DOWNLOAD_BOOT bit and restarts, so the
 * ROM boots straight into UART/USB download mode on the next boot — no
 * BOOT/RST button press needed.
 *
 * Verified against local ESP-IDF v6.0.2 headers (see README.md "Nguồn tham
 * chiếu" section for exactly which files/lines back which claim). Whether
 * the chip's masked ROM actually treats this bit the way we expect (bypasses
 * strapping pins, what "Joint Download Boot" covers, interaction with
 * secure boot/flash encryption) is NOT verified here — ROM boot-mode-select
 * logic isn't in the ESP-IDF source tree. See README.md.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "soc/lp_system_struct.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"

static const char *TAG = "usb_dfu_trigger";

#define DFU_TRIGGER_STRING  "DFU"
#define DFU_TRIGGER_LEN     (sizeof(DFU_TRIGGER_STRING) - 1)

/* Sliding window holding the last DFU_TRIGGER_LEN bytes received, so the
 * trigger is detected regardless of how the host's writes are chunked. */
static char s_window[DFU_TRIGGER_LEN];
static size_t s_window_len = 0;

static void enter_download_boot_and_restart(void)
{
    ESP_LOGW(TAG, "\"DFU\" received -> setting FORCE_DOWNLOAD_BOOT and restarting");

    /* Best-effort ack before the reset races the USB write. */
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)"DFU-OK\r\n", 8);
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(50));

    /* soc/lp_system_struct.h: lp_system_reg_dev_t.sys_ctrl.force_download_boot,
     * bit 2 of LP_SYSTEM_REG_SYS_CTRL_REG (LP_SYS is the extern'd instance). */
    LP_SYS.sys_ctrl.force_download_boot = 1;

    esp_restart();
}

static void cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    (void)event;
    uint8_t buf[64];
    size_t rx_size = 0;

    if (tinyusb_cdcacm_read(itf, buf, sizeof(buf), &rx_size) != ESP_OK || rx_size == 0) {
        return;
    }

    for (size_t i = 0; i < rx_size; i++) {
        if (s_window_len < DFU_TRIGGER_LEN) {
            s_window[s_window_len++] = (char)buf[i];
        } else {
            memmove(s_window, s_window + 1, DFU_TRIGGER_LEN - 1);
            s_window[DFU_TRIGGER_LEN - 1] = (char)buf[i];
        }

        if (s_window_len == DFU_TRIGGER_LEN &&
            memcmp(s_window, DFU_TRIGGER_STRING, DFU_TRIGGER_LEN) == 0) {
            enter_download_boot_and_restart();
            return; /* esp_restart() does not return; kept for clarity */
        }
    }
}

void app_main(void)
{
    /* __DATE__/__TIME__ are filled in by the compiler at build time, so this
     * line tells you exactly which build is running on the chip -- compare
     * it against the time you ran `idf.py build`/`idf.py dfu` to confirm a
     * DFU flash actually replaced the firmware. */
    ESP_LOGI(TAG, "Firmware build time: %s %s", __DATE__, __TIME__);

    /* esp_efuse_table.h: ESP_EFUSE_DIS_FORCE_DOWNLOAD (EFUSE_BLK0 bit 44).
     * 1 = force-download-boot permanently disabled on this chip. */
    if (esp_efuse_read_field_bit(ESP_EFUSE_DIS_FORCE_DOWNLOAD)) {
        ESP_LOGE(TAG, "eFuse DIS_FORCE_DOWNLOAD=1: this chip has force-download-boot "
                      "permanently disabled. The \"DFU\" trigger below will NOT be able "
                      "to put it into download mode.");
    } else {
        ESP_LOGI(TAG, "eFuse DIS_FORCE_DOWNLOAD=0: force-download-boot is available.");
    }

    ESP_LOGI(TAG, "Starting TinyUSB CDC on the native High-Speed USB-OTG port (USB-A)");

    tinyusb_config_t tusb_cfg = {
        .port = TINYUSB_PORT_HIGH_SPEED_0,
        .phy = {
            .skip_setup = false,
            .self_powered = false,
            .vbus_monitor_io = -1,
        },
        .task = {
            .size = 4096,
            .priority = 5,
            .xCoreID = 0,
        },
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = cdc_rx_callback,
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));

    ESP_LOGI(TAG, "Ready. Send \"DFU\" over the USB-CDC port to force download-boot mode.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
