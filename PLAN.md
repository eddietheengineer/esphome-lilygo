# T-SIM7670G-S3 + ESPHome + Tailscale — Plan

Goal: a LILYGO T-SIM7670G-S3 (ESP32-S3 N16R8 + SIM7670G LTE module) that
joins a Tailscale tailnet over the SIM7670G's cellular (PPP) uplink and shows
up in Home Assistant. The ESP32-S3's own Wi-Fi radio separately joins a local
network (with internet) as a fallback uplink — and provides the "network"
component ESPHome's api/time components require. (The SIM7670G has no Wi-Fi.)
All real data flows over cellular.

## Investigation findings

### Hardware (LILYGO T-SIM7670G-S3, H707 SKU)
- ESP32-S3-WROOM-1 N16R8: 16 MB flash (QIO 80 MHz), 8 MB OPI PSRAM,
  dual-core LX7 @ 240 MHz. USB-CDC console (USB Serial/JTAG).
- SIM7670G: LTE Cat-1 bis (10/5 Mbps) cellular module (no Wi-Fi of its own).
  The ESP32-S3's own Wi-Fi radio is used for the local-network fallback uplink.
- Pin map (LilyGo `T-SIM7670G-S3-Standard` README + `ATdebug`; the "Standard"
  SKU is the main product — a separate non-Standard revision uses different
  pins, see the README "board variants" note):

  | Function            | ESP GPIO | Notes                                  |
  |---------------------|----------|----------------------------------------|
  | Modem UART TX       | 4        | ESP TX → modem RXD                     |
  | Modem UART RX       | 5        | ESP RX ← modem TXD                     |
  | Modem RING          | 6        | input, ring indicator                  |
  | Modem DTR           | 7        | LOW keeps the modem awake (no sleep)  |
  | Modem PWRKEY        | 46       | 100 ms HIGH pulse powers the modem on  |
  | Power Save Mode     | 42       | HIGH = max LTE output; LOW = reduced   |
  | Battery ADC         | 8        | ADC1 (readable with Wi-Fi on), ×2 div  |
  | Solar ADC           | 18       | ADC2 (NOT readable with Wi-Fi on)      |

  The Standard board has **no** ESP-GPIO reset or LED — the RST/BOOT buttons
  are physical, and the status LEDs sit on the modem/battery circuit — so the
  component's `reset` and `led` pins are left unconfigured.

- Power-on sequence (LilyGo's tested `ATdebug` code; the Standard board has no
  reset pin, so it is PWRKEY-driven):
  1. Power Save Mode: HIGH (max LTE output)
  2. DTR: LOW (keep awake)
  3. PWRKEY: LOW 100 ms → HIGH 100 ms → LOW
  4. poll `AT` until `OK` (modem boot takes ~5–30 s)
  A 100 ms PWRKEY pulse is ignored if the modem is already on, so the
  sequence is safe to repeat.

### Modem AT surface (SIM7670G / A76XX family manual)
- `AT+CGMM` / `AT+CGMR` / `AT+CGSN` — model, firmware, IMEI
- `AT+CPIN?` — SIM status (`+CPIN: READY`)
- `AT+CGDCONT=1,"IP","<apn>"` — PDP context
- `AT+CGACT=1,1` — activate PDP context
- `AT+CEREG?` — LTE registration (0–5)
- `AT+CGPADDR=1` — cellular IP
- `AT+CSQ` — signal quality 0–31

### Tailscale on ESP32-S3
- No official Tailscale client for ESP32 (userspace Go client is far too
  heavy; no kernel WireGuard either).
- **Csontikka/esphome-tailscale** (external component, actively maintained
  2026, requires ESPHome ≥ 2026.3.1) implements a Tailscale node in C++
  using **microlink** (a C++ port of the Go userspace-networkstack):
  WireGuard crypto (ChaCha20-Poly1305, Curve25519, BLAKE2s), full IP/TCP/
  UDP stack, Tailscale control-plane protocol (key exchange, endpoint
  discovery, DERP relay fallback).
- Tested on ESP32-S3 N16R8 (16 MB flash / 8 MB PSRAM) — exactly this
  board's ESP module. It needs:
  - `framework: esp-idf` (not Arduino)
  - `psram: octal, 80 MHz`
  - a working network to reach the control plane (the cellular PPP uplink, or Wi-Fi)
  - a Tailscale auth key (or self-hosted Headscale/Koofy)
