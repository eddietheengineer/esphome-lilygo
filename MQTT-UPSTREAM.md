# MQTT Uplink Investigation — T-SIM7670G-S3

Branch: `feat/mqtt-uplink`

## Goal & hypothesis

Replace the Tailscale uplink with **ESPHome native MQTT** for publishing sensor
data (battery / solar voltage) over the SIM7670G cellular link.

Hypothesis to validate: **less data transmission + less complexity** (no
Tailscale). Known tradeoff: **no OTA** (HA can't reach the device behind
carrier NAT).

A second, hybrid option is also evaluated (user request): run MQTT normally,
but let HA send an **MQTT command that enables Tailscale on-demand for OTA**,
then disables it again to save data most of the time.

---

## Implementation status (built on this branch)

Implemented **Option D (hybrid)** with one simplification: the `modem`
component **reuses the microlink cellular driver** (`ml_cellular_init` +
`ml_cellular_ppp_start`) instead of extracting `ml_cellular.c` into a separate
ESP-IDF component. Since Option D keeps the Tailscale node in the build (for
on-demand OTA), the microlink component is compiled anyway — so the modem
component just calls its cellular driver. **No extraction needed.**

What was built:
- **`components/modem`** (new ESPHome component, in `esphome-tailscale`):
  - Brings up the SIM7670G PPP (microlink cellular driver) in a background dial
    task with retry/backoff + redial-on-link-loss.
  - Implements the `USE_MODEM` interface (`network/util.h`) so
    `network::is_connected()` reflects the cellular link → `mqtt` works over PPP
    with no Wi-Fi.
  - `AUTO_LOAD = ["network"]` (loads the network component / `esp_netif_init`).
  - State sensors: `data_connected` (binary), `rssi_dbm` (sensor).
- **`tailscale` component**: added a `start_enabled` option (default `true`).
  Set `false` → the node does not start at boot (saves data + PSRAM); the
  `vpn_enabled` switch's initial state now reflects it. Enable on-demand for OTA.
- **`esphome/t-sim7670g-s3-mqtt.yaml`** (new device config): `modem:` (cellular
  PPP) + `mqtt:` (publishes sensors) + `tailscale:` (`start_enabled: false`,
  `cellular` disabled) + `sim7670g:` (battery/solar). No `wifi:`, no `api:`, no
  `ota:`.
- **`mosquitto/`** (VPS broker): `docker-compose.yml`, `mosquitto.conf`
  (1883 plaintext + 8883 TLS), `gen-certs.sh` (self-signed CA + server cert +
  password file).
- **`secrets.yaml` / `secrets.yaml.example`**: added `mqtt_broker`,
  `mqtt_username`, `mqtt_password` (and optional `mqtt_ca_cert`).

The `tailscale` component runs with `cellular.enabled: false` (its default), so
it does **not** bring up its own PPP — the `modem` component is the sole PPP
provider (avoids a double `ml_cellular_init` on the singleton modem state). The
on-demand Tailscale node rides the `modem` component's PPP netif.

---

## Current architecture (baseline)

```
SIM7670G ──UART PPP──► ESP32-S3 (microlink cellular driver)
                          │  cellular PPP netif (standard lwIP)
                          ▼
                     Tailscale node (microlink: WireGuard + DERP + control plane)
                          │  100.x.y.z over the tailnet
                          ▼
                     Home Assistant (native API :6053)
```

