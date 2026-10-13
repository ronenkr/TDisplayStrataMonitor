#pragma once
#include <stdbool.h>

// Settings kept in NVS, set over the USB serial console (see console.c / provision.py).
typedef struct {
    char ssid[33];
    char pass[65];
    char host[48];   // "name-or-ip[:port]" of the Strata server
    char key[128];   // Strata API key (sent as a Bearer token)
} config_t;

// Placeholder only: the real server name/IP is stored in NVS by provision.py, never in the source.
#define DEFAULT_HOST "strata-pc:8080"

void config_load(config_t *c);
bool config_set(const char *name, const char *value);   // "ssid" | "pass" | "host" | "key"
void config_clear(void);
