#include "usecase_pick_and_place.h"

#include "module_helpers/pen_messages/message_types.h"
#include "module_helpers/scene_detection_helper/message_types.h"
#include "module_helpers/usecase_wrapper/serialization_helper.h"
#include "module_helpers/pose_utils/pose_utils.h"
#include <chrono>
#include <limits>
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace aergo::default_modules::usecase_pick_and_place;
using namespace aergo::module;
using namespace aergo::module::helpers::usecase_wrapper;
namespace pm = aergo::module::helpers::pen_messages;
namespace sdh = aergo::module::helpers::scene_detection_helper;
namespace pu = aergo::module::helpers::pose_utils;

using json = nlohmann::json;



UsecasePickAndPlace::UsecasePickAndPlace(
    const char* data_path, 
    ICore* core, 
    InputChannelMapInfo channel_map_info, 
    const logging::ILogger* logger, 
    uint64_t module_id, 
    const ModuleInfo* module_info,
    bool supports_multi_program,
    bool supports_pause,
    bool supports_stop
) : BaseUsecase(data_path, core, channel_map_info, logger, module_id, module_info, supports_multi_program, supports_pause, supports_stop),
    robot_wrapper_(*this)
{
    if (!robot_wrapper_.valid())
    {
        log(logging::LogType::ERROR, "UsecasePickAndPlace: Failed to initialize robot wrapper.");
        return;
    }

    if (!getSubscribeChannelByName(pm::pen_message_intent_subscribe_consumer.channel_type_identifier_, pen_message_intent_subscribe_channel_id_))
    {
        log(logging::LogType::ERROR, "UsecasePickAndPlace: Failed to get pen message intent subscribe channel.");
        return;
    }

    if (!getRequestChannelByName(sdh::scene_detection_request_consumer.channel_type_identifier_, scene_detection_request_channel_id_))
    {
        log(logging::LogType::ERROR, "UsecasePickAndPlace: Failed to get scene detection request channel.");
        return;
    }

    auto channel_info = getRequestChannelInfo(scene_detection_request_channel_id_);
    if (channel_info.channel_identifier_count_ != 1 || channel_info.channel_identifier_ == nullptr)
    {
        log(logging::LogType::ERROR, "UsecasePickAndPlace: There should be exactly one scene detection request channel by contract, invalid state");
        return;
    }

    std::map<RequestType, sync_req::RequestChannelInfo> request_type_to_channel = {
        { 
            RequestType::SCENE_DETECTION_REQUEST, 
            {
                .local_channel_id_ = scene_detection_request_channel_id_,
                .target_channel_ = channel_info.channel_identifier_[0]
            }
        }
    };

    sync_request_helper_ = std::make_unique<sync_req::SynchronousRequestHelper<RequestType>>(
        request_type_to_channel, *this
    );

    // Create registry request handler which validates the channel and launches background thread
    registry_request_handler_ = std::make_unique<sdh::RegistryRequestHandler>(
        scene_detection_request_channel_id_,
        this
    );

    if (!registry_request_handler_ || !registry_request_handler_->valid())
    {
        log(logging::LogType::ERROR, "UsecasePickAndPlace: Failed to create registry request handler.");
        return;
    }

    valid_ = true;
}


bool UsecasePickAndPlace::sendRequestFromUsecase(const std::vector<p_desc::ParameterDescription>& auto_parameters, const uint32_t param_id, uint64_t& out_request_id)
{
    if (param_id != 0)
    {
        log(logging::LogType::ERROR, "UsecasePickAndPlace: Expected parameter 0 for scene detection request, got " + std::to_string(param_id));
        return false;
    }

    return sendSceneDetectionRequest(out_request_id);
}


bool UsecasePickAndPlace::sendSceneDetectionRequest(uint64_t& out_request_id)
{
    // Get channel info for the scene detection request channel
    InputChannelMapInfo::IndividualChannelInfo channel_info = getRequestChannelInfo(scene_detection_request_channel_id_);
        
    if (channel_info.channel_identifier_ == nullptr || channel_info.channel_identifier_count_ == 0)
    {
        log(logging::LogType::ERROR, "UsecasePickAndPlace: There should be exactly one scene detection request channel by contract, invalid state");
        return false;
    }

    // Pick the first channel (there's exactly one by contract)
    ChannelIdentifier target_channel = channel_info.channel_identifier_[0];
    sdh::Request request = sdh::Request::readScene();
    message::MessageHeader message = message::MessageHeader::Message(&request);

   out_request_id = sendRequest(scene_detection_request_channel_id_, target_channel, message);
   return true;
}


aergo::module::IModule::IngressDecision UsecasePickAndPlace::onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
{
    if (registry_request_handler_ && registry_request_handler_->handlesIngress(kind, local_channel_id, src, msg))
    {
        return registry_request_handler_->processIngress(kind, local_channel_id, src, msg, queue_status);
    }

    if (sync_request_helper_->handlesIngress(kind, local_channel_id, src))
    {
        return sync_request_helper_->onIngress(kind, msg, queue_status);
    }

    if (robot_wrapper_.handlesIngress(kind, local_channel_id, src))
    {
        return robot_wrapper_.onIngress(kind, msg, queue_status);
    }

    // push all other messages to BaseUsecase for decision
    return BaseUsecase::onIngress(kind, local_channel_id, src, msg, queue_status);
}