- It exposes `tailscale.use_address <100.x.y.z>` to pin the tailnet IP for
  the ESPHome dashboard/OTA.

### Home Assistant side
- ESPHome devices register with HA over the native API (port 6053) or
  MQTT. Once the device is on the tailnet, HA (on the tailnet) reaches it
  directly at its 100.x IP. No port forwarding, no public IP, no local
  network required.
- Recommended: native API (default ESPHome `api:` component). MQTT is
  optional if the user's HA already runs a broker on the tailnet.

## Architecture

```
tailnet (100.64.0.0/10)
┌──────────────────┐        ┌──────────────────────────────────────────┐
│ Home Assistant   │  100.x │ ESP32-S3 (ESPHome, ESP-IDF)              │
│  API :8123       │◄───────►│  • ESPHome API :6053                   │
│  (optional MQTT) │ tailnet │  • microlink Tailscale node             │
│                  │         │  • Wi-Fi STA (own radio → local net) ──┐   │
                             │  • PPP netif (cellular data) ──────┼───┤
                             └────────────────────────────────────┼───┘
                                                                   │
              ┌────────────────────────────────────────────────────┼────────┐
              │ SIM7670G                                            │          │
              │  (no Wi-Fi AP — LTE only) ───────────────────────────┤          │
              │  UART 115200 PPP (TX 4 / RX 5) ────────────────────┤          │
              │  PWRKEY←46, DTR←7, PwrSave←42                     │          │
              └────────────────────────────────────────────────────┼────────┘
                                                                     │ LTE
              ┌─────────────────────────────────────────────────────▼────────┐
              │ Cellular network (SIM) → Internet                             │
              └────────────────────────────────────────────────────────────────┘
```

Boot sequence:
1. The microlink's cellular task powers the SIM7670G on (PWRKEY pulse),
   waits for AT, sets the APN (and optional SIM PIN / PPP CHAP creds), and
   dials the PPP uplink — giving the ESP a cellular IP.
2. The `wifi:` component (the ESP's own radio) connects to a local network
   (with internet) as a fallback uplink — and provides the "network"
   component ESPHome requires (the api/time components need one).
3. The microlink node starts over the PPP netif (or the Wi-Fi uplink if there
   is no SIM), authenticates with the auth key, and gets a 100.x IP.
4. `api:` listens on the 100.x IP; HA connects over the tailnet.
   `use_address` (set via `!secret tailscale_ip`) pins that IP for OTA/dashboard.

## Deliverables

| File | Purpose |
|------|---------|
| (board: `esp32-s3-devkitc1-n16r8`) | Standard Espressif board ID matching the N16R8 module; no custom board file needed in ESPHome ≥ 2026.x |
| `components/sim7670g/` | ESPHome external component: battery-voltage ADC sensor (the modem itself is driven by the microlink's built-in cellular driver) |
| (tailscale component) | Pulled from the `eddietheengineer/esphome-tailscale` fork (git source in the config): the microlink Tailscale component + its ESP-IDF microlink components, with the SIM7670G cellular (PPP) uplink wired in |
| `esphome/t-sim7670g-s3.yaml` | Main config: wifi (local-network fallback uplink) + tailscale (cellular PPP uplink) + sim7670g (battery) + api |
| `esphome/secrets.yaml` | Template: auth key, Tailscale IP, APN, SIM PIN, PPP CHAP creds, Wi-Fi SSID/password |
| `README.md` | Setup guide: hardware, Tailscale key, flashing, first boot, pinning IP, disabling key expiry, HA, troubleshooting |

## Risks / mitigations
- **PPP uplink not coming up** (SIM not ready, wrong APN, CHAP mismatch, or a
  board-revision pin mismatch): mitigated by the microlink's diagnostics
  (registration, cellular IP, CHAP-failure events) and the config's
  overridable modem pins (see README "Modem pins"). The Wi-Fi (local-network)
  link is independent, so a PPP failure does not brick the device.
- **ESPHome version drift**: component targets ESPHome ≥ 2026.3.1 (tested
  against 2026.6.2); the tailscale component pins its own microlink
  revision.
- **First boot needs a Tailscale auth key** (one-time, from the Tailscale
  admin console). After the node is added, the key can be set to never
  expire; the node then re-authenticates with its device key on reboot.
