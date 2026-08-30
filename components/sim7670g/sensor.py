import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
    UNIT_DEGREES,
    UNIT_METERS,
    UNIT_KILOMETERS_PER_HOUR,
)

from . import Sim7670gComponent

DEPENDENCIES = ["sim7670g"]

CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_LATITUDE = "latitude"
CONF_LONGITUDE = "longitude"
CONF_ALTITUDE = "altitude"
CONF_SPEED = "speed"
CONF_SATELLITES = "satellites"
CONF_HDOP = "hdop"

# GPS sensor defaults
GPS_LAT_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_DEGREES,
    accuracy_decimals=6,
    state_class=STATE_CLASS_MEASUREMENT,
)
GPS_LON_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_DEGREES,
    accuracy_decimals=6,
    state_class=STATE_CLASS_MEASUREMENT,
)
GPS_ALT_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_METERS,
    accuracy_decimals=1,
    state_class=STATE_CLASS_MEASUREMENT,
)
GPS_SPEED_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_KILOMETERS_PER_HOUR,
    accuracy_decimals=1,
    state_class=STATE_CLASS_MEASUREMENT,
)
GPS_SAT_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=0,
    state_class=STATE_CLASS_MEASUREMENT,
)
GPS_HDOP_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=2,
    state_class=STATE_CLASS_MEASUREMENT,
)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(Sim7670gComponent),
        cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=2,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_LATITUDE): GPS_LAT_SCHEMA,
        cv.Optional(CONF_LONGITUDE): GPS_LON_SCHEMA,
        cv.Optional(CONF_ALTITUDE): GPS_ALT_SCHEMA,
        cv.Optional(CONF_SPEED): GPS_SPEED_SCHEMA,
        cv.Optional(CONF_SATELLITES): GPS_SAT_SCHEMA,
        cv.Optional(CONF_HDOP): GPS_HDOP_SCHEMA,
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ID])

    if battery_config := config.get(CONF_BATTERY_VOLTAGE):
        sens = await sensor.new_sensor(battery_config)
        cg.add(hub.set_battery_sensor(sens))

    if lat_config := config.get(CONF_LATITUDE):
        sens = await sensor.new_sensor(lat_config)
        cg.add(hub.set_latitude_sensor(sens))

    if lon_config := config.get(CONF_LONGITUDE):
        sens = await sensor.new_sensor(lon_config)
        cg.add(hub.set_longitude_sensor(sens))

    if alt_config := config.get(CONF_ALTITUDE):
        sens = await sensor.new_sensor(alt_config)
        cg.add(hub.set_altitude_sensor(sens))

    if speed_config := config.get(CONF_SPEED):
        sens = await sensor.new_sensor(speed_config)
        cg.add(hub.set_speed_sensor(sens))

    if sat_config := config.get(CONF_SATELLITES):
        sens = await sensor.new_sensor(sat_config)
        cg.add(hub.set_satellites_sensor(sens))

    if hdop_config := config.get(CONF_HDOP):
        sens = await sensor.new_sensor(hdop_config)
        cg.add(hub.set_hdop_sensor(sens))