void UsecasePickAndPlace::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{

    if (robot_wrapper_.handlesResponse(request_consumer_id, source_channel))
    {
        robot_wrapper_.processResponse(message);
        return;
    }

    if (registry_request_handler_ && registry_request_handler_->handlesResponse(request_consumer_id, source_channel, message))
    {
        if (registry_request_handler_->processResponse(message))
        {
            return; // handled by registry handler
        }
    } 

    // Handle scene detection response
    if (sync_request_helper_->handlesResponse(request_consumer_id, source_channel))
    {
        sync_request_helper_->processResponse(message);
        return;
    }
    
    // pass all other responses to BaseUsecase
    BaseUsecase::processResponse(request_consumer_id, source_channel, message);
}
void UsecasePickAndPlace::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    robot_wrapper_.processMessage(subscribe_consumer_id, message);

    // pass all messages also to BaseUsecase
    BaseUsecase::processMessage(subscribe_consumer_id, source_channel, message);
}


UsecasePickAndPlace::ProcessingResult UsecasePickAndPlace::processCustomMessageOrResponse(
    ProcessingChannelType channel_type, 
    uint32_t consumer_id, 
    ChannelIdentifier source_channel, 
    message::MessageHeader message, 
    std::vector<uint8_t>& out_data_replace
)
{
    if (channel_type == ProcessingChannelType::MESSAGE)
    {
        // Handle pen message intent
        if (consumer_id != pen_message_intent_subscribe_channel_id_)
        {
            log(logging::LogType::WARNING, "UsecasePickAndPlace: Received message from unexpected channel.");
            return ProcessingResult::DROP;
        }

        pm::PenMessageIntent pen_message_intent;
        if (!message.readAs(pen_message_intent))
        {
            log(logging::LogType::WARNING, "UsecasePickAndPlace: Received message is not a pen message intent.");
            return ProcessingResult::DROP;
        }

        if (pen_message_intent.intent != pm::PenIntent::POSE)
        {
            return ProcessingResult::DROP;
        }

        out_data_replace.clear();
        out_data_replace.reserve(sizeof(double) * 7);

        using aergo::module::helpers::serialization_helper::serialization::push;
        push<double>(out_data_replace, pen_message_intent.pose.x);
        push<double>(out_data_replace, pen_message_intent.pose.y);
        push<double>(out_data_replace, pen_message_intent.pose.z);
        push<double>(out_data_replace, pen_message_intent.pose.qx);
        push<double>(out_data_replace, pen_message_intent.pose.qy);
        push<double>(out_data_replace, pen_message_intent.pose.qz);
        push<double>(out_data_replace, pen_message_intent.pose.qw);

        return ProcessingResult::ACCEPT_REPLACE;
    }
    else if (channel_type == ProcessingChannelType::RESPONSE)
    {
        // Handle scene detection response
        if (consumer_id != scene_detection_request_channel_id_)
        {
            log(logging::LogType::WARNING, "UsecasePickAndPlace: Received response from unexpected channel.");
            return ProcessingResult::DROP;
        }

        sdh::Response response;
        if (!message.readAs(response))
        {
            log(logging::LogType::WARNING, "UsecasePickAndPlace: Received response is not a scene detection response.");
            return ProcessingResult::DROP;
        }

        // Check version
        if (response.version != sdh::SCENE_DETECTION_MESSAGE_VERSION)
        {
            log(logging::LogType::WARNING, "UsecasePickAndPlace: Scene detection response has invalid version: " + std::to_string(response.version) + ", expected " + std::to_string(sdh::SCENE_DETECTION_MESSAGE_VERSION) + ".");
            return ProcessingResult::DROP;
        }

        // Only accept READ_SCENE responses, anything else is a failure
        if (response.req_type != sdh::ReqType::READ_SCENE)
        {
            log(logging::LogType::WARNING, "UsecasePickAndPlace: Scene detection response has unexpected request type: " + std::to_string(static_cast<int>(response.req_type)) + ", expected READ_SCENE.");
            return ProcessingResult::DROP;
        }

        // Try to parse READ_SCENE response
        bool parse_success = false;
        if (message.blob_count_ == 1 && message.blobs_ != nullptr)
        {
            std::vector<sdh::DetectedBox> boxes;
            parse_success = response.parseScene(message.blobs_[0].data(), message.blobs_[0].size(), boxes);
        }

        if (parse_success)
        {
            return ProcessingResult::ACCEPT;
        }
        else
        {
            log(logging::LogType::WARNING, "UsecasePickAndPlace: Failed to parse scene detection response.");
            return ProcessingResult::DROP;
        }
    }

    // Unknown channel type
    return ProcessingResult::DROP;
}


