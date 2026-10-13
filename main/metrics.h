#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

typedef enum {
    LINK_NO_CONFIG,     // no WiFi credentials stored yet
    LINK_WIFI_DOWN,     // WiFi configured but not connected
    LINK_CONNECTING,    // WiFi up, first request not answered yet
    LINK_OK,
    LINK_NO_NAME,       // the server's name does not resolve (router DNS has no entry for it)
    LINK_UNREACHABLE,   // no answer: the PC is off, asleep, or off the network
    LINK_REFUSED,       // the PC answered but nothing listens on the port: Strata is not running
    LINK_AUTH,          // 401: API key missing or wrong
    LINK_HTTP_ERROR,    // some other HTTP status
    LINK_BAD_DATA,      // 200 but not the expected /metrics JSON
} link_state_t;

typedef struct {
    link_state_t link;
    int http_status;
    int fail_count;          // consecutive failed polls
    bool have_data;          // a good sample has been received at some point
    uint32_t last_ok_ms;     // uptime (ms) of the last good sample
    char state[12];          // idle | reading | generating | unloaded
    char model[48];
    float tok_s;             // live decode speed, NAN unless generating
    float last_tok_s;        // decode speed of the most recent finished request, NAN if none
    float prefill_tok_s;
    int queued, prompt_read, prompt_total;
    double ram_used, ram_total;      // bytes, NAN if the server can't tell
    float disk_read_mb, disk_write_mb;
    float cpu, gpu_util, gpu_temp;
} metrics_t;

void metrics_start(const config_t *cfg);
void metrics_get(metrics_t *out);
