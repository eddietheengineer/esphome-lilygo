#!/usr/bin/env bash
# Generate a self-signed CA + server cert for the Mosquitto broker, and a
# mosquitto password file. Run once on the VPS:
#
#   ./gen-certs.sh <vps-ip-or-domain> <mqtt-password> [mqtt-username]
#
# Example:
#   ./gen-certs.sh 203.0.113.5 "s3cr3t" esphome
#
# After running, copy certs/ca.crt to the machine that builds the device
# firmware and name it mosquitto-ca.pem (next to the .yaml) to enable TLS.
set -euo pipefail
cd "$(dirname "$0")"

DOMAIN="${1:?usage: gen-certs.sh <vps-ip-or-domain> <mqtt-password> [username]}"
PASS="${2:?missing mqtt password}"
USER="${3:-esphome}"

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

# 3. Server cert signed by the CA, with the SAN for the VPS address
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -days 3650 -sha256 -out server.crt \
  -extfile <(printf "subjectAltName=${SAN}")

cd ..

# 4. Password file (mosquitto_passwd). Prefer the local CLI; fall back to the
#    docker image if it isn't installed.
if command -v mosquitto_passwd >/dev/null 2>&1; then
  mosquitto_passwd -b password.txt "${USER}" "${PASS}"
else
  docker run --rm -v "${PWD}:/data" eclipse-mosquitto:2 \
    mosquitto_passwd -b /data/password.txt "${USER}" "${PASS}"
fi
echo "Created password.txt for user '${USER}'"

echo
echo "Done. Next steps:"
echo "  1. docker compose up -d"
echo "  2. Copy certs/ca.crt to the firmware-build host as mosquitto-ca.pem"
echo "     (next to the .yaml) and enable the TLS block in the mqtt config."
echo "  3. Set mqtt_broker='${DOMAIN}' in secrets.yaml."