std::expected<void, uw::helper::ErrorInfo> UsecasePickAndPlace::createCommandFromParameters(
    const uw::p_desc::ParameterList& auto_parameters,
    const uw::p_desc::ParameterList& required_parameters,
    const uw::p_desc::ParameterList& advanced_parameters,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& auto_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& required_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
    nlohmann::json& out_command_json,
    uw::IUsecaseModule::UsecaseVisualization& out_visualization
)
{
    // Validate auto parameters count
    if (auto_parameter_values.size() != 3 || auto_parameter_values[0].size() != 1 || auto_parameter_values[1].size() != 1 || auto_parameter_values[2].size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecasePickAndPlace: Expected 3 auto parameter values with exactly one value each, got " + std::to_string(auto_parameter_values.size())));
    }

    // Deserialize Scene Request (auto parameter 0)
    const auto& scene_data = std::get<std::vector<uint8_t>>(auto_parameter_values[0][0].value_);
    std::vector<sdh::DetectedBox> detected_boxes;
    auto scene_result = deserializeSceneResponse(scene_data, detected_boxes);
    if (!scene_result)
    {
        return scene_result;
    }

    // Deserialize Pick Position (auto parameter 1)
    const auto& pick_data = std::get<std::vector<uint8_t>>(auto_parameter_values[1][0].value_);
    sdh::Pose pick_pose;
    auto pick_result = deserializePose(pick_data, pick_pose);
    if (!pick_result)
    {
        return pick_result;
    }

    // Deserialize Place Position (auto parameter 2)
    const auto& place_data = std::get<std::vector<uint8_t>>(auto_parameter_values[2][0].value_);
    sdh::Pose place_pose;
    auto place_result = deserializePose(place_data, place_pose);
    if (!place_result)
    {
        return place_result;
    }

    // Deserialize advanced parameters (with units)
    double movement_speed_mm_s;
    double movement_acceleration_mm_s2;
    double pick_detection_distance_mm;
    double lift_distance_mm;
    auto advanced_result = deserializeAdvancedParameters(
        advanced_parameter_values,
        movement_speed_mm_s,
        movement_acceleration_mm_s2,
        pick_detection_distance_mm,
        lift_distance_mm
    );
    if (!advanced_result)
    {
        return advanced_result;
    }

    // Perform pick detection (pick_detection_distance is in mm)
    auto pick_detection_result = detectPickObject(pick_pose, pick_detection_distance_mm, detected_boxes);
    if (!pick_detection_result)
    {
        return std::unexpected(pick_detection_result.error());
    }

    // Convert movement parameters from mm-based units to meters
    const double movement_speed_m_s = movement_speed_mm_s / 1000.0;
    const double movement_acceleration_m_s2 = movement_acceleration_mm_s2 / 1000.0;
    const double lift_distance_m = lift_distance_mm / 1000.0;

    // Extract pick detection info
    const auto& pick_info = pick_detection_result.value();

    // Convert SE3 T_box_pick to quaternion + translation
    cv::Vec4d quat_box_pick;
    cv::Vec3d t_box_pick;
    pick_info.T_box_pick.toQuatTvec(quat_box_pick, t_box_pick, true); // (qw,qx,qy,qz), (tx,ty,tz)

    // Build command JSON
    nlohmann::json command_json;

    // Store T_box_pick as quaternion + translation
    command_json["T_box_pick"] = {
        {"qw", quat_box_pick[0]},
        {"qx", quat_box_pick[1]},
        {"qy", quat_box_pick[2]},
        {"qz", quat_box_pick[3]},
        {"tx", t_box_pick[0]},
        {"ty", t_box_pick[1]},
        {"tz", t_box_pick[2]}
    };

    // Store box identifier
    command_json["box_id"] = pick_info.box_id;

    // Store motion parameters in meters
    command_json["movement_speed_m_s"] = movement_speed_m_s;
    command_json["movement_acceleration_m_s2"] = movement_acceleration_m_s2;
    command_json["lift_distance_m"] = lift_distance_m;

    // Store place pose (world frame) as quaternion + translation
    command_json["place_pose"] = {
        {"x",  place_pose.x},
        {"y",  place_pose.y},
        {"z",  place_pose.z},
        {"qx", place_pose.qx},
        {"qy", place_pose.qy},
        {"qz", place_pose.qz},
        {"qw", place_pose.qw}
    };

    out_visualization = uw::IUsecaseModule::UsecaseVisualization {
        .supports_visualization = true,
        .poses = {
            {
                .position = {
                    .x = pick_pose.x,
                    .y = pick_pose.y,
                    .z = pick_pose.z
                },
                .orientation = {
                    .qw = pick_pose.qw,
                    .qx = pick_pose.qx,
                    .qy = pick_pose.qy,
                    .qz = pick_pose.qz
                }
            },
            {
                .position = {
                    .x = place_pose.x,
                    .y = place_pose.y,
                    .z = place_pose.z
                },
                .orientation = {
                    .qw = place_pose.qw,
                    .qx = place_pose.qx,
                    .qy = place_pose.qy,
                    .qz = place_pose.qz
                }
            }
        },
        .points = {
            {
                .x = pick_info.box_touch_point[0],
                .y = pick_info.box_touch_point[1],
                .z = pick_info.box_touch_point[2]
            }
        },
        .trajectories = {
            {
                {
                    .x = pick_pose.x,
                    .y = pick_pose.y,
                    .z = pick_pose.z
                },
                {
                    .x = pick_pose.x,
                    .y = pick_pose.y,
                    .z = pick_pose.z + lift_distance_m
                },
                {
                    .x = place_pose.x,
                    .y = place_pose.y,
                    .z = place_pose.z + lift_distance_m
                },
                {
                    .x = place_pose.x,
                    .y = place_pose.y,
                    .z = place_pose.z
                }
            },
        }
    };

    out_command_json = std::move(command_json);
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecasePickAndPlace::validateParameters(const nlohmann::json& command_json)
{
    // Validate T_box_pick
    if (!command_json.contains("T_box_pick") || !command_json["T_box_pick"].is_object())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecasePickAndPlace: command JSON missing 'T_box_pick' object."));
    }
    const auto& T_box_pick = command_json["T_box_pick"];
    if (!T_box_pick.contains("qw") || !T_box_pick["qw"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecasePickAndPlace: command JSON missing 'T_box_pick.qw' number."));
    }
    if (!T_box_pick.contains("qx") || !T_box_pick["qx"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "UsecasePickAndPlace: command JSON missing 'T_box_pick.qx' number."));
    }
    if (!T_box_pick.contains("qy") || !T_box_pick["qy"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecasePickAndPlace: command JSON missing 'T_box_pick.qy' number."));
    }
    if (!T_box_pick.contains("qz") || !T_box_pick["qz"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecasePickAndPlace: command JSON missing 'T_box_pick.qz' number."));
    }
    if (!T_box_pick.contains("tx") || !T_box_pick["tx"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "UsecasePickAndPlace: command JSON missing 'T_box_pick.tx' number."));
    }
    if (!T_box_pick.contains("ty") || !T_box_pick["ty"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "UsecasePickAndPlace: command JSON missing 'T_box_pick.ty' number."));
    }
    if (!T_box_pick.contains("tz") || !T_box_pick["tz"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "UsecasePickAndPlace: command JSON missing 'T_box_pick.tz' number."));
    }

    // Validate box_id
    if (!command_json.contains("box_id") || !command_json["box_id"].is_number_unsigned())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(9, "UsecasePickAndPlace: command JSON missing 'box_id' unsigned integer."));
    }

    // Validate movement parameters in meters
    if (!command_json.contains("movement_speed_m_s") || !command_json["movement_speed_m_s"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(10, "UsecasePickAndPlace: command JSON missing 'movement_speed_m_s' number."));
    }
    if (!command_json.contains("movement_acceleration_m_s2") || !command_json["movement_acceleration_m_s2"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(11, "UsecasePickAndPlace: command JSON missing 'movement_acceleration_m_s2' number."));
    }
    if (!command_json.contains("lift_distance_m") || !command_json["lift_distance_m"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(12, "UsecasePickAndPlace: command JSON missing 'lift_distance_m' number."));
    }

    const double speed_m_s = command_json["movement_speed_m_s"].get<double>();
    const double accel_m_s2 = command_json["movement_acceleration_m_s2"].get<double>();
    const double lift_m = command_json["lift_distance_m"].get<double>();
    if (speed_m_s <= 0.0 || accel_m_s2 <= 0.0 || lift_m <= 0.0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(13, "UsecasePickAndPlace: movement_speed_m_s, movement_acceleration_m_s2 and lift_distance_m must be greater than 0."));
    }

    // Validate place_pose
    if (!command_json.contains("place_pose") || !command_json["place_pose"].is_object())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(14, "UsecasePickAndPlace: command JSON missing 'place_pose' object."));
    }
    const auto& place_pose = command_json["place_pose"];
    if (!place_pose.contains("x") || !place_pose["x"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(15, "UsecasePickAndPlace: command JSON missing 'place_pose.x' number."));
    }
    if (!place_pose.contains("y") || !place_pose["y"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(16, "UsecasePickAndPlace: command JSON missing 'place_pose.y' number."));
    }
    if (!place_pose.contains("z") || !place_pose["z"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(17, "UsecasePickAndPlace: command JSON missing 'place_pose.z' number."));
    }
    if (!place_pose.contains("qx") || !place_pose["qx"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(18, "UsecasePickAndPlace: command JSON missing 'place_pose.qx' number."));
    }
    if (!place_pose.contains("qy") || !place_pose["qy"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(19, "UsecasePickAndPlace: command JSON missing 'place_pose.qy' number."));
    }
    if (!place_pose.contains("qz") || !place_pose["qz"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(20, "UsecasePickAndPlace: command JSON missing 'place_pose.qz' number."));
    }
    if (!place_pose.contains("qw") || !place_pose["qw"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(21, "UsecasePickAndPlace: command JSON missing 'place_pose.qw' number."));
    }

    // All checks passed
    return std::expected<void, uw::helper::ErrorInfo>{};
}


