#!/usr/bin/env bash
# Generate a self-signed CA + server cert for the Mosquitto broker, and a
# mosquitto password file. Run once on the VPS:
#
# ./gen-certs.sh <vps-ip-or-domain> <device-mqtt-password> [device-username] [ha-mqtt-password]
#
# Example:
# ./gen-certs.sh 203.0.113.5 "device-s3cr3t" esphome "ha-s3cr3t"
#
# Creates two broker users (see acl.conf):
#   esphome       - the device (password: arg 2)
#   homeassistant - Home Assistant's MQTT integration (password: arg 4)
#
# After running, copy certs/ca.crt to the machine that builds the device
# firmware and name it mosquitto-ca.pem (next to the .yaml) to enable TLS.
set -euo pipefail
cd "$(dirname "$0")"

DOMAIN="${1:?usage: gen-certs.sh <vps-ip-or-domain> <device-password> [device-username] [ha-password]}"
PASS="${2:?missing device mqtt password}"
USER="${3:-esphome}"
HA_PASS="${4:-}"

# Build the SAN: IP: for a raw IPv4, DNS: for a domain.
if [[ "$DOMAIN" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  SAN="IP:${DOMAIN}"
else
  SAN="DNS:${DOMAIN}"
fi

mkdir -p certs
cd certs

# 1. CA (10 years)
openssl genrsa -out ca.key 4096
openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 \
  -subj "/CN=MQTT-CA" -out ca.crt

# 2. Server key + CSR
openssl genrsa -out server.key 2048
openssl req -new -key server.key -subj "/CN=${DOMAIN}" -out server.csr

# 3. Sign the server cert with the CA (825 days — rotate periodically; a
# leaked server.key should not stay valid for a decade), with SAN.
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -days 825 -sha256 -out server.crt \
  -extfile <(printf "subjectAltName=${SAN}")

# Sanity-check the chain before anything else uses it.
openssl verify -CAfile ca.crt server.crt
# Permissions: the container runs as the mosquitto user (uid 100), which must
# read the mounted files (password.txt, server.key, certs) — so those stay
# 644 (the standard pattern for the eclipse-mosquitto image on a single-user
# VPS). ca.key is host-only (never mounted) and stays 600. On a multi-user
# VPS, tighten these and run the container as your own user instead.
chmod 600 ca.key
chmod 644 ca.crt server.crt server.key

cd ..

# 4. Password file (mosquitto_passwd). Prefer the local CLI; fall back to the
# docker image if it isn't installed.
add_user() {
  # mosquitto_passwd requires the file to exist (it does not create it).
  [ -f password.txt ] || { touch password.txt; chmod 600 password.txt; }
  if command -v mosquitto_passwd >/dev/null 2>&1; then
    mosquitto_passwd -b password.txt "$1" "$2"
  else
    # Run as the current user so the container can write the mounted file
    # (the image's default mosquitto user has no write access to the host dir).
    docker run --rm --user "$(id -u):$(id -g)" -v "${PWD}:/data" eclipse-mosquitto:2.0.20 \
      mosquitto_passwd -b /data/password.txt "$1" "$2"
  fi
}
add_user "${USER}" "${PASS}"
if [[ -n "$HA_PASS" ]]; then
  add_user "homeassistant" "${HA_PASS}"
fi
# 644: the container's mosquitto user (uid 100) must read it (see above).
chmod 644 password.txt
echo "Created password.txt for user '${USER}'${HA_PASS:+ and 'homeassistant'}"

echo
echo "Done. Next steps:"
echo " 1. docker compose up -d"
echo " 2. Copy certs/ca.crt to the firmware-build host as mosquitto-ca.pem"
echo "    (next to the .yaml) and set mqtt_ca_cert in secrets.yaml."
echo " 3. Set mqtt_broker='${DOMAIN}' in secrets.yaml."
if [[ -n "$HA_PASS" ]]; then
echo " 4. In Home Assistant, add the MQTT integration with username"
echo "    'homeassistant' and the HA password you just set."
fi
