#pragma once

// Line-based setup console on the USB serial port (115200 baud). Commands:
//   set ssid <wifi name>      set pass <wifi password>
//   set host <ip:port>        set key <strata api key>
//   show                      clear      reboot
// Every reply starts with "@OK" or "@ERR" so provision.py can find it among the log lines.
void console_start(void);
