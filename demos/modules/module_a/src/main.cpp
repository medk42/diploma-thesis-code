#include "plugin_contract.h"

const char* PLUGIN_NAME = "AWESOME_Module_A";
const uint64_t PLUGIN_VERSION = 13;

const char* readPluginName()
{
    return PLUGIN_NAME;
}

uint64_t readPluginApiVersion()
{
    return PLUGIN_VERSION;
}