namespace
{
    // Helper to deserialize command JSON into strongly-typed values used by runProgram.
    inline void deserializeCommandJson(
        const nlohmann::json& command_json,
        rc::Pose& out_place_pose,
        double& out_movement_speed_m_s,
        double& out_movement_acceleration_m_s2,
        double& out_lift_distance_m,
        uint64_t& out_box_id,
        pu::SE3& out_T_box_pick
    )
    {
        // Extract place_pose
        const auto& place_pose = command_json["place_pose"];
        out_place_pose = rc::Pose{
            .position = {
                .x = place_pose["x"].get<double>(),
                .y = place_pose["y"].get<double>(),
                .z = place_pose["z"].get<double>()
            },
            .orientation = {
                .x = place_pose["qx"].get<double>(),
                .y = place_pose["qy"].get<double>(),
                .z = place_pose["qz"].get<double>(),
                .w = place_pose["qw"].get<double>()
            }
        };

        // Extract motion parameters (already in meters / m^2)
        out_movement_speed_m_s = command_json["movement_speed_m_s"].get<double>();
        out_movement_acceleration_m_s2 = command_json["movement_acceleration_m_s2"].get<double>();
        out_lift_distance_m = command_json["lift_distance_m"].get<double>();

        // Box id
        out_box_id = command_json["box_id"].get<uint64_t>();

        // Extract T_box_pick from quaternion + translation
        const auto& T_box_pick = command_json["T_box_pick"];
        cv::Vec4d quat(
            T_box_pick["qw"].get<double>(),
            T_box_pick["qx"].get<double>(),
            T_box_pick["qy"].get<double>(),
            T_box_pick["qz"].get<double>()
        );
        cv::Vec3d tvec(
            T_box_pick["tx"].get<double>(),
            T_box_pick["ty"].get<double>(),
            T_box_pick["tz"].get<double>()
        );
        out_T_box_pick = pu::SE3::fromQuatTvec(quat, tvec, /*reorthonormalize=*/true);
    }
}


