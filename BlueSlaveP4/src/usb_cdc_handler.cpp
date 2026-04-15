// =============================================================================
// usb_cdc_handler.cpp — USB Host CDC-ACM (P4 reads S3 HWCDC via USB-OTG)
// ESP32-P4 acts as USB Host on its OTG port.
// ESP32-S3 presents as composite JTAG+CDC device (VID 0x303A, PID 0x1001).
// CDC-ACM interface is typically interface 2 on ESP32-S3 HWCDC composite.
// =============================================================================

#include "usb_cdc_handler.h"
#include "../include/config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

// ESP32-S3 HWCDC identifiers (from waveshare_s3_lcd7.json hwids)
#define S3_USB_VID  0x303A
#define S3_USB_PID  0x1001

// Ring buffer for received USB data
#define USB_RX_BUF_SIZE 1024
static uint8_t  usbRxBuf[USB_RX_BUF_SIZE];
static volatile int usbRxHead = 0;
static volatile int usbRxTail = 0;

// CDC device handle
static cdc_acm_dev_hdl_t s_cdc_dev = NULL;
static volatile bool s_usb_connected = false;
static volatile bool s_usb_init_ok = false;
static unsigned long s_last_open_attempt = 0;
static const unsigned long OPEN_RETRY_MS = 2000;
static int s_open_attempts = 0;

// Diagnostic counters (persist across connections)
static volatile int s_connect_count = 0;
static volatile int s_disconnect_count = 0;
static volatile int s_dev_detect_count = 0;
static volatile int s_last_open_err = 0;
static volatile int s_last_dtr_err = 0;
static char s_status_buf[128] = "not-init";

// =============================================================================
// CALLBACKS
// =============================================================================

// Called by CDC driver when data arrives from S3
static bool on_cdc_rx(const uint8_t *data, size_t data_len, void *user_arg) {
    for (size_t i = 0; i < data_len; i++) {
        int next = (usbRxHead + 1) % USB_RX_BUF_SIZE;
        if (next != usbRxTail) {  // drop if full
            usbRxBuf[usbRxHead] = data[i];
            usbRxHead = next;
        }
    }
    return true;
}

// Called on device events (disconnect, error)
static void on_cdc_event(const cdc_acm_host_dev_event_data_t *event, void *user_arg) {
    char buf[128];
    switch (event->type) {
        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            s_disconnect_count++;
            snprintf(buf, sizeof(buf), "[USB-CDC] DISCONNECTED (#%d) at %lums\n",
                     s_disconnect_count, millis());
            P4_LOG_PRINT(buf);
            if (s_cdc_dev) {
                cdc_acm_host_close(s_cdc_dev);
                s_cdc_dev = NULL;
            }
            s_usb_connected = false;
            break;
        case CDC_ACM_HOST_ERROR:
            snprintf(buf, sizeof(buf), "[USB-CDC] ERROR: %d at %lums\n",
                     event->data.error, millis());
            P4_LOG_PRINT(buf);
            break;
        default:
            snprintf(buf, sizeof(buf), "[USB-CDC] Event type=%d at %lums\n",
                     event->type, millis());
            P4_LOG_PRINT(buf);
            break;
    }
}

