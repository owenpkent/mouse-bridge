// main.c
//
// Bluetooth Classic mouse -> wired USB mouse bridge for the ESP32-S31, matching
// the behavior of firmware/mouse_bridge/mouse_bridge.ino on the Pico.
//
// LED:   fast blink = pairing, slow blink = reconnecting, solid = connected.
// Hold the BOOT button for 3 s to forget the mouse and reboot into pairing mode.

#include <inttypes.h>
#include <string.h>

#include "bridge_logic.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "mouse_report.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

// ---------------------------------------------------------------- config ----

// Defaults match the ESP32-S31-Function-CoreBoard-1.
#define LED_GPIO 60     // addressable RGB LED
#define BUTTON_GPIO 61  // BOOT button, low when pressed

// Set to 1 if the scroll wheel runs backwards.
#define WHEEL_INVERT 0

#define SCAN_INQUIRY_UNITS 4           // in 1.28 s steps, about 5 s
#define SCAN_TIMEOUT_MS 20000          // inquiry plus the name lookups after it
#define PAIR_CONNECT_TIMEOUT_MS 30000  // includes SSP pairing
#define RECONNECT_TIMEOUT_MS 6000      // per attempt
#define RECONNECT_PAUSE_MS 1000        // between attempts
#define FORGET_HOLD_MS 3000
#define FAST_BLINK_MS 100
#define SLOW_BLINK_MS 600
#define USB_READY_TIMEOUT_MS 20
#define USB_RETRY_MS 100

// ----------------------------------------------------------------------------

static const char *TAG = "mouse_bridge";

typedef enum { STATE_PAIRING, STATE_RECONNECTING, STATE_CONNECTED } state_t;
static state_t s_state = STATE_PAIRING;

#define BIT_CONNECTED BIT0
#define BIT_SCAN_DONE BIT1
static EventGroupHandle_t s_events;

static QueueHandle_t s_mouse_queue;  // mouse_report_t, HID callback to USB task

// The only address allowed to connect. The main task sets it before opening a
// connection, and the HID callback reads it.
static esp_bd_addr_t s_target_addr;
static volatile bool s_have_target = false;

// Used only by the HID callback, which runs on its own event loop task.
#define MAX_LAYOUTS 4
static esp_hidh_dev_t *s_dev = NULL;
static mouse_layout_t s_layouts[MAX_LAYOUTS];
static size_t s_layout_count = 0;
static bool s_boot_mode = false;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------------------------------------------------- flash storage ---------

#define NVS_NAMESPACE "bridge"
#define NVS_KEY_MOUSE "mouse"

static bool load_addr(esp_bd_addr_t out) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = sizeof(esp_bd_addr_t);
    esp_err_t err = nvs_get_blob(h, NVS_KEY_MOUSE, out, &len);
    nvs_close(h);
    return err == ESP_OK && len == sizeof(esp_bd_addr_t);
}

static void save_addr(const esp_bd_addr_t addr) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Could not open storage, the mouse won't be remembered");
        return;
    }
    if (nvs_set_blob(h, NVS_KEY_MOUSE, addr, sizeof(esp_bd_addr_t)) != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
        ESP_LOGE(TAG, "Could not save the mouse");
    }
    nvs_close(h);
}

static void erase_addr(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_MOUSE);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ------------------------------------------------------------- USB mouse ----

static const uint8_t s_report_desc[] = {TUD_HID_REPORT_DESC_MOUSE()};

#define USB_CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define HID_ENDPOINT_IN 0x81
#define HID_ENDPOINT_SIZE 16

// bInterval is in frames at full speed: 1 = 1 ms.
static const uint8_t s_fs_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, USB_CONFIG_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_MOUSE, sizeof(s_report_desc), HID_ENDPOINT_IN,
                       HID_ENDPOINT_SIZE, 1),
};

#if TUD_OPT_HIGH_SPEED
// At high speed the interval is 2^(bInterval-1) x 125 us: 4 = 1 ms.
static const uint8_t s_hs_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, USB_CONFIG_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_MOUSE, sizeof(s_report_desc), HID_ENDPOINT_IN,
                       HID_ENDPOINT_SIZE, 4),
};
#endif

