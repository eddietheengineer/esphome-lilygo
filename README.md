# LILYGO T-SIM7670G-S3 — ESPHome over Cellular + Tailscale

Turn a **LILYGO T-SIM7670G-S3** (ESP32-S3 N16R8 + SIM7670G LTE module) into a
Home Assistant device that reaches your network **over a cellular SIM card**,
tunneled through **Tailscale** — no public IP, no port forwarding required.

```
 Home Assistant  ◄──── tailnet (100.x.y.z) ────►  ESP32-S3 (ESPHome)
 (API :8123)                                          │  • Wi-Fi STA (own radio → local net, fallback)
                                                      │  • PPP netif (cellular data, primary)
                                                      ▼
                                               SIM7670G  (UART PPP only — no Wi-Fi)
                                                      │  LTE
                                                      ▼
                                               Cellular network (SIM)
```

The ESP32-S3 dials the SIM7670G's **cellular (PPP) uplink** over its UART — the
microlink Tailscale component powers the modem on, sets the APN, and dials PPP,
giving the ESP a cellular IP. The ESP's **own Wi-Fi radio** separately joins a
local network (with internet) as a **fallback uplink** — and provides the
"network" component that ESPHome's `api`/`time` components require. (The
SIM7670G itself has no Wi-Fi.) A Tailscale node
([Csontikka/esphome-tailscale](https://github.com/Csontikka/esphome-tailscale),
built on *microlink*) then joins your tailnet over the cellular uplink, and
Home Assistant reaches the device directly at its `100.x.y.z` address.

---

## What you need

| Item | Notes |
|------|-------|
| LILYGO T-SIM7670G-S3 | H707 / H802 SKU (ESP32-S3-WROOM-1 **N16R8**: 16 MB flash, 8 MB octal PSRAM) |
| A working SIM card | Data-enabled (APN known). Any carrier. |
| A **Tailscale** account | [tailscale.com](https://tailscale.com) — free for personal use. |
| [ESPHome](https://esphome.io) **≥ 2026.3.1** | Tested against **2026.6.2**. The Tailscale component requires it. |
| A USB-C cable | For the first flash and for console logs. |

> **Why Tailscale?** The cellular link has no fixed address and sits behind
> carrier NAT — Home Assistant can't reach the device directly. Tailscale gives
> the device a stable, encrypted, NAT-traversing identity (`100.x.y.z`) that HA
> can always reach, even when the device is roaming on cellular.

---

## 1. Hardware setup

1. **Insert the SIM** into the SIM7670G tray (eject with a paper clip).
2. **Power** the board. Options:
   - **USB-C** — simplest for bring-up.
   - **LiPo battery** (via the battery connector) — for unattended operation.
   - **Solar input** — the board has a solar-charger input; pair with a small
     panel + LiPo for a fully off-grid node.
3. **Antennas** — the board has separate LTE and Wi-Fi/BT antenna connectors.
   Connect both for reliable signal. (The SIM7670G's LTE antenna is the one
   that matters for data.)
4. **First flash only:** connect the **USB-C** port to your computer. This is
   the ESP32-S3's USB Serial/JTAG console — you'll use it for the initial
   flash and to watch the boot log.

> The ESP32-S3's own Wi-Fi radio and the SIM7670G's LTE radio are two separate
> radios on the same board. The ESP's Wi-Fi joins a local network (with
> internet) as a fallback uplink; the modem's LTE radio does the primary
> internet access over the PPP uplink. The SIM7670G has no Wi-Fi of its own.

---

## 2. Create a Tailscale auth key

The device authenticates to your tailnet once, using a **one-time auth key**.

1. Sign in at <https://login.tailscale.com/admin/settings/keys>.
2. Click **Create auth key**.
3. Recommended settings:
   - **Ephemeral:** *No* (you want a persistent node).
   - **Revoke after use:** *No* — the key is only consumed on first boot; the
     node then re-authenticates with its own device key on every reboot.
   - **Expiry:** *No expiration* (or a long one). You can shorten this later.
   - **Tags / users:** leave default for a personal node.
4. **Copy the key** (`tskey-...`). You'll paste it into `secrets.yaml` next.

> After the node joins once, you can manage it (rename, set expiry, revoke)
> from the admin console. The auth key itself is single-use.

---

## 3. Configure the device

All secrets live in `esphome/secrets.yaml` (kept out of the main YAML and out of
version control). Fill in the values:

```yaml
# esphome/secrets.yaml
tailscale_auth_key: "tskey-XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"   # from step 2
tailscale_ip: "100.x.y.z"                                       # the device's tailnet IP (for OTA)
modem_apn: "internet"                                           # your carrier's data APN
modem_sim_pin: ""                                                # SIM PIN (empty if none)
modem_ppp_user: ""                                               # PPP CHAP user (empty for IMSI auth)
modem_ppp_pass: ""                                               # PPP CHAP pass (empty for IMSI auth)
wifi_ssid: "<YOUR-WIFI-SSID>"                                   # a local network with internet
wifi_password: "<YOUR-WIFI-PASSWORD>"
```

- **`tailscale_auth_key`** — the key from step 2.
- **`tailscale_ip`** — the device's Tailscale IP (`100.x.y.z`), used so the
  dashboard/OTA reach it over the tailnet.
- **`modem_apn`** — your carrier's data APN. `"internet"` works for most;
  check your carrier's docs otherwise.
- **`modem_sim_pin`** — your SIM's PIN, if it has one. Leave empty otherwise.
- **`modem_ppp_user` / `modem_ppp_pass`** — PPP CHAP credentials. Most carriers
  use IMSI-based auth (leave both empty). Some (e.g. Soracom) use `sora`.
- **`wifi_ssid` / `wifi_password`** — a local network **with internet access**.
  The ESP's own Wi-Fi radio joins it as a fallback uplink (and to provide the
  "network" component the `api` component requires).

The main config is `esphome/t-sim7670g-s3.yaml`. The only things you may want
to touch:

- **`wifi.use_address`** — set via `!secret tailscale_ip` to the device's
  Tailscale IP (`100.x.y.z`) so the dashboard/OTA reach it over the tailnet.
- **`tailscale.cellular` pins** (`tx_pin` / `rx_pin` / `pwrkey_pin` /
  `dtr_pin`) — set for the **Standard** SKU (TX=4 / RX=5 / PWRKEY=46 / DTR=7).
  See "Modem pins" below if your board is a different revision.
- **`sim7670g.battery_adc`** (default `8`) and **`voltage_divider`** (default
  `2.0`) — the battery-voltage ADC channel and divider.
- **`sim7670g.update_interval`** (default `30s`) — how often the battery
  voltage is read.

### Modem pins

The `tailscale.cellular` pins are set for the **T-SIM7670G-S3-Standard** (the
main LilyGo SKU): UART TX=4 / RX=5, PWRKEY=46, DTR=7. The microlink also ships
an alternate LILYGO preset (TX=11 / RX=10 / PWRKEY=18 / DTR=9) for a different
board revision. If the modem never answers `AT`, your board is likely that
revision — check the LilyGo wiki/schematic for its pin map and update the
`tailscale.cellular` pins.

---

## 4. First flash

From the `esphome/` directory:

```bash
esphome run t-sim7670g-s3.yaml
```

`run` = validate + compile + flash + start the log monitor. The first build
downloads the ESP-IDF toolchain and the microlink, so it takes a while. Keep
the board plugged into USB.

### What to watch in the log

A healthy first boot looks like this (abridged, representative):

```
... booting ESP32-S3 ...
[wifi] Wi-Fi connection established          # ESP's own radio -> local network
[wifi] IP address is 192.168.4.2
[tailscale] Spawning cellular bring-up task (SIM7670G PPP)...
[tailscale] Modem power-on: PWRKEY pulse on GPIO46
[tailscale] Modem responded to AT probe
[tailscale] PDP context 1 -> APN 'internet'
[tailscale] PPP connected, cellular IP 10.12.34.56
[tailscale] Starting Tailscale node ...
[tailscale] Authenticated, assigned 100.64.123.45
[api] API server: 100.64.123.45:6053
```

Key milestones:

1. **`Modem responded to AT probe`** — the power-on sequence worked.
2. **`PPP connected, cellular IP …`** — the SIM registered and the PPP uplink is
   up. (If you see `+CPIN: SIM NOT READY` instead, the SIM isn't seated or is
   locked — see Troubleshooting.)
3. **`Wi-Fi connection established`** — the ESP's own radio joined the local
   network (the fallback uplink).
4. **`[tailscale] Authenticated, assigned 100.x.y.z`** — you're on the tailnet.

> **Note the assigned `100.x.y.z`** — you'll need it in the next step.

---

## 4a. Testing without a SIM card

You don't need the SIM to validate most of the build. The SIM7670G powers on
and answers `AT` **without** a SIM. And because the ESP's own Wi-Fi radio joins
a local network **with internet**, a no-SIM boot still has a working uplink —
so you can exercise the whole path, including Tailscale, without the SIM.

Flash it (section 4) and watch the console. Expected **without a SIM**:

```
[tailscale] Modem responded to AT probe
[tailscale] No SIM present; skipping PPP dial-up
[wifi] Wi-Fi connection established
[wifi] IP address is 192.168.4.2
[tailscale] Starting Tailscale node ...
[tailscale] Authenticated, assigned 100.64.123.45   # over the Wi-Fi uplink
```

What this proves with no SIM:
- **Power-on + AT** — the PWRKEY pulse drives the modem up and the AT engine
  gets `OK`.
- **Wi-Fi uplink** — the ESP's own radio joins the local network (with internet).
- **Tailscale** — with no cellular PPP, the node uses the Wi-Fi uplink to reach
  the control plane, authenticates, and gets a `100.x.y.z`.
- **ADC sensor** — the battery voltage reading appears.

What stays idle (correctly) with no SIM: only the cellular PPP uplink. The
Tailscale node falls back to the Wi-Fi uplink instead, so it still comes online.

> **Tip:** to test the *cellular* path specifically you do need the SIM. But for
> validating the build, the Tailscale + Home Assistant integration, and OTA, the
> Wi-Fi uplink is enough.

---

## 5. After first connection

### 5a. Pin the tailnet IP

The Tailscale component logs a hint like:

```
[tailscale] Set wifi use_address: 100.64.123.45 in your ESPHome YAML
```

Add that IP to `secrets.yaml` as `tailscale_ip` — the config's
`wifi.use_address` is set to `!secret tailscale_ip`, so the ESPHome dashboard
and OTA use the stable `100.x` address directly:

```yaml
# esphome/secrets.yaml
tailscale_ip: "100.64.123.45"   # ← the IP from the log
```

Re-flash (`esphome run …`) once. From then on the dashboard/OTA talk to the
device at its tailnet address.

### 5b. (Optional) Stop the auth key expiring

If you gave the key an expiry, the node will be asked to re-authenticate when
it lapses. For a permanent node, set the key to **never expire** in the
Tailscale admin console (or re-issue a non-expiring one). The node keeps its
device key and re-authenticates automatically on reboot.

### 5c. Add to Home Assistant

1. In HA: **Settings → Devices & Services → Add Integration → ESPHome**.
2. Enter the device's **`100.x.y.z`** address (the one from step 4/5a).
3. HA discovers it over the native API (port `6053`) and imports all the
   entities below.

That's it — the device now shows up in Home Assistant, reachable over cellular
through Tailscale, with no local network involved.

---

## 6. Entities

### Battery (from the `sim7670g` component)

| Entity | Type | Meaning |
|--------|------|---------|
| Battery Voltage | sensor | Battery rail (ADC1, ×2 divider) |

### Tailscale (from the `tailscale` component)

The component adds the full microlink entity set, including the node's
`100.x.y.z` address, VPN up/down state, peer count, uptime, a debug log, and
more. See the [esphome-tailscale README](https://github.com/Csontikka/esphome-tailscale)
for the complete list.

---

## 7. Day-to-day operation

- **OTA updates:** once `use_address` is set, `esphome run …` (or the
  dashboard) updates the device over the tailnet — no USB needed.
- **Logs:** `esphome logs t-sim7670g-s3.yaml` connects to the device's serial
  console over the tailnet.
- **Power:** LTE idle draw is modest but non-zero. For a battery/solar node,
  the board's power-save pin (GPIO 42) can reduce LTE output when idle. (The
  solar input + LiPo is designed for exactly this.)

---

## 8. Troubleshooting

- **Modem never answers `AT`** — check the `tailscale.cellular` pins match
  your board revision (see "Modem pins"), that the PWRKEY pulse is reaching the
  modem, and that the UART is wired (TX/RX not swapped). Try the alternate
  LILYGO preset (TX=11 / RX=10 / PWRKEY=18 / DTR=9).
- **`+CPIN: SIM NOT READY`** — the SIM isn't seated or is PIN-locked. Reseat it
  or set `modem_sim_pin`.
- **PPP won't connect** — check the APN (`modem_apn`) and, if your carrier
  needs it, the CHAP credentials (`modem_ppp_user` / `modem_ppp_pass`).
- **Tailscale won't authenticate** — check the auth key is valid and not
  expired, and that the PPP uplink is up (the node needs internet to reach the
  control plane).
- **HA can't reach the device** — confirm the device is on the tailnet (check
  the Tailscale admin console) and that HA is also on the tailnet.

---

## References

- [LilyGo T-SIM7670G-S3 wiki](https://wiki.lilygo.cc/products/t-sim-series/t-sim7670g-s3/)
- [Csontikka/esphome-tailscale](https://github.com/Csontikka/esphome-tailscale) — the microlink Tailscale component
- [Tailscale auth keys](https://tailscale.com/kb/121/authentication-keys)
- [ESPHome external components](https://esphome.io/guides/component-structure) — how `external_components` / `packages` work.
