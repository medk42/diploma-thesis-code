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

    // Publish: message contains PubType as data (1 byte)
    //    PubType::ANNOUNCE: no blobs
    //    PubType::UPDATE: 1 blob with update data (see VisualizationHelper::sendUpdate())
    //        serialization::pushObjectCommands, serialization::pushTrajectoryCommands
    // Request: message contains ReqType as data (1 byte), no blobs
    // Response: message contains ReqType as data (1 byte, copied from request), 1 blob with full scene data
    //    ReqType::READ_FULL: serialization::pushPendingRegistration, serialization::pushSceneObjects, serialization::pushSceneTrajectories
    //    ReqType::READ_SCENE: serialization::pushSceneObjects, serialization::pushSceneTrajectories

    constexpr aergo::module::communication_channel::Producer visualization_3d_interface_publish_producer = {
        .channel_type_identifier_ = "visualization_3d_interface/v1:struct{pub_type:uint8}[+blob{update_data}]",
        .display_name_ = "3D Visualization Interface",
        .display_description_ = "Channel for 3D visualization messages (announce and scene updates).",
    };

    constexpr aergo::module::communication_channel::Consumer visualization_3d_interface_request_consumer = {
        .count_ = aergo::module::communication_channel::Consumer::Count::AUTO_ALL,
        .channel_type_identifier_ = "visualization_3d_interface_req/v1:struct{req_type:uint8}",
        .display_name_ = "3D Visualization Interface Request",
        .display_description_ = "Request channel for 3D visualization interface messages (request full scene data, optionally with registrations).",
        .prioritized_ = true,
        .message_queue_capacity_ = 128 // visualization module can get many responses at once, allow larger queue
    };

    constexpr aergo::module::communication_channel::Producer visualization_3d_interface_response_producer = {
        .channel_type_identifier_ = "visualization_3d_interface_resp/v1:struct{req_type:uint8}+blob{[registrations,]objects,trajectories}",
        .display_name_ = "3D Visualization Interface Response",
        .display_description_ = "Response channel for 3D visualization interface requests (full scene data, optionally with registrations).",
        .prioritized_ = true,
        .message_queue_capacity_ = 4 // usually single visualization module, no need for large queue
    };
}