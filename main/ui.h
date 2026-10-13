#pragma once
#include "metrics.h"

// Draws one frame for the given metrics and pushes it to the panel.
void ui_render(const metrics_t *m, const char *ssid, const char *host, int rssi_dbm, uint32_t now_ms);