std::expected<void, uw::helper::ErrorInfo> UsecasePickAndPlace::runProgram(const nlohmann::json& command_json, bool simulated)
{
    // ----- Begin JSON deserialization for pick-and-place runProgram -----
    rc::Pose place_pose{};
    double movement_speed_m_s = 0.0;
    double movement_acceleration_m_s2 = 0.0;
    double lift_distance_m = 0.0;
    uint64_t box_id = 0;
    pu::SE3 T_box_pick = pu::SE3::unit();

    deserializeCommandJson(
        command_json,
        place_pose,
        movement_speed_m_s,
        movement_acceleration_m_s2,
        lift_distance_m,
        box_id,
        T_box_pick
    );



    auto request = sdh::Request::readScene();
    sdh::Response response;
    std::vector<std::vector<std::byte>> response_blobs;

    sync_req::RequestResult sync_result = sync_request_helper_->sendSynchronousRequest(
        RequestType::SCENE_DETECTION_REQUEST,
        request,
        std::span<std::byte>(),
        response,
        &response_blobs,
        nullptr,
        5000 // 5 seconds timeout
    );

    if (sync_result != sync_req::RequestResult::SUCCESS)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecasePickAndPlace: Failed to send scene detection request to scene detection helper: " + std::string(sync_result == sync_req::RequestResult::TIMEOUT ? "TIMEOUT" : "UNKNOWN_ERROR")));
    }

    if (response.req_type != sdh::ReqType::READ_SCENE || response_blobs.size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecasePickAndPlace: Scene detection response has unexpected request type or blob count."));
    }

    std::vector<sdh::DetectedBox> detected_boxes;
    if (!response.parseScene(response_blobs[0].data(), response_blobs[0].size(), detected_boxes))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "UsecasePickAndPlace: Failed to parse scene detection response."));
    }

    bool box_found = false;
    pu::SE3 T_world_box = pu::SE3::unit();
    for (const auto& box : detected_boxes)
    {
        if (box.id == box_id)
        {
            cv::Vec4d quat(box.pose.qw, box.pose.qx, box.pose.qy, box.pose.qz);
            cv::Vec3d tvec(box.pose.x, box.pose.y, box.pose.z);
            T_world_box = pu::SE3::fromQuatTvec(quat, tvec, true);
            
            box_found = true;

            break;
        }
    }

    if (!box_found)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecasePickAndPlace: Box with ID " + std::to_string(box_id) + " not found in scene detection response."));
    }

    handleControlRequests(false, true); // check for stop request before starting execution (ends the runProgram early via StopException if stop is requested)

    pu::SE3 T_world_pick = T_world_box * T_box_pick;

    cv::Vec3d pick_position;
    cv::Vec4d pick_quat;
    T_world_pick.toQuatTvec(pick_quat, pick_position, true);

    rc::Pose pick_pose = {
        .position = {
            .x = pick_position[0],
            .y = pick_position[1],
            .z = pick_position[2]
        },
        .orientation = {
            .x = pick_quat[1],
            .y = pick_quat[2],
            .z = pick_quat[3],
            .w = pick_quat[0]
        }
    };

    rc::Pose above_pick_pose = pick_pose;
    above_pick_pose.position.z += lift_distance_m;

    rc::Pose above_place_pose = place_pose;
    above_place_pose.position.z += lift_distance_m;


    // Move above pick pose
    auto move_res = moveLinear(above_pick_pose, movement_speed_m_s, movement_acceleration_m_s2);
    if (move_res.error) return std::unexpected(*move_res.error);
    if (move_res.stopped) handleControlRequests(false, true); // if stopped during move, handle stop request

    // Move to pick pose
    move_res = moveLinear(pick_pose, movement_speed_m_s, movement_acceleration_m_s2);
    if (move_res.error) return std::unexpected(*move_res.error);
    if (move_res.stopped) handleControlRequests(false, true);

    log(logging::LogType::INFO, "UsecasePickAndPlace: Closing gripper...");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    log(logging::LogType::INFO, "UsecasePickAndPlace: Gripper closed.");

    // Move above pick pose
    move_res = moveLinear(above_pick_pose, movement_speed_m_s, movement_acceleration_m_s2);
    if (move_res.error) return std::unexpected(*move_res.error);
    if (move_res.stopped) handleControlRequests(false, true);

    // Move above place pose
    move_res = moveLinear(above_place_pose, movement_speed_m_s, movement_acceleration_m_s2);
    if (move_res.error) return std::unexpected(*move_res.error);
    if (move_res.stopped) handleControlRequests(false, true);

    // Move to place pose
    move_res = moveLinear(place_pose, movement_speed_m_s, movement_acceleration_m_s2);
    if (move_res.error) return std::unexpected(*move_res.error);
    if (move_res.stopped) handleControlRequests(false, true);

    log(logging::LogType::INFO, "UsecasePickAndPlace: Opening gripper...");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    log(logging::LogType::INFO, "UsecasePickAndPlace: Gripper opened.");

    // Move above place pose
    move_res = moveLinear(above_place_pose, movement_speed_m_s, movement_acceleration_m_s2);
    if (move_res.error) return std::unexpected(*move_res.error);
    if (move_res.stopped) handleControlRequests(false, true);


    return std::expected<void, uw::helper::ErrorInfo>{};
}


