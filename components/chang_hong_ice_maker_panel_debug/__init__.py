# Copyright (c) 2026 Ljzd-PRO <me@ljzd.link>
# Project: https://github.com/Ljzd-PRO

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@Ljzd-PRO"]
DEPENDENCIES = ["wifi"]

chang_hong_ice_maker_panel_debug_ns = cg.esphome_ns.namespace("chang_hong_ice_maker_panel_debug")
ChangHongIceMakerPanelDebug = chang_hong_ice_maker_panel_debug_ns.class_(
    "ChangHongIceMakerPanelDebug", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ChangHongIceMakerPanelDebug),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
