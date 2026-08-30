#!/usr/bin/env python3
"""Probe each ttyACM port (run inside docker as root): identify ESP32-S3
console vs SIM7670G AT port, and learn the modem's real capabilities."""
import time
import serial

PORTS = ["/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyACM2", "/dev/ttyACM3", "/dev/ttyACM4"]

def open_port(p):
    try:
        # dsrdtr/rtscts False -> no DTR/RTS toggling (avoids resetting ESP32/modem)
        s = serial.Serial(p, 115200, timeout=1.0, write_timeout=1.0,
                          dsrdtr=False, rtscts=False, xonxoff=False)
        time.sleep(0.2)
        s.reset_input_buffer()
        return s
    except Exception as e:
        return None

def probe(s, cmd, wait=0.7):
    if s is None:
        return None
    try:
        s.write(cmd)
        time.sleep(wait)
        data = b""
        while s.in_waiting:
            data += s.read(s.in_waiting)
            time.sleep(0.02)
        return data
    except Exception as e:
        return ("ERR:" + str(e)).encode()

# SIMCom AT command set. Wi-Fi + GPS + UDP probes included to map capabilities.
PROBES = [
    b"AT\r\n",
    b"AT+CGMM\r\n",      # model
    b"AT+CGMR\r\n",      # firmware revision
    b"AT+CGSN\r\n",      # serial number
    b"AT+CPIN?\r\n",     # SIM status
    b"AT+CEREG?\r\n",    # network registration
    b"AT+CGACT?\r\n",    # PDP context active
    b"AT+CGDCONT?\r\n",  # PDP context def
    b"AT+CSQ\r\n",       # signal quality
    b"AT+CGPADDR?\r\n",  # PDP IP address
    b"AT+QIOPEN?\r\n",   # UDP sockets (SIMCom UDP-over-cellular)
    b"AT+CGPS?\r\n",     # GNSS
    b"AT+CWAP?\r\n",     # Wi-Fi AP (SIM7600-style) - tests if Wi-Fi exists
    b"AT+CWMODE?\r\n",   # Wi-Fi mode
    b"AT+QCFG=\"WIFIMODE\"\r\n",  # Qualcomm wifi mode (in case)
]

for p in PORTS:
    print("=" * 64)
    print("PORT:", p)
    s = open_port(p)
    if s is None:
        print("  (could not open)")
        continue
    for cmd in PROBES:
        r = probe(s, cmd)
        if r is None:
            continue
        txt = r.decode("utf-8", "replace").strip()
        if txt:
            print(f"  {cmd.decode().strip():24s} -> {txt!r}")
        else:
            print(f"  {cmd.decode().strip():24s} -> (no response)")
    s.close()
    time.sleep(0.2)

print("=" * 64)
print("done")
