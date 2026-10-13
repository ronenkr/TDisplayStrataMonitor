#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "display.h"

#define C_BG     RGB(10, 12, 18)
#define C_TEXT   RGB(236, 240, 247)
#define C_DIM    RGB(104, 112, 130)
#define C_FAINT  RGB(52, 58, 72)
#define C_GREEN  RGB(72, 220, 140)
#define C_CYAN   RGB(80, 200, 240)
#define C_AMBER  RGB(255, 190, 60)
#define C_RED    RGB(255, 90, 90)
#define C_BOX    RGB(28, 18, 22)

// After this many failed polls in a row the screen switches to the "can't reach it" view.
#define FAILS_BEFORE_BANNER 2

static void fmt_gb(char *out, size_t n, double bytes)
{
    if (isnan(bytes)) snprintf(out, n, "-- GB");
    else snprintf(out, n, "%.1f GB", bytes / 1073741824.0);
}

static void fmt_disk(char *out, size_t n, float mb)
{
    if (isnan(mb)) snprintf(out, n, "-- MB/s");
    else if (mb >= 1000) snprintf(out, n, "%.2f GB/s", mb / 1024.0);
    else if (mb >= 10) snprintf(out, n, "%.0f MB/s", mb);
    else snprintf(out, n, "%.1f MB/s", mb);
}

static void fmt_age(char *out, size_t n, uint32_t ms)
{
    uint32_t s = ms / 1000;
    if (s < 100) snprintf(out, n, "%us", (unsigned)s);
    else if (s < 6000) snprintf(out, n, "%um", (unsigned)(s / 60));
    else snprintf(out, n, "%uh", (unsigned)(s / 3600));
}

static void draw_rssi(int x, int y, int rssi)
{
    int bars = rssi == 0 ? 0 : rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        int h = 3 + i * 3;
        gfx_fill_rect(x + i * 5, y + 12 - h, 3, h, i < bars ? C_TEXT : C_FAINT);
    }
}

static void draw_header(const char *status, uint16_t color, int rssi)
{
    gfx_text(6, 1, "STRATA", 2, C_TEXT);
    draw_rssi(108, 2, rssi);
    gfx_text(LCD_W - 6 - gfx_text_width(status, 2), 1, status, 2, color);
    gfx_hline(0, 18, LCD_W, C_FAINT);
}

// A message block at [y, y+h): title (scale 2) and up to two detail lines (scale 1).
static void draw_message(int y, int h, const char *title, uint16_t color, const char *l1, const char *l2)
{
    gfx_fill_rect(4, y, LCD_W - 8, h, C_BOX);
    gfx_fill_rect(4, y, 3, h, color);
    int ty = y + 5;
    gfx_text(14, ty, title, 2, color);
    if (l1) gfx_text(14, ty + 22, l1, 1, C_TEXT);
    if (l2) gfx_text(14, ty + 33, l2, 1, C_DIM);
}

static void draw_data(const metrics_t *m, bool stale)
{
    uint16_t fg = stale ? C_DIM : C_TEXT;
    uint16_t lbl = stale ? C_FAINT : C_DIM;
    char buf[40];

    // tokens per second: live while generating, otherwise the last finished request
    bool gen = !strcmp(m->state, "generating") && !isnan(m->tok_s);
    float tok = gen ? m->tok_s : m->last_tok_s;
    if (isnan(tok)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), tok >= 100 ? "%.0f" : "%.1f", tok);
    gfx_text(6, 24, buf, 4, stale ? C_DIM : (gen ? C_GREEN : C_TEXT));
    gfx_text(LCD_W - 6 - gfx_text_width("t/s", 2), 24, "t/s", 2, lbl);
    if (gen) snprintf(buf, sizeof(buf), "DECODING NOW");
    else if (!strcmp(m->state, "reading")) snprintf(buf, sizeof(buf), "READING PROMPT");
    else if (!isnan(m->last_tok_s)) snprintf(buf, sizeof(buf), "LAST REQUEST");
    else snprintf(buf, sizeof(buf), "NO REQUESTS YET");
    gfx_text(6, 58, buf, 1, lbl);
    if (m->queued > 0) {
        snprintf(buf, sizeof(buf), "%d QUEUED", m->queued);
        gfx_text(LCD_W - 6 - gfx_text_width(buf, 1), 58, buf, 1, stale ? lbl : C_AMBER);
    }

    // disk read
    fmt_disk(buf, sizeof(buf), m->disk_read_mb);
    gfx_text(6, 70, "DISK", 2, lbl);
    gfx_text(80, 70, buf, 2, stale ? C_DIM : C_CYAN);

    // RAM used / free
    double used = m->ram_used, total = m->ram_total;
    char val[16], line[24];
    fmt_gb(val, sizeof(val), used);
    snprintf(line, sizeof(line), "USED %s", val);
    gfx_text(6, 90, line, 2, fg);
    fmt_gb(val, sizeof(val), (isnan(total) || isnan(used)) ? NAN : total - used);
    snprintf(line, sizeof(line), "FREE %s", val);
    gfx_text(6, 108, line, 2, fg);

    // RAM bar
    gfx_fill_rect(6, 127, LCD_W - 12, 6, C_FAINT);
    if (!isnan(total) && !isnan(used) && total > 0) {
        double frac = used / total;
        if (frac > 1) frac = 1;
        uint16_t c = stale ? C_DIM : (frac > 0.92 ? C_RED : frac > 0.8 ? C_AMBER : C_GREEN);
        gfx_fill_rect(6, 127, (int)((LCD_W - 12) * frac), 6, c);
    }
}

