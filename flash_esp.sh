#!/usr/bin/env bash
# Flash an ESPHome firmware.bin to the ESP32-S3 over its native USB-Serial/JTAG
# port (the board's "ESP USB" port), using esptool inside a docker container
# (we're not in the dialout group, so we run as root inside the container).
#
# Usage: flash_esp.sh <firmware.bin> [baud] [port] [flash-mode]
set -euo pipefail

FIRMWARE="$1"
BAUD="${2:-921600}"
PORT="${3:-/dev/ttyACM0}"
MODE="${4:-dio}"

# The host path of the firmware (must be readable inside the container).
HOST_FW="$(realpath "$FIRMWARE")"
# Mount the build dir read-only into the container.
MOUNT_SRC="$(dirname "$HOST_FW")"
MOUNT_DST="/fw"
FW_IN_CONTAINER="${MOUNT_DST}/$(basename "$HOST_FW")"

echo "### flashing $HOST_FW to $PORT @ $BAUD ###"
docker run --rm \
  --device "${PORT}:${PORT}" \
  -v "${MOUNT_SRC}:${MOUNT_DST}:ro" \
  python:3.11-slim \
  bash -c "
    pip install -q esptool 2>/dev/null
    echo '--- esptool read-mac (chip detect) ---'
    esptool --chip esp32s3 --baud ${BAUD} --port ${PORT} read-mac 2>&1 | tail -5 || true
    echo '--- write-flash: full factory image to 0x0, hard-reset after ---'
    esptool --chip esp32s3 --baud ${BAUD} --port ${PORT} --after hard-reset write-flash --flash-mode ${MODE} --flash-freq 80m --flash-size 16MB 0x0 '${FW_IN_CONTAINER}'
  "
