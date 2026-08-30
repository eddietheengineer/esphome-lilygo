"""ESPHome component for the LILYGO T-SIM7670G-S3.

Handles:
  - Battery voltage ADC sensor
  - GPS/NMEA parsing from the SIM7670G modem UART via microlink RX callback

The cellular data path is handled by the microlink (Tailscale) component's
built-in SIM7670G PPP driver. This component registers an RX byte callback
with the microlink to intercept NMEA sentences from the modem's UART output
during AT command phases (before PPP dials and in AT socket fallback mode).
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    CONF_NAME,
)
from esphome.components.esp32 import include_builtin_idf_component

CODEOWNERS = []
DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["sensor", "text_sensor"]

sim7670g_ns = cg.esphome_ns.namespace("sim7670g")
Sim7670gComponent = sim7670g_ns.class_("Sim7670gComponent", cg.Component)

CONF_BATTERY_ADC = "battery_adc"
CONF_VOLTAGE_DIVIDER = "voltage_divider"
CONF_GPS = "gps"
CONF_GPS_ENABLED = "enabled"

# GPS sensor config keys
CONF_GPS_LATITUDE = "latitude"
CONF_GPS_LONGITUDE = "longitude"
CONF_GPS_ALTITUDE = "altitude"
CONF_GPS_SPEED = "speed"
CONF_GPS_SATELLITES = "satellites"
CONF_GPS_HDOP = "hdop"
CONF_GPS_DATETIME = "datetime"
CONF_GPS_FIX_STATUS = "fix_status"


def _validate_adc_channel(value):
    """The battery ADC must be an ESP32-S3 ADC1 channel (GPIO1-GPIO10)."""
    value = cv.int_(value)
    if not 1 <= value <= 10:
        raise cv.Invalid("battery_adc must be GPIO1-GPIO10 (ADC1 channels on ESP32-S3)")
    return value


def _gps_sensor_schema():
    """Schema for a single GPS sensor entity (name optional)."""
    return cv.Schema({cv.Optional(CONF_NAME): cv.string})


GPS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_GPS_ENABLED, default=True): cv.boolean,
        cv.Optional(CONF_GPS_LATITUDE): _gps_sensor_schema(),
        cv.Optional(CONF_GPS_LONGITUDE): _gps_sensor_schema(),
        cv.Optional(CONF_GPS_ALTITUDE): _gps_sensor_schema(),
        cv.Optional(CONF_GPS_SPEED): _gps_sensor_schema(),
        cv.Optional(CONF_GPS_SATELLITES): _gps_sensor_schema(),
        cv.Optional(CONF_GPS_HDOP): _gps_sensor_schema(),
        cv.Optional(CONF_GPS_DATETIME): _gps_sensor_schema(),
        cv.Optional(CONF_GPS_FIX_STATUS): _gps_sensor_schema(),
    }
)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Sim7670gComponent),
        cv.Required(CONF_BATTERY_ADC): _validate_adc_channel,
        cv.Optional(CONF_VOLTAGE_DIVIDER, default=2.0): cv.positive_float,
        cv.Optional(CONF_UPDATE_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_GPS, default={}): GPS_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_global(sim7670g_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    # Re-enable ESP-IDF's esp_adc component (excluded by default to save compile
    # time) so the battery ADC read compiles.
    include_builtin_idf_component("esp_adc")

    cg.add(var.set_battery_adc_channel(config[CONF_BATTERY_ADC]))
    cg.add(var.set_voltage_divider(config[CONF_VOLTAGE_DIVIDER]))
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL].total_milliseconds))

    # GPS configuration
    gps_config = config[CONF_GPS]
    cg.add(var.set_gps_enabled(gps_config[CONF_GPS_ENABLED]))

    # Declare sensor pointers on the C++ side; sensor.py and text_sensor.py
    # will populate them via set_*_sensor() calls.