UsecasePickAndPlace::AsyncResult UsecasePickAndPlace::moveLinear(const rc::Pose& pose, double speed, double acceleration)
{
    rc::MoveRequestResult res = robot_wrapper_.moveLinear(pose, speed, acceleration, false);
    if (!res.success_)
    {
        return AsyncResult {
            .stopped = false,
            .error = uw::helper::ErrorInfo::WithDetails(
                1, 
                "UsecasePickAndPlace: Failed to send move linear command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)
            )
        };
    }
    return asyncWaitForFinish(res.action_id_);
}


UsecasePickAndPlace::AsyncResult UsecasePickAndPlace::asyncWaitForFinish(uint64_t action_id)
{
    bool cancel_requested = false;
    while (robot_wrapper_.isActionActive(action_id))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (cancel_requested)
        {
            continue; // already requested cancel, just wait for action to end
        }

        auto [pause_requested, stop_requested] = checkControlRequests();
        if (stop_requested)
        {
            log(logging::LogType::INFO, "UsecasePickAndPlace: Stop requested, cancelling robot action " + std::to_string(action_id) + ".");

            rc::MoveRequestResult cancel_res = robot_wrapper_.cancelAction(action_id);
            if (!cancel_res.success_)
            {
                return AsyncResult {
                    .stopped = true,
                    .error = uw::helper::ErrorInfo::WithDetails(
                        5, 
                        "UsecasePickAndPlace: Failed to send cancel command to robot for action " + std::to_string(action_id) + ": " + (cancel_res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : cancel_res.err_message_)
                    )
                };
            }
            cancel_requested = true;
        }
    }

    return AsyncResult {
        .stopped = cancel_requested,
        .error = std::nullopt
    };
}

