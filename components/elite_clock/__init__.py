"""CYD-Elite: tumbling Elite ships + clock, drawn on any ESPHome display."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import time
from esphome.const import CONF_ID, CONF_TIME_ID

CODEOWNERS = ["@DJL-F3D"]
DEPENDENCIES = ["display"]

elite_clock_ns = cg.esphome_ns.namespace("elite_clock")
EliteClock = elite_clock_ns.class_("EliteClock", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EliteClock),
        cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.Optional("fallback_time_id"): cv.use_id(time.RealTimeClock),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_TIME_ID in config:
        t = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(t))
    if "fallback_time_id" in config:
        t = await cg.get_variable(config["fallback_time_id"])
        cg.add(var.set_fallback_time(t))
