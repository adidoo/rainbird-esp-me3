"""ESPHome component: serial link with a Rain Bird ESP-ME3 controller (in place of the LNK2 module).

Protocol recorded on 01/11/2026 (see ../../PROTOCOLE_ESP-ME3.md): UART 9600 8N1, frame
1C <len> 00 <payload> <crc8 poly 0x07> 1D, escaping 1B 01/02/03, pyrainbird SIP commands.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, text_sensor, uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor"]

rainbird_ns = cg.esphome_ns.namespace("rainbird_me3")
RainbirdME3 = rainbird_ns.class_("RainbirdME3", cg.PollingComponent, uart.UARTDevice)

CONF_MODEL = "model"
CONF_CONTROLLER_TIME = "controller_time"
CONF_CONTROLLER_DATE = "controller_date"
CONF_ACTIVE_ZONES = "active_zones"
CONF_ACTIVE_ZONE = "active_zone"
CONF_RAIN_DELAY = "rain_delay"
CONF_IRRIGATION_STATE = "irrigation_state"
CONF_RAIN_SENSOR = "rain_sensor"
CONF_LINK = "link"
CONF_LAST_FRAME = "last_frame"

TEXT_SENSORS = (CONF_MODEL, CONF_CONTROLLER_TIME, CONF_CONTROLLER_DATE, CONF_ACTIVE_ZONES, CONF_LAST_FRAME)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(RainbirdME3),
            cv.Optional(CONF_MODEL): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_CONTROLLER_TIME): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_CONTROLLER_DATE): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_ACTIVE_ZONES): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_LAST_FRAME): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_ACTIVE_ZONE): sensor.sensor_schema(accuracy_decimals=0),
            cv.Optional(CONF_RAIN_DELAY): sensor.sensor_schema(unit_of_measurement="j", accuracy_decimals=0),
            cv.Optional(CONF_IRRIGATION_STATE): sensor.sensor_schema(accuracy_decimals=0),
            cv.Optional(CONF_RAIN_SENSOR): binary_sensor.binary_sensor_schema(device_class="moisture"),
            cv.Optional(CONF_LINK): binary_sensor.binary_sensor_schema(device_class="connectivity"),
        }
    )
    .extend(cv.polling_component_schema("15s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "rainbird_me3", baud_rate=9600, require_tx=True, require_rx=True, data_bits=8, parity="NONE", stop_bits=1
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    for key in TEXT_SENSORS:
        if key in config:
            s = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_text")(s))
    for key in (CONF_ACTIVE_ZONE, CONF_RAIN_DELAY, CONF_IRRIGATION_STATE):
        if key in config:
            s = await sensor.new_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_sensor")(s))
    for key in (CONF_RAIN_SENSOR, CONF_LINK):
        if key in config:
            s = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_binary")(s))