// Called when ANY new USB device is detected by the host
static void on_new_dev(usb_device_handle_t usb_dev) {
    s_dev_detect_count++;
    // Use snprintf to build one message at a time (avoids race conditions on Serial)
    char buf[256];

    const usb_device_desc_t *desc;
    if (usb_host_get_device_descriptor(usb_dev, &desc) == ESP_OK) {
        snprintf(buf, sizeof(buf),
            "[USB-CDC] NEW DEV VID=0x%04X PID=0x%04X class=%d sub=%d configs=%d bcdUSB=0x%04X\n",
            desc->idVendor, desc->idProduct, desc->bDeviceClass, desc->bDeviceSubClass,
            desc->bNumConfigurations, desc->bcdUSB);
        P4_LOG_PRINT(buf);
    } else {
        P4_LOG_PRINTLN("[USB-CDC] NEW DEVICE: (no descriptor)");
    }

    // Dump config descriptor to find the CDC interface index
    const usb_config_desc_t *config_desc;
    if (usb_host_get_active_config_descriptor(usb_dev, &config_desc) == ESP_OK) {
        snprintf(buf, sizeof(buf), "[USB-CDC]   num_ifaces=%d total_len=%d\n",
                 config_desc->bNumInterfaces, config_desc->wTotalLength);
        P4_LOG_PRINT(buf);
        // Walk interface descriptors
        int offset = 0;
        const uint8_t *p = (const uint8_t *)config_desc;
        while (offset < config_desc->wTotalLength) {
            uint8_t len = p[offset];
            uint8_t type = p[offset + 1];
            if (len == 0) break;
            if (type == 0x04) { // INTERFACE descriptor
                snprintf(buf, sizeof(buf), "[USB-CDC]   IF#%d: class=0x%02X sub=0x%02X proto=0x%02X eps=%d\n",
                         p[offset + 2], p[offset + 5], p[offset + 6], p[offset + 7], p[offset + 4]);
                P4_LOG_PRINT(buf);
            }
            offset += len;
        }
    }
}

// =============================================================================
// USB HOST DAEMON TASK
// =============================================================================
static void usb_host_lib_task(void *arg) {
    P4_LOG_PRINTLN("[USB-CDC] Host lib task started");
    for (;;) {
        uint32_t event_flags;
        esp_err_t err = usb_host_lib_handle_events(pdMS_TO_TICKS(1000), &event_flags);
        if (err == ESP_OK) {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                P4_LOG_PRINTLN("[USB-CDC] No clients event");
                usb_host_device_free_all();
            }
        }
    }
}

// =============================================================================
// INIT
// =============================================================================
void usb_cdc_init(void) {
    P4_LOG_PRINTLN("[USB-CDC] === Initializing USB Host ===");

    // 1. Install USB Host Library (drives OTG-HS controller)
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        P4_LOG_PRINTF("[USB-CDC] usb_host_install FAILED: 0x%x (%s)\n", err, esp_err_to_name(err));
        return;
    }
    P4_LOG_PRINTLN("[USB-CDC] usb_host_install OK");

    // 2. Daemon task — handles USB Host Library events (enumeration, etc.)
    BaseType_t ok = xTaskCreatePinnedToCore(
        usb_host_lib_task, "usb_host", 4096, NULL, 5, NULL, 0);
    if (ok != pdPASS) {
        P4_LOG_PRINTLN("[USB-CDC] Failed to create usb_host task!");
        return;
    }
    P4_LOG_PRINTLN("[USB-CDC] Daemon task created");

    // 3. Install CDC-ACM class driver with new_dev callback for diagnostics
    const cdc_acm_host_driver_config_t drv_cfg = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 5,
        .xCoreID = 0,
        .new_dev_cb = on_new_dev,
    };
    err = cdc_acm_host_install(&drv_cfg);
    if (err != ESP_OK) {
        P4_LOG_PRINTF("[USB-CDC] cdc_acm_host_install FAILED: 0x%x (%s)\n", err, esp_err_to_name(err));
        return;
    }
    P4_LOG_PRINTLN("[USB-CDC] CDC-ACM driver installed");

    s_usb_init_ok = true;
    P4_LOG_PRINTLN("[USB-CDC] === Host ready — plug S3 into USB-OTG port ===");
}

// Helper: finalize connection after successful open
static void finalize_connection(const char* method) {
    s_usb_connected = true;
    s_connect_count++;

    char buf[128];
    snprintf(buf, sizeof(buf), "[USB-CDC] CONNECTED #%d via %s at %lums\n",
             s_connect_count, method, millis());
    P4_LOG_PRINT(buf);

    // DO NOT assert DTR — CH343 auto-reset circuit pulses ESP32-S3 EN on DTR=true
    // With DTR=false the CDC data channel still works normally
    esp_err_t err = cdc_acm_host_set_control_line_state(s_cdc_dev, false, false);
    s_last_dtr_err = err;
    snprintf(buf, sizeof(buf), "[USB-CDC] DTR/RTS (no-reset): 0x%x (%s)\n", err, esp_err_to_name(err));
    P4_LOG_PRINT(buf);

    // Set line coding (informational for USB CDC)
    cdc_acm_line_coding_t lc = {
        .dwDTERate   = 921600,
        .bCharFormat = 0,  // 1 stop bit
        .bParityType = 0,  // none
        .bDataBits   = 8,
    };
    err = cdc_acm_host_line_coding_set(s_cdc_dev, &lc);
    snprintf(buf, sizeof(buf), "[USB-CDC] LineCoding: 0x%x (%s)\n", err, esp_err_to_name(err));
    P4_LOG_PRINT(buf);

    // Update status buffer
    snprintf(s_status_buf, sizeof(s_status_buf), "OK via %s", method);
}

