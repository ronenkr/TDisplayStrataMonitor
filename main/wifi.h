#pragma once
#include <stdbool.h>
#include "config.h"

void wifi_start(const config_t *cfg);
void wifi_stop(void);   // before deep sleep
bool wifi_is_connected(void);
int  wifi_rssi(void);   // dBm, 0 when not connected
