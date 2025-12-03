/**
   Copyright 2026 Achim Pieters | StudioPieters®

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NON INFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   for more information visit https://www.studiopieters.nl
 **/

#include <stdio.h>
#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#include "esp32-lcm.h"
#include <button.h>

// -------- GPIO configuration (set these in sdkconfig) --------
#define BUTTON_GPIO      CONFIG_ESP_BUTTON_GPIO
#define RELAY_GPIO       CONFIG_ESP_RELAY_GPIO
#define BLUE_LED_GPIO    CONFIG_ESP_BLUE_LED_GPIO

static const char *RELAY_TAG   = "RELAY";
static const char *BUTTON_TAG  = "BUTTON";
static const char *IDENT_TAG   = "IDENT";

// Relay / plug state
bool relay_on = false;

// ---------- Low-level GPIO helpers ----------

static inline void relay_write(bool on) {
    gpio_set_level(RELAY_GPIO, on ? 1 : 0);
}

static inline void blue_led_write(bool on) {
    // Single LED used both as relay indicator and identify LED
    gpio_set_level(BLUE_LED_GPIO, on ? 1 : 0);
}

// Apply logical relay state to hardware (relay + blue LED)
void relay_apply_state(void) {
    relay_write(relay_on);
    blue_led_write(relay_on);
}

// All GPIO Settings
void gpio_init(void) {
    // Relay
    gpio_reset_pin(RELAY_GPIO);
    gpio_set_direction(RELAY_GPIO, GPIO_MODE_OUTPUT);

    // Blue LED
    gpio_reset_pin(BLUE_LED_GPIO);
    gpio_set_direction(BLUE_LED_GPIO, GPIO_MODE_OUTPUT);

    // Initial states
    relay_on = false;
    relay_apply_state();
}

// ---------- Accessory identification (Blue LED) ----------

void accessory_identify_task(void *args) {
    // Blink BLUE LED to identify, then restore previous state
    bool previous_led_state = relay_on;  // LED normally volgt relay_on

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++) {
            blue_led_write(true);
            vTaskDelay(pdMS_TO_TICKS(100));
            blue_led_write(false);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    // Zet LED terug naar de normale toestand (afhankelijk van relay_on)
    blue_led_write(previous_led_state);

    vTaskDelete(NULL);
}

void accessory_identify(homekit_value_t _value) {
    ESP_LOGI(IDENT_TAG, "Accessory identify");
    xTaskCreate(accessory_identify_task, "Accessory identify", configMINIMAL_STACK_SIZE,
                NULL, 2, NULL);
}

// ---------- HomeKit characteristics ----------

#define DEVICE_NAME          "HomeKit Plug"
#define DEVICE_MANUFACTURER  "StudioPieters®"
#define DEVICE_SERIAL        "NLDA4SQN1466"
#define DEVICE_MODEL         "SD466NL/A"
#define FW_VERSION           "0.0.1"

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER, DEVICE_MANUFACTURER);
homekit_characteristic_t serial = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model = HOMEKIT_CHARACTERISTIC_(MODEL, DEVICE_MODEL);
homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, LIFECYCLE_DEFAULT_FW_VERSION);
homekit_characteristic_t ota_trigger = API_OTA_TRIGGER;

// ON characteristic for the plug/relay
homekit_value_t relay_on_get() {
    return HOMEKIT_BOOL(relay_on);
}

void relay_on_set(homekit_value_t value) {
    if (value.format != homekit_format_bool) {
        ESP_LOGE(RELAY_TAG, "Invalid value format: %d", value.format);
        return;
    }

    relay_on = value.bool_value;
    ESP_LOGI(RELAY_TAG, "Setting relay %s", relay_on ? "ON" : "OFF");
    relay_apply_state();
}

// We keep a handle to ON characteristic so we can notify on button presses
homekit_characteristic_t relay_on_characteristic =
    HOMEKIT_CHARACTERISTIC_(ON, false, .getter = relay_on_get, .setter = relay_on_set);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(
        .id = 1,
        .category = homekit_accessory_category_outlet,  // Smart plug / outlet
        .services = (homekit_service_t *[]) {
            HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics = (homekit_characteristic_t *[]) {
                &name,
                &manufacturer,
                &serial,
                &model,
                &revision,
                HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
                NULL
            }),
            HOMEKIT_SERVICE(OUTLET, .primary = true, .characteristics = (homekit_characteristic_t *[]) {
                HOMEKIT_CHARACTERISTIC(NAME, "HomeKit Plug"),
                &relay_on_characteristic,
                &ota_trigger,
                NULL
            }),
            NULL
        }),
    NULL
};
#pragma GCC diagnostic pop

homekit_server_config_t config = {
    .accessories = accessories,
    .password = CONFIG_ESP_SETUP_CODE,
    .setupId = CONFIG_ESP_SETUP_ID,
};

// ---------- Button handling ----------

void button_callback(button_event_t event, void *context) {
    switch (event) {
    case button_event_single_press: {
        ESP_LOGI(BUTTON_TAG, "Single press -> toggle relay");

        bool new_state = !relay_on;
        homekit_value_t new_value = HOMEKIT_BOOL(new_state);

        // Update via setter (keeps logic in one place)
        relay_on_set(new_value);

        // Notify HomeKit about physical state change
        homekit_characteristic_notify(&relay_on_characteristic, new_value);
        break;
    }
    case button_event_double_press:
        // Do nothing, by design
        ESP_LOGI(BUTTON_TAG, "Double press -> no action");
        break;
    case button_event_long_press:
        ESP_LOGI(BUTTON_TAG, "Long press (10s) -> factory reset + reboot");
        lifecycle_factory_reset_and_reboot();
        break;
    default:
        ESP_LOGI(BUTTON_TAG, "Unknown button event: %d", event);
        break;
    }
}

// ---------- Wi-Fi / HomeKit startup ----------

void on_wifi_ready() {
    static bool homekit_started = false;

    if (homekit_started) {
        ESP_LOGI("INFORMATION", "HomeKit server already running; skipping re-initialization");
        return;
    }

    ESP_LOGI("INFORMATION", "Starting HomeKit server...");
    homekit_server_init(&config);
    homekit_started = true;
}

// ---------- app_main ----------

void app_main(void) {
    ESP_ERROR_CHECK(lifecycle_nvs_init());
    lifecycle_log_post_reset_state("INFORMATION");
    ESP_ERROR_CHECK(lifecycle_configure_homekit(&revision, &ota_trigger, "INFORMATION"));

    gpio_init();

    button_config_t btn_cfg = button_config_default(button_active_low);
    btn_cfg.max_repeat_presses = 3;
    btn_cfg.long_press_time = 10000;  // 10 seconds for lifecycle_factory_reset_and_reboot

    if (button_create(BUTTON_GPIO, btn_cfg, button_callback, NULL)) {
        ESP_LOGE(BUTTON_TAG, "Failed to initialize button");
    }

    esp_err_t wifi_err = wifi_start(on_wifi_ready);
    if (wifi_err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW("WIFI", "WiFi configuration not found; provisioning required");
    } else if (wifi_err != ESP_OK) {
        ESP_LOGE("WIFI", "Failed to start WiFi: %s", esp_err_to_name(wifi_err));
    }
}
