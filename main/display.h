#pragma once
#include <stdint.h>

// TTGO T-Display: 135x240 ST7789, used in landscape (240x135).
#define LCD_W 240
#define LCD_H 135

// RGB565 colour helper (the framebuffer holds byte-swapped values, so DMA can send it as is).
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))
#define RGB(r, g, b)    __builtin_bswap16(RGB565(r, g, b))

void display_init(void);
void display_flush(void);                       // push the framebuffer to the panel
void display_set_brightness(int percent);       // 0..100
void display_hold_dark(void);                   // backlight off and latched off through deep sleep (works before display_init)
void display_power_down(void);                  // panel to sleep + backlight held off; call right before deep sleep

void gfx_clear(uint16_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);
void gfx_hline(int x, int y, int w, uint16_t color);
void gfx_pixel(int x, int y, uint16_t color);
// Draws text with the 8x8 font at an integer scale. Returns the x just after the text.
int  gfx_text(int x, int y, const char *s, int scale, uint16_t fg);
int  gfx_text_width(const char *s, int scale);
