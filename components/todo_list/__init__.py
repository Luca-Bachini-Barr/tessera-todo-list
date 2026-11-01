"""To-do list: the ESPHome side. register_plugin() reads the id, version, tile memory and the screen's texts from the
manifest and translations/ beside this folder (docs/FIRMWARE_API.md of the Tessera plugins repository)."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display"]

todo_list_ns = cg.esphome_ns.namespace("todo_list")
TodoList = todo_list_ns.class_("TodoList", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(TodoList)}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