static const char *s_usb_strings[] = {
    (const char[]){0x09, 0x04},  // English
    "mouse-bridge",
    "Bluetooth Mouse Bridge",
    "000001",
    "Mouse",
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return s_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

static bool usb_send(void *ctx, uint8_t buttons, int8_t x, int8_t y, int8_t wheel) {
    (void)ctx;
    uint32_t start = now_ms();
    while (!tud_hid_ready()) {
        if (!tud_mounted() || now_ms() - start >= USB_READY_TIMEOUT_MS) {
            return false;
        }
        vTaskDelay(1);
    }
    return tud_hid_mouse_report(0, buttons, x, y, wheel, 0);
}

static void usb_task(void *arg) {
    (void)arg;
    mouse_forward_t forward = {0};
    for (;;) {
        // The timeout retries a button change that couldn't be sent.
        mouse_report_t r;
        bool got = xQueueReceive(s_mouse_queue, &r, pdMS_TO_TICKS(USB_RETRY_MS)) == pdTRUE;
        mouse_forward(&forward, tud_mounted(), got ? &r : NULL, usb_send, NULL);
    }
}

// Discards anything queued, so a stale press can't follow the release.
static void release_everything(void) {
    const mouse_report_t idle = {0};
    xQueueReset(s_mouse_queue);
    xQueueSend(s_mouse_queue, &idle, 0);
}

// ------------------------------------------------------------ HID host ------

static void load_layouts(esp_hidh_dev_t *dev) {
    size_t map_count = 0;
    esp_hid_raw_report_map_t *maps = NULL;
    s_layout_count = 0;
    if (esp_hidh_dev_report_maps_get(dev, &map_count, &maps) == ESP_OK) {
        for (size_t i = 0; i < map_count && s_layout_count < MAX_LAYOUTS; i++) {
            s_layout_count += mouse_parse_report_map(maps[i].data, maps[i].len, (uint8_t)i,
                                                     &s_layouts[s_layout_count],
                                                     MAX_LAYOUTS - s_layout_count);
        }
    }
    // No usable layout: fall back to boot protocol, which most mice support, at the
    // cost of the wheel.
    s_boot_mode = s_layout_count == 0;
    ESP_LOGI(TAG, "%u mouse report layout(s)%s", (unsigned)s_layout_count,
             s_boot_mode ? ", using boot protocol" : "");
    if (s_boot_mode && esp_hidh_dev_set_protocol(dev, ESP_HID_PROTOCOL_MODE_BOOT) != ESP_OK) {
        ESP_LOGE(TAG, "Could not switch to boot protocol");
    }
}

static void on_open(const esp_hidh_event_data_t *param) {
    if (param->open.status != ESP_OK) {
        ESP_LOGW(TAG, "Connection failed: %s", esp_err_to_name(param->open.status));
        return;
    }
    esp_hidh_dev_t *dev = param->open.dev;
    const uint8_t *bda = esp_hidh_dev_bda_get(dev);
    if (!s_have_target || bda == NULL || memcmp(bda, s_target_addr, sizeof(esp_bd_addr_t)) != 0) {
        ESP_LOGW(TAG, "Dropping connection from an unexpected device");
        esp_hidh_dev_close(dev);
        return;
    }
    load_layouts(dev);
    s_dev = dev;
    xEventGroupSetBits(s_events, BIT_CONNECTED);
}

static void on_input(const esp_hidh_event_data_t *param) {
    if (param->input.dev != s_dev) {
        return;
    }
    mouse_report_t r;
    if (s_boot_mode) {
        if (param->input.usage != ESP_HID_USAGE_MOUSE ||
            !mouse_decode_boot(param->input.data, param->input.length, &r)) {
            return;
        }
    } else {
        const mouse_layout_t *layout = mouse_layout_find(s_layouts, s_layout_count,
                                                         param->input.map_index,
                                                         (uint8_t)param->input.report_id);
        if (layout == NULL) {
            return;
        }
        mouse_decode(layout, param->input.data, param->input.length, &r);
    }
#if WHEEL_INVERT
    r.wheel = -bridge_clamp(r.wheel, BRIDGE_MAX_MOVE);  // clamp first: -INT32_MIN overflows
#endif
    xQueueSend(s_mouse_queue, &r, 0);  // if USB has stalled, drop rather than block Bluetooth
}

static void hidh_callback(void *args, esp_event_base_t base, int32_t id, void *event_data) {
    (void)args;
    (void)base;
    const esp_hidh_event_data_t *param = event_data;
    switch ((esp_hidh_event_t)id) {
        case ESP_HIDH_OPEN_EVENT:
            on_open(param);
            break;
        case ESP_HIDH_INPUT_EVENT:
            on_input(param);
            break;
        case ESP_HIDH_CLOSE_EVENT:
            if (param->close.dev == s_dev) {
                ESP_LOGI(TAG, "Mouse disconnected");
                s_dev = NULL;
                xEventGroupClearBits(s_events, BIT_CONNECTED);
                release_everything();
            }
            break;
        default:
            break;
    }
}

// ----------------------------------------------------------- Bluetooth GAP --

// Written by the GAP callback during a scan; read once BIT_SCAN_DONE is set.
static esp_bd_addr_t s_best_addr;
static int s_best_rssi;
static bool s_found;

static void on_device_found(const struct disc_res_param *res) {
    uint32_t cod = 0;
    bool has_cod = false;
    int rssi = -128;
    for (int i = 0; i < res->num_prop; i++) {
        const esp_bt_gap_dev_prop_t *p = &res->prop[i];
        if (p->type == ESP_BT_GAP_DEV_PROP_COD && p->len >= (int)sizeof(cod)) {
            memcpy(&cod, p->val, sizeof(cod));
            has_cod = true;
        } else if (p->type == ESP_BT_GAP_DEV_PROP_RSSI && p->len >= 1) {
            rssi = *(int8_t *)p->val;
        }
    }
    if (!has_cod) {
        return;
    }
    bool pointer = bridge_is_pointing_device(cod);
    ESP_LOGI(TAG, "  " ESP_BD_ADDR_STR "  cod=0x%06" PRIx32 "  rssi=%d%s", ESP_BD_ADDR_HEX(res->bda),
             cod, rssi, pointer ? "  <- pointing device" : "");
    if (pointer && (!s_found || rssi > s_best_rssi)) {
        memcpy(s_best_addr, res->bda, sizeof(esp_bd_addr_t));
        s_best_rssi = rssi;
        s_found = true;
    }
}

static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT:
            on_device_found(&param->disc_res);
            break;
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                xEventGroupSetBits(s_events, BIT_SCAN_DONE);
            }
            break;
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            ESP_LOGI(TAG, "Pairing result: %d", param->auth_cmpl.stat);
            break;
        case ESP_BT_GAP_CFM_REQ_EVT:  // a mouse has no display, so accept
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
        case ESP_BT_GAP_PIN_REQ_EVT: {  // legacy pairing: mice use a fixed PIN of zeros
            esp_bt_pin_code_t pin;
            memset(pin, '0', sizeof(pin));
            esp_bt_gap_pin_reply(param->pin_req.bda, true, param->pin_req.min_16_digit ? 16 : 4, pin);
            break;
        }
        default:
            break;
    }
}