void ui_render(const metrics_t *m, const char *ssid, const char *host, int rssi, uint32_t now_ms)
{
    gfx_clear(C_BG);
    char l1[64], l2[64], status[16];
    int dots = (now_ms / 400) % 4;

    switch (m->link) {
    case LINK_NO_CONFIG:
        draw_header("SETUP", C_AMBER, 0);
        draw_message(26, 100, "SETUP NEEDED", C_AMBER, "No WiFi saved yet. On the PC run:", "python provision.py");
        gfx_text(14, 88, "(sets WiFi + Strata API key", 1, C_DIM);
        gfx_text(14, 99, " over the USB cable)", 1, C_DIM);
        goto done;
    case LINK_WIFI_DOWN:
        draw_header("NO WIFI", C_RED, 0);
        snprintf(l1, sizeof(l1), "Joining \"%.24s\"%.*s", ssid, dots, "...");
        draw_message(26, 100, "WIFI DOWN", C_RED, l1, "Check the router / password");
        goto done;
    case LINK_CONNECTING:
        draw_header("WAIT", C_AMBER, rssi);
        snprintf(l1, sizeof(l1), "Contacting %s", host);
        draw_message(26, 100, "CONNECTING", C_AMBER, l1, NULL);
        goto done;
    default:
        break;
    }

    bool failing = m->link != LINK_OK && m->fail_count >= FAILS_BEFORE_BANNER;
    if (failing || (m->link != LINK_OK && !m->have_data)) {
        const char *title;
        const char *a = NULL;
        uint16_t col = C_RED;
        switch (m->link) {
        case LINK_REFUSED:     title = "STRATA OFFLINE"; col = C_AMBER; a = "PC is up, Strata not running"; break;
        case LINK_NO_NAME:     title = "NAME NOT FOUND"; a = "Router DNS doesn't know it"; break;
        case LINK_UNREACHABLE: title = "UNREACHABLE";    a = "No answer: PC off or asleep"; break;
        case LINK_AUTH:        title = "API KEY ERROR";  a = "Key missing or wrong"; break;
        case LINK_HTTP_ERROR:  title = "HTTP ERROR";     break;
        default:               title = "BAD RESPONSE";   a = "Not Strata's /metrics JSON"; break;
        }
        if (m->link == LINK_HTTP_ERROR) snprintf(l2, sizeof(l2), "%s answered HTTP %d", host, m->http_status);
        else snprintf(l2, sizeof(l2), "%s", host);

        if (m->have_data) {
            char age[8];
            fmt_age(age, sizeof(age), now_ms - m->last_ok_ms);
            snprintf(status, sizeof(status), "LAST %s", age);
            draw_header(status, C_DIM, rssi);
            draw_data(m, true);
            draw_message(22, 46, title, col, a, l2);
        } else {
            draw_header("OFFLINE", col, rssi);
            draw_message(26, 100, title, col, a, l2);
        }
        goto done;
    }

    // healthy, or a single hiccup: keep showing the last good numbers
    {
        uint16_t col = C_DIM;
        if (!strcmp(m->state, "generating")) {
            snprintf(status, sizeof(status), "GENERATING");
            col = C_GREEN;
        } else if (!strcmp(m->state, "reading")) {
            int pct = m->prompt_total > 0 ? 100 * m->prompt_read / m->prompt_total : -1;
            if (pct > 99) pct = 99;
            if (pct >= 0) snprintf(status, sizeof(status), "READ %d%%", pct);
            else snprintf(status, sizeof(status), "READING");
            col = C_CYAN;
        } else if (!strcmp(m->state, "unloaded")) {
            snprintf(status, sizeof(status), "NO MODEL");
            col = C_AMBER;
        } else {
            snprintf(status, sizeof(status), "IDLE");
        }
        draw_header(status, col, rssi);
        draw_data(m, false);
    }

done:
    display_flush();
}
