import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import time

from esphome.const import (
    CONF_ID,
    CONF_CS_PIN,
    CONF_TIME_ID,
)

hoymiles_ns = cg.esphome_ns.namespace('hoymiles')

HoymilesComponent = hoymiles_ns.class_("HoymilesComponent", cg.Component)

# HoymilesDevice = hoymiles_ns.class_('HoymilesDevice', cg.Component)

CONF_CE_PIN = "ce_pin"
CONF_IRQ_PIN = "irq_pin"
CONF_HOYMILES_ID = "hoymiles_id"

CONF_INVERTERS = "inverters"
CONF_SERIAL_NUMBER = "serialnumber"
CONF_SEND_INTERVAL = "send_interval"
CONF_AMPLIFIER_POWER = "amplifier_power"

HOYMILES_SCHEMA_INVERTER = cv.Schema({
    # cv.GenerateID(): cv.declare_id("hoymiles_inverter_id"),
    # cv.Required(CONF_ID): cv.valid_name,
    cv.Required(CONF_ID): cv.validate_id_name,
    cv.Required(CONF_SERIAL_NUMBER): cv.string,
})

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(HoymilesComponent),
            cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_CS_PIN, default=5): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_CE_PIN, default=4): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_IRQ_PIN, default=17): pins.internal_gpio_output_pin_schema,

            cv.Optional(CONF_SEND_INTERVAL, default=5): cv.int_range(5, 120),
            cv.Optional(CONF_AMPLIFIER_POWER, default=1): cv.int_range(1, 4),

            cv.Required(CONF_INVERTERS, ): [ HOYMILES_SCHEMA_INVERTER ],

        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.has_at_least_one_key(CONF_CS_PIN, CONF_CE_PIN),
)

def to_code(config):
    cg.add_global(hoymiles_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)

    time_ = yield cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time(time_))  

    cs = yield cg.gpio_pin_expression(config[CONF_CS_PIN])
    cg.add(var.set_cs_pin(cs))

    ce = yield cg.gpio_pin_expression(config[CONF_CE_PIN])
    cg.add(var.set_ce_pin(ce))

    irq = yield cg.gpio_pin_expression(config[CONF_IRQ_PIN])
    cg.add(var.set_irq_pin(irq))

    send_interval = config[CONF_SEND_INTERVAL]

    if (send_interval < 5):
        send_interval = 5

    cg.add(var.set_send_interval(send_interval))

    amplifier_power = config[CONF_AMPLIFIER_POWER]
    cg.add(var.set_amplifier_power(amplifier_power))

    count = 0;
    for inverters in config[CONF_INVERTERS]:
        cg.add(var.add_inverter(count, inverters[CONF_ID], inverters[CONF_SERIAL_NUMBER]));
        count = count + 1;    



# A schema to use for all Hoymiles devices, all Hoymiles integrations must extend this!
HOYMILES_DEVICE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_HOYMILES_ID): cv.use_id(HoymilesComponent),
    }
)

async def register_hoymiles_device(var, config):
    """Register a Hoymiles device, setting up all the internal values.
    This is a coroutine, you need to await it with a 'yield' expression!
    """
    parent = await cg.get_variable(config[CONF_HOYMILES_ID])
    cg.add(var.set_hoymiles_parent(parent))

