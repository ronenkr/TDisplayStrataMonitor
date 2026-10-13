"""Send WiFi + Strata settings to the T-Display over USB serial.

    python provision.py                 # prompts for everything
    python provision.py --port COM15 --ssid MyWifi --host my-pc:8080
    python provision.py --show          # print what is stored (secrets masked)

Run it inside the ESP-IDF environment (pyserial comes with it). Passwords and the API key are
prompted for without echo unless given on the command line.
"""
import argparse
import getpass
import sys
import time

import serial


def talk(ser, line, wait=2.0):
    ser.reset_input_buffer()
    ser.write((line + "\n").encode())
    end = time.time() + wait
    while time.time() < end:
        reply = ser.readline().decode(errors="replace").strip()
        if reply.startswith("@OK") or reply.startswith("@ERR"):
            return reply
    return "@ERR no reply (is the firmware running?)"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM15")
    ap.add_argument("--ssid")
    ap.add_argument("--password")
    ap.add_argument("--host", help="Strata server as name-or-ip[:port], e.g. my-pc:8080 (default keeps the stored one)")
    ap.add_argument("--key", help="Strata API key")
    ap.add_argument("--show", action="store_true", help="only show the stored settings")
    a = ap.parse_args()

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = a.port, 115200, 0.3
    ser.dtr = ser.rts = False          # do not reset the board when opening the port
    ser.open()

    if a.show:
        print(talk(ser, "show"))
        return 0

    ssid = a.ssid or input("WiFi name (SSID): ").strip()
    password = a.password if a.password is not None else getpass.getpass("WiFi password (empty for open): ")
    key = a.key if a.key is not None else getpass.getpass("Strata API key (empty for none): ")
    host = a.host

    todo = [("ssid", ssid), ("pass", password)]
    if host:
        todo.append(("host", host))
    todo.append(("key", key))
    for name, value in todo:
        reply = talk(ser, f"set {name} {value}")
        print(f"{name}: {reply}")
        if reply.startswith("@ERR"):
            return 1
    print(talk(ser, "show"))
    print(talk(ser, "reboot"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
