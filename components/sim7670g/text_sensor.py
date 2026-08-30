import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

from . import Sim7670gComponent

DEPENDENCIES = ["sim7670g"]

CONF_DATETIME = "datetime"
CONF_FIX_STATUS = "fix_status"


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(Sim7670gComponent),
        cv.Optional(CONF_DATETIME): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_FIX_STATUS): text_sensor.text_sensor_schema(),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ID])

    if dt_config := config.get(CONF_DATETIME):
        ts = await text_sensor.new_text_sensor(dt_config)
        cg.add(hub.set_datetime_sensor(ts))

    if fix_config := config.get(CONF_FIX_STATUS):
        ts = await text_sensor.new_text_sensor(fix_config)
        cg.add(hub.set_fix_status_sensor(ts))