// ------------------------------------------------------ LED and button -------

static led_strip_handle_t s_led = NULL;

static void led_write(bool on) {
    if (s_led == NULL) {
        return;
    }
    if (on) {
        led_strip_set_pixel(s_led, 0, 0, 16, 32);
        led_strip_refresh(s_led);
    } else {
        led_strip_clear(s_led);
    }
}

static void led_update(void) {
    static bool on = false;
    static bool first = true;
    bool want;
    uint32_t now = now_ms();
    switch (s_state) {
        case STATE_CONNECTED:
            want = true;
            break;
        case STATE_RECONNECTING:
            want = (now / SLOW_BLINK_MS) & 1;
            break;
        default:
            want = (now / FAST_BLINK_MS) & 1;
            break;
    }
    if (first || want != on) {
        first = false;
        on = want;
        led_write(on);
    }
}

// Hold the button for FORGET_HOLD_MS: drop the mouse and reboot into pairing mode.
static void check_forget(void) {
    static uint32_t pressed_since = 0;
    static bool pressed = false;
    uint32_t now = now_ms();
    if (gpio_get_level(BUTTON_GPIO) != 0) {
        pressed = false;
        return;
    }
    if (!pressed) {
        pressed = true;
        pressed_since = now;
    }
    if (now - pressed_since < FORGET_HOLD_MS) {
        return;
    }

    ESP_LOGI(TAG, "Button held: forgetting mouse");
    release_everything();
    for (int i = 0; i < 6; i++) {  // acknowledge with a quick flutter
        led_write(i & 1);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    if (s_have_target) {
        esp_bt_gap_remove_bond_device(s_target_addr);
    }
    erase_addr();
    while (gpio_get_level(BUTTON_GPIO) == 0) {  // don't reboot with BOOT still pressed
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_restart();
}

// Waits for an event while keeping the LED and button alive.
static bool wait_for(EventBits_t bit, uint32_t timeout_ms) {
    uint32_t start = now_ms();
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_events, bit, pdFALSE, pdFALSE, pdMS_TO_TICKS(10));
        if (bits & bit) {
            return true;
        }
        if (now_ms() - start >= timeout_ms) {
            return false;
        }
        led_update();
        check_forget();
    }
}

// ------------------------------------------------------------- pairing ------

