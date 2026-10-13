#include "console.h"
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MASK(s) ((s)[0] ? "(set)" : "(empty)")

static void handle(char *line)
{
    if (!strcmp(line, "show")) {
        config_t c;
        config_load(&c);
        printf("@OK ssid=%s pass=%s host=%s key=%s\n", c.ssid, MASK(c.pass), c.host, MASK(c.key));
    } else if (!strcmp(line, "clear")) {
        config_clear();
        printf("@OK cleared\n");
    } else if (!strcmp(line, "reboot")) {
        printf("@OK rebooting\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else if (!strncmp(line, "set ", 4)) {
        char *name = line + 4;
        char *value = strchr(name, ' ');
        if (!value) { printf("@ERR usage: set <ssid|pass|host|key> <value>\n"); return; }
        *value++ = 0;
        if (strcmp(name, "ssid") && strcmp(name, "pass") && strcmp(name, "host") && strcmp(name, "key")) {
            printf("@ERR unknown setting '%s'\n", name);
            return;
        }
        size_t max = !strcmp(name, "ssid") ? 32 : !strcmp(name, "pass") ? 64 : !strcmp(name, "host") ? 47 : 127;
        if (strlen(value) > max) { printf("@ERR %s too long (max %u)\n", name, (unsigned)max); return; }
        printf(config_set(name, value) ? "@OK %s saved\n" : "@ERR %s not saved\n", name);
    } else if (line[0]) {
        printf("@ERR unknown command\n");
    }
}

static void task(void *arg)
{
    char line[200];
    size_t n = 0;
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY) != 1) continue;
        if (c == '\r' || c == '\n') {
            line[n] = 0;
            n = 0;
            handle(line);
        } else if (n < sizeof(line) - 1) {
            line[n++] = (char)c;
        }
    }
}

void console_start(void)
{
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0));
    xTaskCreate(task, "console", 4096, NULL, 4, NULL);
}
