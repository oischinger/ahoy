import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import (
    button,
    hoymiles
)

from esphome.const import (
    CONF_ID,  
    DEVICE_CLASS_RESTART,
    ENTITY_CATEGORY_CONFIG,    
)

from .. import hoymiles_ns

AUTO_LOAD=[
    "button"
]

HoymilesButton = hoymiles_ns.class_("HoymilesButton", button.Button, cg.Component)

# CONFIG_SCHEMA = (
#    button.button_schema(
#         device_class=DEVICE_CLASS_RESTART, entity_category=ENTITY_CATEGORY_CONFIG
#     )
#     .extend({cv.GenerateID(): cv.use_id(HoymilesButton)})
#     .extend(cv.COMPONENT_SCHEMA)    
# )

CONF_INVERTER_ID = "inverter_id"


CONFIG_SCHEMA = (
    button.button_schema()
    .extend(
        {
            cv.GenerateID(): cv.declare_id(HoymilesButton),
            cv.Required(CONF_INVERTER_ID): cv.validate_id_name,
        }
    )
    .extend(cv.COMPONENT_SCHEMA).extend(hoymiles.HOYMILES_DEVICE_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await button.register_button(var, config)
    await cg.register_component(var, config)
    await hoymiles.register_hoymiles_device(var, config)

    cg.add(var.set_inverter_id(config[CONF_INVERTER_ID]))