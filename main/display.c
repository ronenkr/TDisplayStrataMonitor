#include "display.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "font8x8_basic.h"

#define PIN_MOSI 19
#define PIN_SCLK 18
#define PIN_CS   5
#define PIN_DC   16
#define PIN_RST  23
#define PIN_BL   4

// The 135x240 panel sits at an offset inside the ST7789's 240x320 RAM.
#define X_OFFSET 40
#define Y_OFFSET 53

#define CHUNK 16384

static spi_device_handle_t spi;
static uint16_t *fb;   // LCD_W * LCD_H RGB565, byte-swapped
static bool ready;     // display_init() has run

static void IRAM_ATTR pre_cb(spi_transaction_t *t)
{
    gpio_set_level(PIN_DC, (int)(intptr_t)t->user);
}

static void send(const void *data, size_t len, bool is_data)
{
    spi_transaction_t t = {.length = len * 8, .tx_buffer = data, .user = (void *)(intptr_t)is_data};
    ESP_ERROR_CHECK(spi_device_transmit(spi, &t));
}

static void cmd(uint8_t c) { send(&c, 1, false); }
static void cmd_data(uint8_t c, const uint8_t *d, size_t n) { cmd(c); if (n) send(d, n, true); }

void display_hold_dark(void)
{
    // The backlight pin is an RTC IO, so its level can be latched through deep sleep.
    rtc_gpio_hold_dis(PIN_BL);
    rtc_gpio_init(PIN_BL);
    rtc_gpio_set_direction(PIN_BL, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(PIN_BL, 0);
    rtc_gpio_hold_en(PIN_BL);
}

void display_power_down(void)
{
    if (ready) {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        cmd(0x28);                                  // display off
        cmd(0x10);                                  // sleep in
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    display_hold_dark();
}

void display_init(void)
{
    // coming back from a deep sleep that held the backlight off: release it
    rtc_gpio_hold_dis(PIN_BL);
    rtc_gpio_deinit(PIN_BL);

    fb = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_DMA);
    assert(fb);

    gpio_config_t io = {.pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST), .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io);

    spi_bus_config_t bus = {.mosi_io_num = PIN_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_SCLK,
                            .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = CHUNK};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {.clock_speed_hz = 26 * 1000 * 1000,   /* MOSI is on the GPIO matrix: ~26 MHz max */ .mode = 0, .spics_io_num = PIN_CS,
                                         .queue_size = 4, .pre_cb = pre_cb};
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &spi));

    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));                 // software reset
    cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));                 // sleep out
    cmd_data(0x3A, (uint8_t[]){0x55}, 1);                      // 16 bit colour
    cmd_data(0x36, (uint8_t[]){0x60}, 1);                      // landscape (MX | MV)
    cmd(0x21);                                                 // display inversion on (this panel needs it)
    cmd(0x13);                                                 // normal mode
    gfx_clear(0);
    display_flush();
    cmd(0x29);                                                 // display on
    vTaskDelay(pdMS_TO_TICKS(20));

    ledc_timer_config_t tmr = {.speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                               .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&tmr));
    ledc_channel_config_t ch = {.gpio_num = PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
                                .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    display_set_brightness(80);
    ready = true;
}

void display_set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 255 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void display_flush(void)
{
    uint16_t x0 = X_OFFSET, x1 = X_OFFSET + LCD_W - 1, y0 = Y_OFFSET, y1 = Y_OFFSET + LCD_H - 1;
    cmd_data(0x2A, (uint8_t[]){x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF}, 4);
    cmd_data(0x2B, (uint8_t[]){y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF}, 4);
    cmd(0x2C);
    const uint8_t *p = (const uint8_t *)fb;
    size_t left = LCD_W * LCD_H * 2;
    while (left) {
        size_t n = left > CHUNK ? CHUNK : left;
        send(p, n, true);
        p += n;
        left -= n;
    }
}

void gfx_clear(uint16_t color)
{
    for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = color;
}

void gfx_pixel(int x, int y, uint16_t color)
{
    if (x >= 0 && x < LCD_W && y >= 0 && y < LCD_H) fb[y * LCD_W + x] = color;
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    for (int j = 0; j < h; j++) {
        uint16_t *row = &fb[(y + j) * LCD_W + x];
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

void gfx_hline(int x, int y, int w, uint16_t color) { gfx_fill_rect(x, y, w, 1, color); }

int gfx_text_width(const char *s, int scale) { return (int)strlen(s) * 8 * scale; }

int gfx_text(int x, int y, const char *s, int scale, uint16_t fg)
{
    for (; *s; s++, x += 8 * scale) {
        unsigned char c = (unsigned char)*s;
        if (c > 127) c = '?';
        for (int row = 0; row < 8; row++) {
            uint8_t bits = (uint8_t)font8x8_basic[c][row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1 << col)) gfx_fill_rect(x + col * scale, y + row * scale, scale, scale, fg);
            }
        }
    }
    return x;
}
