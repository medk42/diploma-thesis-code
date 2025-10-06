#pragma once

#include "module_common/module_interface_.h"

namespace aergo::module::helpers::visualization_3d_interface
{
    enum class PubType : uint8_t
    {
        ANNOUNCE,
        UPDATE
    };

    enum class ReqType : uint8_t
    {
        READ_FULL, // READ_FULL requests full scene (all resources, all objects, all trajectories)
        READ_SCENE // READ_SCENE requests current scene (all objects, all trajectories, but no resources)
    };
}