- The **cellular PPP uplink is driven inside the `tailscale` ESPHome component**:
  `TailscaleComponent::setup()` spawns a task that calls `ml_cellular_init()` +
  `ml_cellular_connect()` (microlink's SIM7670G driver). The Tailscale node then
  rides that PPP netif as its uplink.
- The `sim7670g` component (this repo) is **battery/solar ADC only** — it does
  no cellular.
- `wifi:` (the ESP's own radio) is present as a fallback uplink **and** as the
  provider of ESPHome's `network` component.

---

## Key technical findings

### 1. The cellular PPP is a standard lwIP netif (good)
`ml_cellular.c` creates the PPP interface with `esp_netif_new()` using
`ESP_NETIF_INHERENT_DEFAULT_PPP()` / `ESP_NETIF_NETSTACK_DEFAULT_PPP`, feeds RX
via `esp_netif_receive()`, and TX via `uart_write_bytes()`. The source comment
states it plainly: *"The PPP interface creates a standard lwIP netif, so all
existing … socket operations … work unchanged over cellular."* So at the socket
level, an outbound MQTT TCP/TLS connection **can** ride the cellular PPP.

### 2. The PPP driver is bundled in the microlink component, which compiles the whole Tailscale stack
The microlink ESP-IDF component's `CMakeLists.txt` lists **all** sources
unconditionally: `microlink.c`, `ml_coord.c`, `ml_derp.c`, `ml_wg_mgr.c`,
`ml_noise.c`, `ml_h2.c`, `ml_stun.c`, `ml_tcp.c`, `ml_udp.c`, `nacl_box.c`,
`ml_x25519.c`, **`ml_cellular.c`**, `ml_at_socket.c`, … plus the nested
`wireguard_lwip` component. There is **no Kconfig gate that excludes the
non-cellular sources** — `CONFIG_ML_ENABLE_CELLULAR` only `#ifdef`s the *body*
of `ml_cellular.c`. So merely not using the `tailscale` ESPHome component does
**not** remove the Tailscale code from the build. To actually drop Tailscale you
must **extract `ml_cellular.c` into its own ESP-IDF component**.

### 3. The blocker: ESPHome `mqtt` gates on `network::is_connected()`
`MQTTClientComponent::start_connect_()` (mqtt_client.cpp) begins with:
```cpp
if (!network::is_connected()) return;
```
`network::is_connected()` (network/util.h) is a compile-time `#ifdef` chain that
only checks **ethernet / modem / wifi / openthread**. The microlink cellular PPP
netif is created **outside** ESPHome's wifi/ethernet, so it is **invisible** to
`network::is_connected()`. Consequence: **MQTT will not connect over the
cellular PPP** unless something makes `network::is_connected()` return true.

### 4. There is no native LTE modem component — but the extension point already exists
ESPHome 2026.6.2 ships **no** LTE/cellular PPP component (only `sim800l`, a 2G
AT-command module for SMS/calls — not a PPP data uplink). **However**,
`network/util.h` and `network/util.cpp` already contain `#ifdef USE_MODEM` hooks
that expect:
```cpp
modem::global_modem_component          // a global pointer
  ->is_connected()   -> bool
  ->is_disabled()    -> bool
  ->get_use_address()-> const char *
  ->get_ip_addresses()-> network::IPAddresses   // std::array<IPAddress, 5>
```
ESPHome auto-defines `USE_<COMPONENT>` for each component in the build, so a
component named **`modem`** would define `USE_MODEM` and be picked up by the
existing `network` code with **no ESPHome-core patching**. The interface is
small and already specced by the `#ifdef`s. This is the clean way to make the
cellular link visible to `mqtt` (and `api`/`time`/etc.).

### 5. `ml_cellular.c` is largely self-contained for the PPP path
Its only microlink-internal dependency is `ml_psram_malloc()` (a PSRAM malloc
wrapper — trivially replaced by `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`).
The AT-socket-bridge fallback (`ml_at_socket_init/deinit`) is a **separate**
file and is only used by the non-PPP `ml_cellular_data_start/stop` path — not
needed for PPP. Everything else is stock ESP-IDF (`esp_netif`,
`esp_netif_ppp`, `driver/uart`, `driver/gpio`, `freertos`, `esp_log`,
`esp_event`). **Extraction effort is low.**

### 6. The `vpn_enabled` switch already toggles the Tailscale node at runtime
`TailscaleComponent::set_tailscale_enabled(bool)` (the `vpn_enabled` switch)
does a full runtime enable/disable:
- **disable** → deferred `microlink_stop()` + `microlink_destroy()` (300 ms).
- **enable** → sets the flag; the next `loop()` calls `start_microlink_()`
  (`microlink_init()` + `microlink_start()`).
- The node **re-authenticates with its stored NVS device key** (no re-entry of
  the one-time auth key) and keeps a **stable `100.x` IP**.

This is exactly the primitive the hybrid option needs: an MQTT command can
`switch.turn_on(vpn_enabled)` to bring Tailscale up for OTA, then
`switch.turn_off(vpn_enabled)` to save data.

---

## Options

### Option A — Reuse the `tailscale` component for PPP, node off, add MQTT
Keep the `tailscale` external component with `cellular.enabled: true`; keep the
Tailscale node off (`vpn_enabled: false`); add `mqtt:`.
- **Pro:** minimal new code.
- **Con:** still compiles the full microlink/Tailscale stack (finding #2) — not
  "less complexity" in the build. Still requires an (auth) key. **And** MQTT is
  still gated on `network::is_connected()` (finding #3) → you'd need Wi-Fi up
  (defeats off-grid) or a `modem` shim anyway.
- **Verdict:** doesn't cleanly achieve the goal.

### Option B — Standalone `modem` component + MQTT, no Tailscale (clean)
1. Extract `ml_cellular.c` (+`ml_cellular.h`) into a new ESP-IDF component
   `sim7670g_ppp` (PPP-only; shim `ml_psram_malloc`; drop the AT-socket
   fallback).
2. Write an ESPHome **`modem`** component wrapping it: `setup()` →
   `ml_cellular_init()` + `ml_cellular_ppp_start()`; expose the `USE_MODEM`
   interface (finding #4) so `network::is_connected()` reflects the cellular
   link; optional cellular-state sensors.
3. Add `mqtt:` (broker over TLS). Remove the `tailscale` component + microlink
   from the build.
- **Pro:** no Tailscale code compiled or run; least runtime data; cleanest
  architecture.
- **Con:** **no OTA** (USB flash only). Moderate work (1 ESP-IDF component +
  1 ESPHome component + config + broker).
- **Verdict:** the clean "MQTT-only" path.

### Option C — Wi-Fi-only MQTT (no cellular)
If the deployment site has Wi-Fi: just add `mqtt:` over the existing `wifi:`,
drop Tailscale + cellular entirely.
- **Pro:** trivial; pure native ESPHome.
- **Con:** device must be in Wi-Fi range — **defeats the off-grid cellular
  purpose**.
- **Verdict:** only if Wi-Fi is available at the site.

### Option D — Hybrid: MQTT always-on + on-demand Tailscale for OTA (user's idea)
Run the **Option B `modem` component** for the always-on cellular PPP + MQTT
(low data). **Keep the Tailscale node in the build** but **off by default**; an
HA→device MQTT command enables it for OTA, then disables it.
- The `vpn_enabled` switch already does the runtime node toggle (finding #6); an
  MQTT `on_message` automation drives `switch.turn_on/off(vpn_enabled)`.
- The Tailscale component is configured `cellular.enabled: false` so it does
  **not** bring up its own PPP (avoids a double-`ml_cellular_init` on the
  singleton modem state); the node rides the `modem` component's PPP netif.
  Its existing start gate (`cellular_up_ || network::is_connected()`) is
  satisfied by the `modem` component.
- **Pro:** retains **OTA** (on-demand); low data most of the time (node off).
- **Con:** keeps the full microlink/Tailscale stack **compiled** (the node is
  real code); most total work of the options (modem component + Tailscale node +
  on-demand wiring + broker). The "less build complexity" goal is **not**
  achieved — only "less runtime data."
- **Verdict:** the only option that keeps cellular **and** OTA while paying the
  Tailscale data cost only during OTA windows.

---

## Broker infrastructure (required for the cellular options A/B/D)

The device sits on cellular **behind carrier NAT** → it can only make
**outbound** connections; HA (home network) cannot reach it inbound. So the
MQTT broker must be reachable by **both** the device (outbound from cellular)
and HA (outbound from home):

1. **Small VPS running Mosquitto** (TLS on 8883, username/password) —
   `device → VPS ← HA`. Cleanest.
2. **Cloud MQTT broker** (EMQX/HiveMQ cloud, etc.) — same pattern.
3. **Expose HA's built-in Mosquitto** via a public tunnel (Cloudflare Tunnel /
   Tailscale Funnel / reverse proxy) — adds its own infra; Funnel = Tailscale
   again.

This is **new infrastructure** the Tailscale approach did not need (Tailscale's
DERP + control plane are Tailscale-hosted). MQTT shifts the reachability
problem to a broker you must run and secure (TLS + auth).

---

## Data comparison

LTE has its own signaling overhead (RRC/PDCP) that is the same in every
cellular option; the comparison below is **application-layer** overhead.

- **Tailscale (node on):** control-plane long-poll + keepalive (~60 s), DISCO
  pings (~5 s) + heartbeats (~3 s) per active peer, WireGuard keepalives (~25 s
  / peer), DERP relay when there's no direct path, periodic netcheck.
  Continuous background traffic + WireGuard/DERP TLS CPU.
- **MQTT:** keepalive `PINGREQ`/`PINGRESP` (~15 s, ~10 B each) + sensor
  publishes (2×/30 s, ~50–100 B each) + one-time discovery.
- **Hybrid (D):** MQTT-only most of the time (low); the Tailscale cost is paid
  only during the (short) OTA window.

**Verdict:** MQTT is materially less data and less CPU than an always-on
Tailscale node. The "less data" hypothesis is **confirmed** for B and D.

---

## OTA tradeoff

- **B / C:** no remote OTA. ESPHome OTA needs HA→device reachability (native
  API :6053 or the esphome OTA protocol); MQTT is device→broker only. Updates =
  **USB flash** (`esphome run` / `esphome flash` over USB-C).
- **D:** OTA on-demand — enable Tailscale via MQTT → HA reaches the device over
  the tailnet → OTA over the native API → disable Tailscale. (A theoretical
  "self-OTA" via MQTT — device downloads a `.bin` and self-updates — is not
  built into ESPHome and would be fragile custom work; not recommended.)

---

## Recommendation

- **If OTA matters** (it usually does for a remote node): **Option D (hybrid)**.
  It is the only path that keeps cellular, drops the Tailscale *data cost* most
  of the time, **and** retains OTA. More work, but the `vpn_enabled` switch
  already implements the hard part (runtime node toggle).
- **If OTA can be sacrificed** (USB reflash is acceptable): **Option B** is
  simpler — no microlink/Tailscale in the build at all.
- **If the site has Wi-Fi:** **Option C** is trivial and worth doing first.

Keep the current Tailscale config as the baseline; the MQTT/hybrid config can
coexist as a separate YAML.

---

## What was built (Option D, as implemented)

1. **`components/modem`** (ESPHome, in `esphome-tailscale`) — brings up the
   SIM7670G PPP via the **microlink cellular driver** (no `ml_cellular.c`
   extraction; the microlink component is already compiled for the on-demand
   Tailscale node). Implements the `USE_MODEM` interface so
   `network::is_connected()` reflects the cellular link; `AUTO_LOAD = ["network"]`;
   `data_connected` + `rssi_dbm` state sensors.
2. **`tailscale` component** — added `start_enabled` (default `true`); the
   `vpn_enabled` switch's initial state now reflects it. Configured
   `start_enabled: false` (node off at boot) + `cellular` disabled (the modem
   component dials PPP).
3. **`esphome/t-sim7670g-s3-mqtt.yaml`** — `modem:` + `mqtt:` + `tailscale:`
   (`start_enabled: false`) + `sim7670g:`; no `wifi:`/`api:`/`ota:`.
4. **`mosquitto/`** — VPS broker: `docker-compose.yml`, `mosquitto.conf`
   (1883 plaintext + 8883 TLS), `gen-certs.sh` (self-signed CA + server cert +
   password file).
5. **`secrets.yaml` / `secrets.yaml.example`** — `mqtt_broker`,
   `mqtt_username`, `mqtt_password` (+ optional `mqtt_ca_cert`).

**Remaining (needs a VPS + the device):** stand up the broker, set the MQTT
secrets, flash over **USB**, verify PPP up → MQTT publishes → HA shows sensors;
then exercise the OTA flow (turn on `vpn_enabled` → wait for `VPN Connected` →
`esphome upload --device <100.x.y.z>` → turn off `vpn_enabled`).

## Config sketch (Option D)

```yaml
esphome:
  name: t-sim7670g-s3-mqtt
esp32:
  board: esp32-s3-devkitc1-n16r8
  variant: esp32s3
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG: y
      CONFIG_PPP_SUPPORT: y
      CONFIG_SPIRAM: y
      CONFIG_SPIRAM_MODE_OCT: y      # N16R8 = octal PSRAM
      CONFIG_SPIRAM_SPEED_80M: y
  flash_size: 16MB
psram:
  mode: octal
  speed: 80MHz
external_components:
  - source:
      type: git
      url: https://github.com/eddietheengineer/esphome-lilygo
      ref: feat/mqtt-uplink
      path: components
    components:
      - sim7670g
      - modem            # new: wraps sim7670g_ppp, provides USE_MODEM
  - source:
      type: git
      url: https://github.com/eddietheengineer/esphome-tailscale
      ref: feat/sim7670g-cellular-uplink
      path: components
    components:
      - tailscale        # kept for the on-demand OTA node

modem:                   # always-on cellular PPP uplink
  apn: !secret modem_apn
  sim_pin: !secret modem_sim_pin
  ppp_user: !secret modem_ppp_user
  ppp_pass: !secret modem_ppp_pass
  tx_pin: 4
  rx_pin: 5
  pwrkey_pin: 46
  dtr_pin: 7

tailscale:               # off by default; enabled on-demand for OTA
  auth_key: !secret tailscale_auth_key
  hostname: "t-sim7670g-s3"
  cellular:
    enabled: false       # do NOT dial PPP here — the modem component does it

mqtt:
  broker: !secret mqtt_broker_host
  port: 8883
  username: !secret mqtt_user
  password: !secret mqtt_pass
  certificate_authority: !secret mqtt_ca
  keepalive: 30s

sim7670g:
  battery_adc: 8
  voltage_divider: 2.0
  solar_adc: 18
  solar_divider: 2.0
  update_interval: 30s

sensor:
  - platform: sim7670g
    battery_voltage:
      name: "Battery Voltage"
    solar_voltage:
      name: "Solar Voltage"

switch:
  - platform: tailscale
    vpn_enabled:
      name: "VPN Enabled"

# On-demand Tailscale for OTA, driven by HA over MQTT
mqtt:
  on_message:
    - topic: t-sim7670g-s3/cmd/tailscale/enable
      then:
        - switch.turn_on: vpn_enabled
    - topic: t-sim7670g-s3/cmd/tailscale/disable
      then:
        - switch.turn_off: vpn_enabled

logger:
  level: INFO
```

> Note: the sketch shows two `mqtt:` blocks for readability; in the real config
> merge `on_message` into the single `mqtt:` block. The `modem:` config key and
> exact schema are defined when the component is written.

---

## Open items to verify during implementation

- **PSRAM mode:** the current device config says `psram: mode: quad`, but the
  `tailscale` component forces `CONFIG_SPIRAM_MODE_OCT`. N16R8 is octal PSRAM —
  confirm `octal` for the standalone build.
- **Uplink selection:** confirm the microlink node (with `cellular.enabled:
  false`) uses the `modem` component's PPP netif as its uplink (default lwIP
  route) when enabled on-demand.
- **IP stability on re-enable:** confirm the node reuses its NVS device key and
  keeps the same `100.x` IP across enable/disable cycles (so HA's pinned
  `use_address` stays valid).
- **`USE_MODEM` auto-define:** confirm ESPHome defines `USE_MODEM` for a
  component named `modem` in this version (2026.6.2).
