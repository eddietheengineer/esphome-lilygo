"""ESPHome component for the LILYGO T-SIM7670G-S3.

Handles:
  - Battery voltage ADC sensor

The cellular data path is handled by the microlink (Tailscale) component's
built-in SIM7670G PPP driver.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
)
from esphome.components.esp32 import include_builtin_idf_component

CODEOWNERS = []
DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["sensor"]

sim7670g_ns = cg.esphome_ns.namespace("sim7670g")
Sim7670gComponent = sim7670g_ns.class_("Sim7670gComponent", cg.Component)

CONF_BATTERY_ADC = "battery_adc"
CONF_VOLTAGE_DIVIDER = "voltage_divider"


def _validate_adc_channel(value):
    """The battery ADC must be an ESP32-S3 ADC1 GPIO (GPIO1-GPIO10).
    Converts GPIO number to ADC1 channel number (channel = GPIO - 1)."""
    value = cv.int_(value)
    if not 1 <= value <= 10:
        raise cv.Invalid("battery_adc must be GPIO1-GPIO10 (ADC1 pins on ESP32-S3)")
    return value - 1  # GPIO N maps to ADC1 channel N-1 on ESP32-S3


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Sim7670gComponent),
        cv.Required(CONF_BATTERY_ADC): _validate_adc_channel,
        cv.Optional(CONF_VOLTAGE_DIVIDER, default=2.0): cv.positive_float,
        cv.Optional(CONF_UPDATE_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_global(sim7670g_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    include_builtin_idf_component("esp_adc")
    include_builtin_idf_component("adc_cali")

    cg.add(var.set_battery_adc_channel(config[CONF_BATTERY_ADC]))
    cg.add(var.set_voltage_divider(config[CONF_VOLTAGE_DIVIDER]))
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL].total_milliseconds))