std::expected<void, uw::helper::ErrorInfo> UsecasePickAndPlace::deserializeSceneResponse(
    const std::vector<uint8_t>& scene_data,
    std::vector<sdh::DetectedBox>& out_detected_boxes
)
{
    // Deserialize message using usecase_wrapper serialization_helper
    using aergo::module::helpers::serialization_helper::deserialization::BufferReader;
    BufferReader reader(scene_data.data(), scene_data.size());
    uw::deserialize::MessageData message_data;
    if (!uw::deserialize::readMessage(reader, message_data))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecasePickAndPlace: Failed to deserialize scene response message."));
    }

    // Read Response struct from message data
    if (message_data.data_.size() < sizeof(sdh::Response))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecasePickAndPlace: Scene response data too small."));
    }
    
    sdh::Response scene_response;
    std::memcpy(&scene_response, message_data.data_.data(), sizeof(sdh::Response));
    
    // Validate scene response version
    if (scene_response.version != sdh::SCENE_DETECTION_MESSAGE_VERSION)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "UsecasePickAndPlace: Scene response has invalid version: " + std::to_string(scene_response.version) + ", expected " + std::to_string(sdh::SCENE_DETECTION_MESSAGE_VERSION) + "."));
    }
    
    // Validate scene response type (must be READ_SCENE)
    if (scene_response.req_type != sdh::ReqType::READ_SCENE)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "UsecasePickAndPlace: Scene response has unexpected request type: " + std::to_string(static_cast<int>(scene_response.req_type)) + ", expected READ_SCENE."));
    }
    
    // Parse scene response blob
    out_detected_boxes.clear();
    if (scene_response.count > 0)
    {
        if (message_data.blobs_.size() != 1)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "UsecasePickAndPlace: Scene response indicates " + std::to_string(scene_response.count) + " boxes but blob data is missing or invalid."));
        }
        const void* blob_data = message_data.blobs_[0].data();
        size_t blob_size = message_data.blobs_[0].size();
        if (!scene_response.parseScene(blob_data, blob_size, out_detected_boxes))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(9, "UsecasePickAndPlace: Failed to parse scene response data."));
        }
    }
    // If count is 0, out_detected_boxes will be empty, which is valid
    
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecasePickAndPlace::deserializePose(
    const std::vector<uint8_t>& pose_data,
    sdh::Pose& out_pose
)
{
    // Match the serialization format from processCustomMessageOrResponse:
    // 7 doubles: x, y, z, qx, qy, qz, qw
    using aergo::module::helpers::serialization_helper::deserialization::BufferReader;
    BufferReader reader(pose_data.data(), pose_data.size());
    if (!reader.read<double>(out_pose.x) ||
        !reader.read<double>(out_pose.y) ||
        !reader.read<double>(out_pose.z) ||
        !reader.read<double>(out_pose.qx) ||
        !reader.read<double>(out_pose.qy) ||
        !reader.read<double>(out_pose.qz) ||
        !reader.read<double>(out_pose.qw))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(10, "UsecasePickAndPlace: Failed to read pose from parameter value."));
    }
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecasePickAndPlace::deserializeAdvancedParameters(
    const std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
    double& out_movement_speed_mm_s,
    double& out_movement_acceleration_mm_s2,
    double& out_pick_detection_distance_mm,
    double& out_lift_distance_mm
)
{
    // Validate advanced parameters count
    if (advanced_parameter_values.size() != 4)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(11, "UsecasePickAndPlace: Expected 4 advanced parameter values, got " + std::to_string(advanced_parameter_values.size())));
    }

    // Validate each advanced parameter has exactly one value
    for (size_t i = 0; i < 4; ++i)
    {
        if (advanced_parameter_values[i].size() != 1)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(12, "UsecasePickAndPlace: Advanced parameter " + std::to_string(i) + " expected 1 value, got " + std::to_string(advanced_parameter_values[i].size())));
        }
        if (!std::holds_alternative<double>(advanced_parameter_values[i][0].value_))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(13, "UsecasePickAndPlace: Advanced parameter " + std::to_string(i) + " expected double, got " + std::to_string(advanced_parameter_values[i][0].value_.index())));
        }
    }

    // Parse advanced parameters (with units)
    out_movement_speed_mm_s = std::get<double>(advanced_parameter_values[0][0].value_);
    out_movement_acceleration_mm_s2 = std::get<double>(advanced_parameter_values[1][0].value_);
    out_pick_detection_distance_mm = std::get<double>(advanced_parameter_values[2][0].value_);
    out_lift_distance_mm = std::get<double>(advanced_parameter_values[3][0].value_);

    // Validate advanced parameter values (include units in messages)
    if (out_movement_speed_mm_s <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(14, "UsecasePickAndPlace: Movement speed [mm/s] must be positive, got " + std::to_string(out_movement_speed_mm_s) + " mm/s."));
    }
    if (out_movement_acceleration_mm_s2 <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(15, "UsecasePickAndPlace: Movement acceleration [mm/s²] must be positive, got " + std::to_string(out_movement_acceleration_mm_s2) + " mm/s²."));
    }
    if (out_pick_detection_distance_mm <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(16, "UsecasePickAndPlace: Pick detection distance [mm] must be positive, got " + std::to_string(out_pick_detection_distance_mm) + " mm."));
    }
    if (out_lift_distance_mm <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(17, "UsecasePickAndPlace: Lift distance [mm] must be positive, got " + std::to_string(out_lift_distance_mm) + " mm."));
    }
    
    return std::expected<void, uw::helper::ErrorInfo>{};
}


// Helper functions for converting between scene_detection_helper::Pose and pose_utils::SE3
namespace
{
    // Convert scene_detection_helper::Pose to pose_utils::SE3
    // scene_detection_helper uses (qx, qy, qz, qw), pose_utils uses (qw, qx, qy, qz)
    pu::SE3 poseToSE3(const sdh::Pose& pose)
    {
        cv::Vec4d quat(pose.qw, pose.qx, pose.qy, pose.qz); // (qw, qx, qy, qz)
        cv::Vec3d tvec(pose.x, pose.y, pose.z);
        return pu::SE3::fromQuatTvec(quat, tvec, false);
    }
    
