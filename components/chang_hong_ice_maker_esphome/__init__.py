import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@local"]
DEPENDENCIES = ["wifi"]

chang_hong_ice_maker_esphome_ns = cg.esphome_ns.namespace("chang_hong_ice_maker_esphome")
ChangHongIceMakerESPHome = chang_hong_ice_maker_esphome_ns.class_(
    "ChangHongIceMakerESPHome", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ChangHongIceMakerESPHome),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