// =============================================================================
// PROCESS — call from loop(); tries to open S3 when not yet connected
// =============================================================================
void usb_cdc_process(void) {
    if (!s_usb_init_ok) return;
    if (s_usb_connected) return;  // already connected, nothing to do

    unsigned long now = millis();
    if (now - s_last_open_attempt < OPEN_RETRY_MS) return;
    s_last_open_attempt = now;
    s_open_attempts++;

    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 500,
        .out_buffer_size = 256,
        .in_buffer_size  = 512,
        .event_cb  = on_cdc_event,
        .data_cb   = on_cdc_rx,
        .user_arg  = NULL,
    };

    // Strategy: try wildcard first (proven to work), then specific VID/PID
    // ESP32-S3 HWCDC is standard 2-interface CDC-ACM: IF#0=Comm, IF#1=Data
    esp_err_t err;

    // 1. Wildcard VID/PID, interface 0 (standard CDC-ACM communication interface)
    err = cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, 0,
                             &dev_config, &s_cdc_dev);
    if (err == ESP_OK && s_cdc_dev) {
        finalize_connection("wildcard iface=0");
        return;
    }

    // 2. Specific Espressif VID/PID
    err = cdc_acm_host_open(S3_USB_VID, S3_USB_PID, 0,
                             &dev_config, &s_cdc_dev);
    if (err == ESP_OK && s_cdc_dev) {
        finalize_connection("specific VID/PID iface=0");
        return;
    }

    // 3. Vendor-specific open (non-standard descriptors)
    err = cdc_acm_host_open_vendor_specific(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, 0,
                                             &dev_config, &s_cdc_dev);
    if (err == ESP_OK && s_cdc_dev) {
        finalize_connection("vendor-specific iface=0");
        return;
    }

    // Log periodically (every 5th attempt = every 10s)
    if (s_open_attempts % 5 == 1) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[USB-CDC] No S3 (attempt #%d)\n", s_open_attempts);
        P4_LOG_PRINT(buf);
        usb_host_lib_info_t info;
        if (usb_host_lib_info(&info) == ESP_OK) {
            snprintf(buf, sizeof(buf), "[USB-CDC] Host: %d devs, %d clients\n",
                     info.num_devices, info.num_clients);
            P4_LOG_PRINT(buf);
        }
    }
}

// =============================================================================
// PUBLIC API
// =============================================================================

bool usb_cdc_connected(void) {
    return s_usb_connected;
}

int usb_cdc_available(void) {
    int h = usbRxHead;
    int t = usbRxTail;
    return (h - t + USB_RX_BUF_SIZE) % USB_RX_BUF_SIZE;
}

int usb_cdc_read(void) {
    if (usbRxHead == usbRxTail) return -1;
    uint8_t b = usbRxBuf[usbRxTail];
    usbRxTail = (usbRxTail + 1) % USB_RX_BUF_SIZE;
    return b;
}

size_t usb_cdc_write(const uint8_t* data, size_t len) {
    if (!s_cdc_dev || !s_usb_connected || !data || len == 0) return 0;
    esp_err_t err = cdc_acm_host_data_tx_blocking(s_cdc_dev, data, len, 100);
    return (err == ESP_OK) ? len : 0;
}

const char* usb_cdc_status_str(void) {
    // Update status buffer with current counters
    snprintf(s_status_buf, sizeof(s_status_buf),
             "init=%d det=%d conn=%d/%d att=%d dtr=0x%x",
             (int)s_usb_init_ok, s_dev_detect_count,
             s_connect_count, s_disconnect_count,
             s_open_attempts, s_last_dtr_err);
    return s_status_buf;
}