    bool segmentIntersectsOrientedBox(
        const cv::Vec3d& line_start_world,
        const cv::Vec3d& line_end_world,
        const pu::SE3&   T_world_box,
        const cv::Vec3d& half_extents_box,   // (hx, hy, hz) in box space
        double&          out_distance_world) // distance from line_start_world to first intersection, in world units
    {
        constexpr double kEps = 1e-12;
    
        const cv::Vec3d seg_world = line_end_world - line_start_world;
        const double seg_len_world = cv::norm(seg_world);
    
        // Degenerate segment: treat as a point test
        if (seg_len_world < kEps)
        {
            const pu::SE3 T_box_world = T_world_box.inverse();
            const cv::Vec3d p_box = T_box_world * line_start_world;
    
            const cv::Vec3d mn(-half_extents_box[0], -half_extents_box[1], -half_extents_box[2]);
            const cv::Vec3d mx( half_extents_box[0],  half_extents_box[1],  half_extents_box[2]);
    
            const bool inside =
                (p_box[0] >= mn[0] && p_box[0] <= mx[0]) &&
                (p_box[1] >= mn[1] && p_box[1] <= mx[1]) &&
                (p_box[2] >= mn[2] && p_box[2] <= mx[2]);
    
            if (!inside) return false;
            out_distance_world = 0.0;
            return true;
        }
    
        // Transform endpoints into box space
        const pu::SE3 T_box_world = T_world_box.inverse();
        const cv::Vec3d p0 = T_box_world * line_start_world;
        const cv::Vec3d p1 = T_box_world * line_end_world;
        const cv::Vec3d d  = p1 - p0; // segment direction in box space, parameter t in [0,1]
    
        const cv::Vec3d mn(-half_extents_box[0], -half_extents_box[1], -half_extents_box[2]);
        const cv::Vec3d mx( half_extents_box[0],  half_extents_box[1],  half_extents_box[2]);
    
        double tmin = 0.0; // entry
        double tmax = 1.0; // exit
    
        for (int axis = 0; axis < 3; ++axis)
        {
            const double p = p0[axis];
            const double v = d[axis];
            const double slab_min = mn[axis];
            const double slab_max = mx[axis];
    
            if (std::abs(v) < kEps)
            {
                // Segment is parallel to this axis' slabs; must be inside to possibly intersect
                if (p < slab_min || p > slab_max)
                    return false;
            }
            else
            {
                double t1 = (slab_min - p) / v;
                double t2 = (slab_max - p) / v;
                if (t1 > t2) std::swap(t1, t2);
    
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
    
                if (tmin > tmax)
                    return false;
            }
        }
    
        // tmin is the first intersection along the segment in [0,1]
        // If start is inside the box, tmin stays at 0 => distance 0 (hit immediately).
        const double t_hit = tmin;
        out_distance_world = seg_len_world * t_hit;
        return true;
    }
}
    


std::expected<UsecasePickAndPlace::PickDetectionResult, uw::helper::ErrorInfo> UsecasePickAndPlace::detectPickObject(
    const sdh::Pose& pick_pose,
    double pick_detection_distance,
    const std::vector<sdh::DetectedBox>& detected_boxes
)
{
    double pick_detection_distance_m = pick_detection_distance / 1000.0;
            
    pu::SE3 T_world_pick = poseToSE3(pick_pose);
    cv::Vec3d line_start_world = T_world_pick * cv::Vec3d(0, 0, 0);
    cv::Vec3d line_end_world = T_world_pick * cv::Vec3d(0, 0, pick_detection_distance_m);

    double min_distance = std::numeric_limits<double>::max();
    const sdh::DetectedBox* closest_box = nullptr;

    // Get a snapshot of the registered boxes from the registry handler
    std::optional<std::vector<sdh::RegisteredBox>> registered_boxes_opt;
    if (registry_request_handler_)
    {
        registered_boxes_opt = registry_request_handler_->getRegisteredBoxesSnapshot();
    }

    if (!registered_boxes_opt)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(18, "UsecasePickAndPlace: Failed to get registered boxes snapshot from registry request handler."));
    }

    const auto& registered_boxes = registered_boxes_opt.value();

    for (const auto& detected_box : detected_boxes)
    {
        auto it = std::find_if(registered_boxes.begin(), registered_boxes.end(),
            [detected_box](const sdh::RegisteredBox& box)
            {
                return box.id == detected_box.id;
            });
        if (it == registered_boxes.end())
        {
            continue;
        }
        const auto& registered_box = *it;
        
        double distance = -1;
        if (segmentIntersectsOrientedBox(
            line_start_world, 
            line_end_world, 
            poseToSE3(detected_box.pose),
            cv::Vec3d(registered_box.size_x / 2.0, registered_box.size_y / 2.0, registered_box.size_z / 2.0), 
            distance)
        )
        {
            if (distance >= 0 && distance < min_distance)
            {
                min_distance = distance;
                closest_box = &detected_box;
            }
        }
    }
    
    if (closest_box == nullptr)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(19, "UsecasePickAndPlace: No object found within pick detection distance."));
    }
    
    pu::SE3 T_world_box = poseToSE3(closest_box->pose);
    
    return PickDetectionResult{ 
        .box_id = closest_box->id, 
        .T_box_pick = T_world_box.inverse() * T_world_pick,
        .box_touch_point = T_world_pick * cv::Vec3d(0, 0, min_distance)
    };
}