static void do_pairing(void) {
    ESP_LOGI(TAG, "Pairing: scanning (put the mouse in pairing mode)");
    s_found = false;
    s_best_rssi = -128;
    xEventGroupClearBits(s_events, BIT_SCAN_DONE);
    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, SCAN_INQUIRY_UNITS, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start the scan: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_PAUSE_MS));
        return;
    }
    if (!wait_for(BIT_SCAN_DONE, SCAN_TIMEOUT_MS)) {
        esp_bt_gap_cancel_discovery();
        wait_for(BIT_SCAN_DONE, RECONNECT_TIMEOUT_MS);
    }
    if (!s_found) {
        ESP_LOGI(TAG, "No pointing device found, scanning again");
        return;
    }

    memcpy(s_target_addr, s_best_addr, sizeof(esp_bd_addr_t));
    s_have_target = true;
    ESP_LOGI(TAG, "Connecting to " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(s_target_addr));
    if (esp_hidh_dev_open(s_target_addr, ESP_HID_TRANSPORT_BT, 0) == NULL ||
        !wait_for(BIT_CONNECTED, PAIR_CONNECT_TIMEOUT_MS)) {
        ESP_LOGI(TAG, "Connect failed, scanning again");
        s_have_target = false;  // a late connection gets dropped by on_open()
        return;
    }

    save_addr(s_target_addr);
    ESP_LOGI(TAG, "Paired and saved");
    s_state = STATE_CONNECTED;
}

// ------------------------------------------------------------ reconnect -----

// Pages the mouse. The mouse can also connect to us by itself (we stay
// connectable), in which case on_open() sets BIT_CONNECTED.
static void do_reconnect(void) {
    static uint32_t last_attempt = 0;
    static bool attempted = false;
    if (xEventGroupGetBits(s_events) & BIT_CONNECTED) {
        ESP_LOGI(TAG, "Reconnected");
        s_state = STATE_CONNECTED;
        return;
    }
    if (attempted && now_ms() - last_attempt < RECONNECT_PAUSE_MS) {
        return;
    }

    ESP_LOGI(TAG, "Reconnecting to saved mouse");
    // Safe to repeat: for a device it already knows, open just retries the connection.
    if (esp_hidh_dev_open(s_target_addr, ESP_HID_TRANSPORT_BT, 0) != NULL &&
        wait_for(BIT_CONNECTED, RECONNECT_TIMEOUT_MS)) {
        ESP_LOGI(TAG, "Reconnected");
        s_state = STATE_CONNECTED;
    }
    attempted = true;
    last_attempt = now_ms();
}

// ------------------------------------------------------------------ init ----

static void init_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void init_led_and_button(void) {
    const led_strip_config_t strip = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &s_led);
    if (err != ESP_OK) {  // the bridge still works without its LED
        ESP_LOGW(TAG, "No LED: %s", esp_err_to_name(err));
        s_led = NULL;
    }

    const gpio_config_t button = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));
}

static void init_usb(void) {
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.full_speed_config = s_fs_config;
    cfg.descriptor.string = s_usb_strings;
    cfg.descriptor.string_count = sizeof(s_usb_strings) / sizeof(s_usb_strings[0]);
#if TUD_OPT_HIGH_SPEED
    cfg.descriptor.high_speed_config = s_hs_config;
#endif
    ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));
}

static void init_bluetooth(void) {
    esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&controller));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    esp_bluedroid_config_t bluedroid = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&bluedroid));
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_bt_io_cap_t io_cap = ESP_BT_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_cap, sizeof(io_cap)));
    esp_bt_pin_code_t unused_pin = {0};
    ESP_ERROR_CHECK(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, unused_pin));
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_callback));
    // Connectable, so a bonded mouse can reconnect by itself.
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE));

    const esp_hidh_config_t hidh = {
        .callback = hidh_callback,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&hidh));
}

void app_main(void) {
    s_events = xEventGroupCreate();
    s_mouse_queue = xQueueCreate(32, sizeof(mouse_report_t));
    configASSERT(s_events && s_mouse_queue);

    init_nvs();
    init_led_and_button();
    init_usb();
    xTaskCreate(usb_task, "usb_mouse", 4096, NULL, 5, NULL);

    // Decide before Bluetooth starts, so an early reconnect from the mouse is accepted.
    if (load_addr(s_target_addr)) {
        ESP_LOGI(TAG, "Saved mouse found, reconnecting");
        s_have_target = true;
        s_state = STATE_RECONNECTING;
    } else {
        ESP_LOGI(TAG, "No saved mouse, pairing");
        s_state = STATE_PAIRING;
    }
    init_bluetooth();

    for (;;) {
        led_update();
        check_forget();
        switch (s_state) {
            case STATE_PAIRING:
                do_pairing();
                break;
            case STATE_RECONNECTING:
                do_reconnect();
                break;
            case STATE_CONNECTED:
                if (!(xEventGroupGetBits(s_events) & BIT_CONNECTED)) {
                    s_state = STATE_RECONNECTING;
                }
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
