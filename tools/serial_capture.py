#!/usr/bin/env python3
"""Capture serial output for N seconds without touching DTR/RTS (which would
reset the ESP32 or hold it in the bootloader). Usage: serial_capture.py PORT SECONDS [--reset]"""
import sys, time, serial
port, secs = sys.argv[1], float(sys.argv[2])
reset = "--reset" in sys.argv
s = serial.Serial()
s.port = port; s.baudrate = 115200; s.timeout = 0.2
s.rts = False; s.dtr = False
s.open()
if reset:
    # EN low via RTS (CH9102 auto-reset wiring), IO0 left high -> normal boot
    s.rts = True; time.sleep(0.1); s.rts = False
t0 = time.time()
buf = b""
while time.time() - t0 < secs:
    d = s.read(4096)
    if d:
        buf += d
        sys.stdout.write(d.decode("utf-8", "replace")); sys.stdout.flush()
s.close()
