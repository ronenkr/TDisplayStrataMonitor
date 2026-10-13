# T-Display Strata Monitor

ESP-IDF firmware for the LilyGO TTGO T-Display (ESP32, 135x240 ST7789) that polls a Strata server's
`GET /metrics` once a second and shows decode tok/s, disk read, and RAM used/free.

## Build and flash (ESP-IDF 6.0.2)

    . C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
    idf.py build
    cd build; python -m esptool --chip esp32 -p COM15 -b 115200 write-flash "@flash_args"

Flash at 115200: this board's USB-serial link drops at 460800, so plain `idf.py flash` fails.

## Configure WiFi and the API key

    python provision.py            # prompts; password and key are not echoed
    python provision.py --show     # what is stored (secrets masked)

Settings live in NVS (`ssid`, `pass`, `host` placeholder `strata-pc:8080`, `key`) and survive reflashing
the app. `host` is `name-or-ip[:port]`; a bare name is looked up through the router's DNS as given and then as
`<name>.lan` (lwIP applies no search domains), and re-resolved on every poll, so a changed IP is followed.
If the name can't be resolved the screen says NAME NOT FOUND. The key is sent as `Authorization: Bearer <key>`.

## Screen states

| Screen | Meaning |
|---|---|
| SETUP NEEDED | no WiFi stored yet |
| WIFI DOWN | WiFi configured but not joined |
| STRATA OFFLINE | the PC answered but refused the connection: Strata is not running |
| UNREACHABLE | no answer at all: PC off, asleep, or off the network |
| API KEY ERROR | HTTP 401/403: key missing or wrong |
| HTTP ERROR / BAD RESPONSE | server answered something unexpected |

After a connection was working, the last numbers stay on screen greyed out under the message, with
`LAST 12s` in the header showing how stale they are. The banner appears after 2 failed polls in a row.
If the server is up but the model is unloaded the header shows `NO MODEL`.

## Deep sleep when Strata is gone

If Strata stays unavailable for 5 minutes (WiFi won't join, PC unreachable, Strata not running, or a broken
reply) the screen and backlight go off and the ESP32 enters deep sleep: CPUs and WiFi are powered down, and
only the RTC timer and the right button stay armed. API-key errors and a missing WiFi setup do not trigger it.

Every 2 minutes it wakes with the screen still dark, joins WiFi and polls `/metrics` once. No answer, or 20 s
passed: back to sleep. If Strata answers (even with a 401) it wakes fully and the screen comes on.
Pressing the right button wakes it at any time (the offline timer then starts over).

The ULP coprocessor is not used: it cannot run WiFi, so the check has to run on the main cores. The constants
are at the top of `main/main.c` (`OFFLINE_SLEEP_MS`, `WAKE_INTERVAL_US`, `PROBE_DEADLINE_MS`).

## Buttons

- GPIO0 (left): cycle backlight brightness
- GPIO35 (right): screen on/off, and wake from deep sleep
