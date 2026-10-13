#include <stdio.h>
#include "config.h"
#include "console.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "metrics.h"
#include "nvs_flash.h"
#include "ui.h"
#include "wifi.h"

#define PIN_BTN_BOOT  0    // left button: cycle brightness
#define PIN_BTN_RIGHT 35   // right button: screen on/off, and wakes the board from deep sleep

// Power saving: when Strata has been unavailable this long, go to deep sleep. Every WAKE_INTERVAL
// the board wakes with the screen dark, checks once whether Strata is back, and sleeps again if not.
#define OFFLINE_SLEEP_MS   (5 * 60 * 1000)
#define WAKE_INTERVAL_US   (2 * 60 * 1000000LL)
#define PROBE_DEADLINE_MS  20000   // give up on a quiet wake-up after this long (WiFi join + one poll)

static const char *TAG = "main";
static const int levels[] = {80, 40, 15};

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool pressed(int pin, bool *was)
{
    bool down = gpio_get_level(pin) == 0;
    bool edge = down && !*was;
    *was = down;
    return edge;
}

// "Strata is not available": WiFi can't join, the PC doesn't answer, Strata isn't running, or it
// answers with something broken. An API key error is not counted (Strata is there, the setup is
// wrong) and neither is a missing WiFi setup.
static bool strata_unavailable(link_state_t l)
{
    return l == LINK_WIFI_DOWN || l == LINK_NO_NAME || l == LINK_UNREACHABLE || l == LINK_REFUSED || l == LINK_HTTP_ERROR ||
           l == LINK_BAD_DATA;
}

// "Strata is there": it answered, even if only with a 401.
static bool strata_present(link_state_t l) { return l == LINK_OK || l == LINK_AUTH; }

static void enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "deep sleep, waking in %d s or on the right button", (int)(WAKE_INTERVAL_US / 1000000));
    wifi_stop();
    display_power_down();
    // Only the RTC timer and the right button stay armed; no ULP program is loaded, so the CPUs,
    // WiFi and the rest of the chip are powered down.
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_US);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
    fflush(stdout);
    esp_deep_sleep_start();
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    static config_t cfg;
    config_load(&cfg);

    // A timer wake-up is a quiet probe: the screen stays dark until Strata is found.
    bool quiet = esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER);
    if (quiet) display_hold_dark(); else display_init();

    console_start();
    if (cfg.ssid[0]) wifi_start(&cfg);
    metrics_start(&cfg);

    static metrics_t m;
    if (quiet) {
        ESP_LOGI(TAG, "probe wake-up");
        uint32_t start = now_ms();
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(200));
            metrics_get(&m);
            if (strata_present(m.link)) break;
            bool failed = m.link == LINK_REFUSED || m.link == LINK_NO_NAME || m.link == LINK_UNREACHABLE || m.link == LINK_HTTP_ERROR ||
                          m.link == LINK_BAD_DATA;
            if (failed || now_ms() - start > PROBE_DEADLINE_MS) enter_deep_sleep();
        }
        ESP_LOGI(TAG, "Strata is back, waking up");
        display_init();
    }

    // GPIO35 is input-only with an external pull-up on the board; GPIO0 needs the internal one.
    gpio_config_t btn0 = {.pin_bit_mask = 1ULL << PIN_BTN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config_t btn35 = {.pin_bit_mask = 1ULL << PIN_BTN_RIGHT, .mode = GPIO_MODE_INPUT};
    gpio_config(&btn0);
    gpio_config(&btn35);
    bool was_boot = false, was_right = true, screen_on = true;   // was_right: ignore the press that woke us
    int level = 0;
    uint32_t unavailable_since = 0;   // 0 = Strata is available (or not yet known)

    for (;;) {
        if (pressed(PIN_BTN_BOOT, &was_boot)) {
            level = (level + 1) % (sizeof(levels) / sizeof(levels[0]));
            if (screen_on) display_set_brightness(levels[level]);
        }
        if (pressed(PIN_BTN_RIGHT, &was_right)) {
            screen_on = !screen_on;
            display_set_brightness(screen_on ? levels[level] : 0);
        }

        metrics_get(&m);
        uint32_t now = now_ms();
        if (strata_unavailable(m.link)) {
            if (!unavailable_since) unavailable_since = now ? now : 1;
            if (now - unavailable_since >= OFFLINE_SLEEP_MS) enter_deep_sleep();
        } else {
            unavailable_since = 0;
        }

        ui_render(&m, cfg.ssid, cfg.host, wifi_rssi(), now);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
