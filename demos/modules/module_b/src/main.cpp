#include "plugin_contract.h"

const char* PLUGIN_NAME = "MyFirstModule_B";
const uint64_t PLUGIN_VERSION = 42;

const char* readPluginName()
{
    return PLUGIN_NAME;
}

uint64_t readPluginApiVersion()
{
    return PLUGIN_VERSION;
}