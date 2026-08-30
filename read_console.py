#!/usr/bin/env python3
"""Read a serial port for N seconds and print everything it emits (raw console).
Usage: read_console.py <port> <seconds> [baud]"""
import sys, time
import serial

port = sys.argv[1]
secs = float(sys.argv[2])
baud = int(sys.argv[3]) if len(sys.argv) > 3 else 115200

try:
    s = serial.Serial(port, baud, timeout=0.5, dsrdtr=False, rtscts=False, xonxoff=False)
except Exception as e:
    print("OPEN FAIL:", e)
    sys.exit(2)

print(f"### reading {port} @ {baud} for {secs}s (dsrdtr/rtscts off) ###", flush=True)
end = time.time() + secs
total = 0
while time.time() < end:
    if s.in_waiting:
        data = s.read(s.in_waiting)
        total += len(data)
        # print as text, escaping non-printable
        txt = data.decode("utf-8", "replace")
        sys.stdout.write(txt)
        sys.stdout.flush()
    time.sleep(0.02)
s.close()
print(f"\n### done: {total} bytes total ###", flush=True)
