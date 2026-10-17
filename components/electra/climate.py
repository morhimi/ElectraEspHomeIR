import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import climate_ir
from esphome.const import CONF_ID

AUTO_LOAD = ["climate_ir"]
CODEOWNERS = ["@omerstr"]

CONF_SUPPORTS_OFF_COMMAND = "supports_off_command"
electra_ns = cg.esphome_ns.namespace("electra")
ElectraClimate = electra_ns.class_("ElectraClimate", climate_ir.ClimateIR)
SyncStateAction = electra_ns.class_("SyncStateAction", automation.Action)

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(ElectraClimate).extend(
    {
        cv.GenerateID(): cv.declare_id(ElectraClimate),
        cv.Optional(CONF_SUPPORTS_OFF_COMMAND, default=True): cv.boolean,
    }
)


async def to_code(config):
    var = await climate_ir.new_climate_ir(config)
    cg.add(var.set_supports_off_command(config[CONF_SUPPORTS_OFF_COMMAND]))


@automation.register_action(
    "electra.sync_state",
    SyncStateAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(ElectraClimate),
        }
    ),
)
async def electra_sync_state_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)
