import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
)

from . import Sim7670gComponent

DEPENDENCIES = ["sim7670g"]

CONF_BATTERY_VOLTAGE = "battery_voltage"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(Sim7670gComponent),
        cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=2,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ID])

    if battery_config := config.get(CONF_BATTERY_VOLTAGE):
        sens = await sensor.new_sensor(battery_config)
        cg.add(hub.set_battery_sensor(sens))
