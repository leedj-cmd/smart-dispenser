#!/usr/bin/env python3
"""
Firebase <-> ESP32 Serial Bridge
- ESP32: /dev/cu.usbserial-*  @ 115200
- Firebase: REST API (asia-southeast1)

Run:
    ~/venv/bin/python firmware/bridge/bridge.py
"""

import json
import os
import sys
import threading
import time
from datetime import datetime

import requests
import serial

DB_URL  = "https://sedal-miracle-49697-default-rtdb.asia-southeast1.firebasedatabase.app"
DB_AUTH = "gD5bH5vtIcrBwA1Xm7ruOfBegiuRUYvFvKTcEARs"
DEVICE  = "device_001"

SERIAL_PORT = os.environ.get("ESP32_PORT", "/dev/cu.usbserial-1140")
BAUD        = 115200

SLOTS = ("morning", "lunch", "dinner")

CMD_POLL_INTERVAL    = 1.0
SCHED_POLL_INTERVAL  = 30.0

# ---------- Firebase helpers ----------

def fb_get(path):
    url = f"{DB_URL}/{path}.json?auth={DB_AUTH}"
    r = requests.get(url, timeout=5)
    r.raise_for_status()
    return r.json()

def fb_put(path, value):
    url = f"{DB_URL}/{path}.json?auth={DB_AUTH}"
    r = requests.put(url, data=json.dumps(value), timeout=5)
    r.raise_for_status()
    return r.json()

# ---------- Serial helpers ----------

def send(ser, line):
    line = line.strip() + "\n"
    ser.write(line.encode("utf-8"))
    ser.flush()
    print(f"-> {line.strip()}")

def push_time(ser):
    now = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
    send(ser, f"TIME {now}")

def push_all_schedules(ser):
    root = fb_get(f"schedules/{DEVICE}") or {}
    for slot in SLOTS:
        s = root.get(slot, {}) or {}
        tm = s.get("time", "--:--")
        en = 1 if s.get("enabled") else 0
        send(ser, f"SCHED {slot} {tm} {en}")

# ---------- Event handler (ESP32 -> Firebase) ----------

def handle_event(line):
    print(f"<- {line}")
    parts = line.split()
    if len(parts) < 2 or parts[0] != "EVT":
        return
    kind = parts[1]
    if kind == "TAKEN" and len(parts) >= 3 and parts[2] in SLOTS:
        fb_put(f"schedules/{DEVICE}/{parts[2]}/taken", True)
    elif kind == "MISSED" and len(parts) >= 3 and parts[2] in SLOTS:
        fb_put(f"schedules/{DEVICE}/{parts[2]}/missed", True)
    elif kind == "DISPENSED" and len(parts) >= 3 and parts[2] in SLOTS:
        fb_put(f"schedules/{DEVICE}/{parts[2]}/dispensed", True)

# ---------- Serial reader thread ----------

def reader(ser, stop_evt):
    buf = b""
    while not stop_evt.is_set():
        try:
            chunk = ser.read(64)
        except serial.SerialException as e:
            print(f"[reader] serial error: {e}")
            break
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", errors="replace").strip()
            if not text:
                continue
            try:
                handle_event(text)
            except Exception as e:
                print(f"[reader] handler error: {e}")

# ---------- Main poll loop ----------

def main():
    print(f"[bridge] opening {SERIAL_PORT} @ {BAUD}")
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.5)
    time.sleep(2.0)   # wait for ESP32 reset/boot

    stop_evt = threading.Event()
    rt = threading.Thread(target=reader, args=(ser, stop_evt), daemon=True)
    rt.start()

    push_time(ser)
    push_all_schedules(ser)
    send(ser, "PING")

    last_sched_push = time.time()
    last_cmd_poll   = 0.0

    try:
        while True:
            now = time.time()

            if now - last_cmd_poll >= CMD_POLL_INTERVAL:
                last_cmd_poll = now
                try:
                    cmd = fb_get(f"dispense/{DEVICE}/command")
                except Exception as e:
                    print(f"[poll cmd] {e}")
                    cmd = None
                if isinstance(cmd, str) and cmd.strip():
                    cmd = cmd.strip()
                    if cmd in SLOTS:
                        send(ser, f"DISPENSE {cmd}")
                    elif cmd.upper() in ("HOME", "PING", "LCDRST"):
                        send(ser, cmd.upper())
                    elif cmd.upper().startswith(("JOG ", "SETPOS ", "SLOT ")):
                        send(ser, cmd.upper())
                    elif cmd.startswith("LCD "):
                        send(ser, cmd)   # keep case for LCD text
                    else:
                        print(f"[poll cmd] unknown: {cmd!r}")
                    try:
                        fb_put(f"dispense/{DEVICE}/command", "")
                    except Exception as e:
                        print(f"[poll cmd] clear failed: {e}")

            if now - last_sched_push >= SCHED_POLL_INTERVAL:
                last_sched_push = now
                try:
                    push_all_schedules(ser)
                except Exception as e:
                    print(f"[sched] {e}")

            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\n[bridge] stopping")
    finally:
        stop_evt.set()
        ser.close()

if __name__ == "__main__":
    main()
