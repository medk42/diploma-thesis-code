#include "usecase_weld.h"

#include "seam_detector.h"
#include "trajectory_seam_matcher.h"

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

using namespace aergo::default_modules::usecase_weld;
using namespace aergo::module;
using namespace aergo::module::helpers::usecase_wrapper;
namespace ri = aergo::module::helpers::robot_interface;
namespace pm = aergo::module::helpers::pen_messages;
namespace sdh = aergo::module::helpers::scene_detection_helper;
namespace pu = aergo::module::helpers::pose_utils;

using json = nlohmann::json;



UsecaseWeld::UsecaseWeld(
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
    if (!getSubscribeChannelByName(pm::pen_message_intent_subscribe_consumer.channel_type_identifier_, pen_message_intent_subscribe_channel_id_))
    {
        log(logging::LogType::ERROR, "UsecaseWeld: Failed to get pen message intent subscribe channel.");
        return;
    }

    if (!getRequestChannelByName(sdh::scene_detection_request_consumer.channel_type_identifier_, scene_detection_request_channel_id_))
    {
        log(logging::LogType::ERROR, "UsecaseWeld: Failed to get scene detection request channel.");
        return;
    }

    registry_request_handler_ = std::make_unique<sdh::RegistryRequestHandler>(
        scene_detection_request_channel_id_,
        this
    );

    if (!registry_request_handler_ || !registry_request_handler_->valid())
    {
        log(logging::LogType::ERROR, "UsecaseWeld: Failed to create registry request handler.");
        return;
    }

    valid_ = true;
}


bool UsecaseWeld::sendRequestFromUsecase(const std::vector<p_desc::ParameterDescription>& auto_parameters, const uint32_t param_id, uint64_t& out_request_id)
{
    if (param_id != 0)
    {
        log(logging::LogType::ERROR, "UsecaseWeld: Expected parameter 0 for scene detection request, got " + std::to_string(param_id));
        return false;
    }

    return sendSceneDetectionRequest(out_request_id);
}


bool UsecaseWeld::sendSceneDetectionRequest(uint64_t& out_request_id)
{
    // Get channel info for the scene detection request channel
    InputChannelMapInfo::IndividualChannelInfo channel_info = getRequestChannelInfo(scene_detection_request_channel_id_);
        
    if (channel_info.channel_identifier_ == nullptr || channel_info.channel_identifier_count_ == 0)
    {
        log(logging::LogType::ERROR, "UsecaseWeld: There should be exactly one scene detection request channel by contract, invalid state");
        return false;
    }

    // Pick the first channel (there's exactly one by contract)
    ChannelIdentifier target_channel = channel_info.channel_identifier_[0];
    sdh::Request request = sdh::Request::readScene();
    message::MessageHeader message = message::MessageHeader::Message(&request);

   out_request_id = sendRequest(scene_detection_request_channel_id_, target_channel, message);
   return true;
}


aergo::module::IModule::IngressDecision UsecaseWeld::onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
{
    if (registry_request_handler_ && registry_request_handler_->handlesIngress(kind, local_channel_id, src, msg))
    {
        return registry_request_handler_->processIngress(kind, local_channel_id, src, msg, queue_status);
    }

    if (robot_wrapper_.handlesIngress(kind, local_channel_id, src))
    {
        return robot_wrapper_.onIngress(kind, msg, queue_status);
    }

    // push all other messages to BaseUsecase for decision
    return BaseUsecase::onIngress(kind, local_channel_id, src, msg, queue_status);
}


void UsecaseWeld::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
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
    
    // pass all other responses to BaseUsecase
    BaseUsecase::processResponse(request_consumer_id, source_channel, message);
}
void UsecaseWeld::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    robot_wrapper_.processMessage(subscribe_consumer_id, message);

    // pass all messages also to BaseUsecase
    BaseUsecase::processMessage(subscribe_consumer_id, source_channel, message);
}


UsecaseWeld::ProcessingResult UsecaseWeld::processCustomMessageOrResponse(
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
            log(logging::LogType::WARNING, "UsecaseWeld: Received message from unexpected channel.");
            return ProcessingResult::DROP;
        }

        pm::PenMessageIntent pen_message_intent;
        if (!message.readAs(pen_message_intent))
        {
            log(logging::LogType::WARNING, "UsecaseWeld: Received message is not a pen message intent.");
            return ProcessingResult::DROP;
        }

        if (pen_message_intent.intent != pm::PenIntent::TRAJECTORY)
        {
            // We want trajectory here to get the welding trajectory
            return ProcessingResult::DROP;
        }

        std::vector<pm::Pose> trajectory;
        if (!pm::parseTrajectoryBlob(message.blobs_[0].data(), message.blobs_[0].size(), trajectory))
        {
            log(logging::LogType::WARNING, "UsecaseWeld: Failed to parse trajectory blob.");
            return ProcessingResult::DROP;
        }

        if (trajectory.size() < 2)
        {
            log(logging::LogType::WARNING, "UsecaseWeld: Received trajectory with less than 2 poses.");
            return ProcessingResult::DROP;
        }

        out_data_replace.clear();
        out_data_replace.reserve(sizeof(uint64_t) + trajectory.size() * sizeof(double) * 7);
        using aergo::module::helpers::serialization_helper::serialization::push;
        push<uint64_t>(out_data_replace, static_cast<uint64_t>(trajectory.size()));
        for (const auto& pose : trajectory)
        {
            push<double>(out_data_replace, pose.x);
            push<double>(out_data_replace, pose.y);
            push<double>(out_data_replace, pose.z);
            push<double>(out_data_replace, pose.qx);
            push<double>(out_data_replace, pose.qy);
            push<double>(out_data_replace, pose.qz);
            push<double>(out_data_replace, pose.qw);
        }

        return ProcessingResult::ACCEPT_REPLACE;
    }
    else if (channel_type == ProcessingChannelType::RESPONSE)
    {
        // Handle scene detection response
        if (consumer_id != scene_detection_request_channel_id_)
        {
            log(logging::LogType::WARNING, "UsecaseWeld: Received response from unexpected channel.");
            return ProcessingResult::DROP;
        }

        sdh::Response response;
        if (!message.readAs(response))
        {
            log(logging::LogType::WARNING, "UsecaseWeld: Received response is not a scene detection response.");
            return ProcessingResult::DROP;
        }

        // Check version
        if (response.version != sdh::SCENE_DETECTION_MESSAGE_VERSION)
        {
            log(logging::LogType::WARNING, "UsecaseWeld: Scene detection response has invalid version: " + std::to_string(response.version) + ", expected " + std::to_string(sdh::SCENE_DETECTION_MESSAGE_VERSION) + ".");
            return ProcessingResult::DROP;
        }

        // Only accept READ_SCENE responses, anything else is a failure
        if (response.req_type != sdh::ReqType::READ_SCENE)
        {
            log(logging::LogType::WARNING, "UsecaseWeld: Scene detection response has unexpected request type: " + std::to_string(static_cast<int>(response.req_type)) + ", expected READ_SCENE.");
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
            log(logging::LogType::WARNING, "UsecaseWeld: Failed to parse scene detection response.");
            return ProcessingResult::DROP;
        }
    }

    // Unknown channel type
    return ProcessingResult::DROP;
}


namespace
{
    ///@brief Convert a pose to a JSON object. Quaternion is in [qw, qx, qy, qz] format.
    inline nlohmann::json poseToJson(const cv::Vec3d& pos, const cv::Vec4d& quat)
    {
        nlohmann::json json;
        json["x"] = pos[0];
        json["y"] = pos[1];
        json["z"] = pos[2];
        json["qx"] = quat[1];
        json["qy"] = quat[2];
        json["qz"] = quat[3];
        json["qw"] = quat[0];
        return json;
    }


    inline bool isValidPose(const nlohmann::json& pose)
    {
        return pose.is_object()
            && pose.contains("x") && pose["x"].is_number()
            && pose.contains("y") && pose["y"].is_number()
            && pose.contains("z") && pose["z"].is_number()
            && pose.contains("qx") && pose["qx"].is_number()
            && pose.contains("qy") && pose["qy"].is_number()
            && pose.contains("qz") && pose["qz"].is_number()
            && pose.contains("qw") && pose["qw"].is_number();
    }


    inline rc::Pose jsonToPose(const nlohmann::json& pose)
    {
        return rc::Pose{
            .position = {
                .x = pose["x"].get<double>(),
                .y = pose["y"].get<double>(),
                .z = pose["z"].get<double>()
            },
            .orientation = {
                .x = pose["qx"].get<double>(),
                .y = pose["qy"].get<double>(),
                .z = pose["qz"].get<double>(),
                .w = pose["qw"].get<double>()
            }
        };
    }
}

std::expected<void, uw::helper::ErrorInfo> UsecaseWeld::createCommandFromParameters(
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
    if (auto_parameter_values.size() != 2 || auto_parameter_values[0].size() != 1 || auto_parameter_values[1].size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseWeld: Expected 2 auto parameter values with exactly one value each, got " + std::to_string(auto_parameter_values.size())));
    }

    // Deserialize Scene Request (auto parameter 0)
    const auto& scene_data = std::get<std::vector<uint8_t>>(auto_parameter_values[0][0].value_);
    std::vector<sdh::DetectedBox> detected_boxes;
    auto scene_result = deserializeSceneResponse(scene_data, detected_boxes);
    if (!scene_result)
    {
        return scene_result;
    }

    // Deserialize Welding Trajectory (auto parameter 1)
    const auto& weld_trajectory_data = std::get<std::vector<uint8_t>>(auto_parameter_values[1][0].value_);
    std::vector<pm::Pose> trajectory;
    auto weld_result = deserializeTrajectory(weld_trajectory_data, trajectory);
    if (!weld_result)
    {
        return weld_result;
    }

    auto registered_boxes_opt = registry_request_handler_->getRegisteredBoxesSnapshot();
    if (!registered_boxes_opt)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(18, "UsecaseWeld: Failed to get registered boxes from registry request handler."));
    }

    const auto& registered_boxes = registered_boxes_opt.value();

    SeamDetector::Params seam_detector_params;
    SeamDetector seam_detector(seam_detector_params);
    auto seams = seam_detector.detectSeams(detected_boxes, registered_boxes);

    if (seams.empty())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(19, "UsecaseWeld: No seams detected."));
    }


    TrajectorySeamMatcher::Params trajectory_seam_matcher_params;
    TrajectorySeamMatcher trajectory_seam_matcher(trajectory_seam_matcher_params);
    pu::SE3 seam_start_pose;
    pu::SE3 seam_end_pose;
    int seam_index = -1;
    if (!trajectory_seam_matcher.matchTrajectoryToSeam(seams, trajectory, seam_start_pose, seam_end_pose, &seam_index))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(20, "UsecaseWeld: Failed to match trajectory to seam."));
    }

    cv::Vec3d seam_start_pos, seam_end_pos;
    cv::Vec4d seam_start_quat, seam_end_quat;
    seam_start_pose.toQuatTvec(seam_start_quat, seam_start_pos, true);
    seam_end_pose.toQuatTvec(seam_end_quat, seam_end_pos, true);


    // Deserialize advanced parameters (with units)
    double movement_speed_mm_s;
    double welding_speed_mm_s;
    double acceleration_mm_s2;
    double weld_offset_mm;
    double approach_distance_mm;
    auto advanced_result = deserializeAdvancedParameters(
        advanced_parameter_values,
        movement_speed_mm_s,
        welding_speed_mm_s,
        acceleration_mm_s2,
        weld_offset_mm,
        approach_distance_mm
    );
    if (!advanced_result)
    {
        return advanced_result;
    }

    // Convert movement parameters from mm-based units to meters where needed
    const double movement_speed_m_s = movement_speed_mm_s / 1000.0;
    const double welding_speed_m_s = welding_speed_mm_s / 1000.0;
    const double acceleration_m_s2 = acceleration_mm_s2 / 1000.0;
    const double weld_offset_m = weld_offset_mm / 1000.0;
    const double approach_distance_m = approach_distance_mm / 1000.0;


    // Calculate approach and depart positions
    cv::Vec3d approach_pos = seam_start_pose * cv::Vec3d(0, 0, -approach_distance_m);
    cv::Vec3d weld_start_pos = seam_start_pose * cv::Vec3d(0, 0, -weld_offset_m);
    cv::Vec3d weld_end_pos = seam_end_pose * cv::Vec3d(0, 0, -weld_offset_m);
    cv::Vec3d depart_pos = seam_end_pose * cv::Vec3d(0, 0, -approach_distance_m);

    nlohmann::json command_json;
    command_json["approach_pose"] = poseToJson(approach_pos, seam_start_quat);
    command_json["weld_start_pose"] = poseToJson(weld_start_pos, seam_start_quat);
    command_json["weld_end_pose"] = poseToJson(weld_end_pos, seam_end_quat);
    command_json["depart_pose"] = poseToJson(depart_pos, seam_end_quat);
    command_json["movement_speed_m_s"] = movement_speed_m_s;
    command_json["welding_speed_m_s"] = welding_speed_m_s;
    command_json["acceleration_m_s2"] = acceleration_m_s2;

    out_command_json = std::move(command_json);

    out_visualization = uw::IUsecaseModule::UsecaseVisualization {
        .supports_visualization = true,
        .poses = {
            {
                .position = {
                    .x = weld_start_pos[0],
                    .y = weld_start_pos[1],
                    .z = weld_start_pos[2]
                },
                .orientation = {
                    .qw = seam_start_quat[0],
                    .qx = seam_start_quat[1],
                    .qy = seam_start_quat[2],
                    .qz = seam_start_quat[3]
                }
            }
        },
        .trajectories = {
            {
                {
                    .x = approach_pos[0],
                    .y = approach_pos[1],
                    .z = approach_pos[2]
                },
                {
                    .x = weld_start_pos[0],
                    .y = weld_start_pos[1],
                    .z = weld_start_pos[2]
                },
                {
                    .x = weld_end_pos[0],
                    .y = weld_end_pos[1],
                    .z = weld_end_pos[2]
                },
                {
                    .x = depart_pos[0],
                    .y = depart_pos[1],
                    .z = depart_pos[2]
                }
            }
        }
    };

    return std::expected<void, uw::helper::ErrorInfo>{};
}



std::expected<void, uw::helper::ErrorInfo> UsecaseWeld::validateParameters(const nlohmann::json& command_json)
{
    if (!command_json.contains("approach_pose") || !isValidPose(command_json["approach_pose"]))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseWeld: command JSON missing 'approach_pose' object or invalid pose."));
    }
    if (!command_json.contains("weld_start_pose") || !isValidPose(command_json["weld_start_pose"]))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecaseWeld: command JSON missing 'weld_start_pose' object or invalid pose."));
    }
    if (!command_json.contains("weld_end_pose") || !isValidPose(command_json["weld_end_pose"]))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "UsecaseWeld: command JSON missing 'weld_end_pose' object or invalid pose."));
    }
    if (!command_json.contains("depart_pose") || !isValidPose(command_json["depart_pose"]))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecaseWeld: command JSON missing 'depart_pose' object or invalid pose."));
    }
    if (!command_json.contains("movement_speed_m_s") || !command_json["movement_speed_m_s"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseWeld: command JSON missing 'movement_speed_m_s' number."));
    }
    if (!command_json.contains("welding_speed_m_s") || !command_json["welding_speed_m_s"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "UsecaseWeld: command JSON missing 'welding_speed_m_s' number."));
    }
    if (!command_json.contains("acceleration_m_s2") || !command_json["acceleration_m_s2"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "UsecaseWeld: command JSON missing 'acceleration_m_s2' number."));
    }
    
    const double movement_speed_m_s = command_json["movement_speed_m_s"].get<double>();
    const double welding_speed_m_s = command_json["welding_speed_m_s"].get<double>();
    const double acceleration_m_s2 = command_json["acceleration_m_s2"].get<double>();
    if (movement_speed_m_s <= 0.0 || welding_speed_m_s <= 0.0 || acceleration_m_s2 <= 0.0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "UsecaseWeld: movement_speed_m_s, welding_speed_m_s and acceleration_m_s2 must be greater than 0."));
    }

    // All checks passed
    return std::expected<void, uw::helper::ErrorInfo>{};
}



std::expected<void, uw::helper::ErrorInfo> UsecaseWeld::runProgram(const nlohmann::json& command_json, bool simulated)
{
    rc::Pose approach_pose = jsonToPose(command_json["approach_pose"]);
    rc::Pose weld_start_pose = jsonToPose(command_json["weld_start_pose"]);
    rc::Pose weld_end_pose = jsonToPose(command_json["weld_end_pose"]);
    rc::Pose depart_pose = jsonToPose(command_json["depart_pose"]);

    double movement_speed_m_s = command_json["movement_speed_m_s"].get<double>();
    double welding_speed_m_s = command_json["welding_speed_m_s"].get<double>();
    double acceleration_m_s2 = command_json["acceleration_m_s2"].get<double>();


    auto move_res = moveLinear(approach_pose, movement_speed_m_s, acceleration_m_s2, true);
    if (move_res.error) return std::unexpected(*move_res.error);
    if (move_res.control_request == UsecaseWeld::AsyncResult::ControlRequest::STOP) handleControlRequests(false, true); // if stopped during approach, handle stop request (ends the runProgram early via StopException)

    move_res = moveLinear(weld_start_pose, movement_speed_m_s, acceleration_m_s2, true);
    if (move_res.error) return std::unexpected(*move_res.error);
    if (move_res.control_request == UsecaseWeld::AsyncResult::ControlRequest::STOP) handleControlRequests(false, true); // if stopped during approach, handle stop request
    log(logging::LogType::INFO, "UsecaseWeld: Starting weld...");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    log(logging::LogType::INFO, "UsecaseWeld: Welding...");

    // we allow pause/resume even while welding, but we must stop/start the weld action (simulated here by sleeping)
    move_res = moveLinear(weld_end_pose, welding_speed_m_s, acceleration_m_s2, true,
        [this, movement_speed_m_s, acceleration_m_s2](const rc::Pose& pose) { 
            log(logging::LogType::INFO, "UsecaseWeld: Stopping weld due to pause..."); 
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            log(logging::LogType::INFO, "UsecaseWeld: Weld stopped, moving to depart pose...");

            cv::Vec4d pose_q = {pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z};
            cv::Vec3d pose_p = {pose.position.x, pose.position.y, pose.position.z};

            pu::SE3 pose_se3 = pu::SE3::fromQuatTvec(pose_q, pose_p, false);
            cv::Vec3d depart_offset = pose_se3 * cv::Vec3d(0, 0, -0.1); // move 10cm down from current pose for depart pose during pause
            robot_wrapper_.moveLinear(rc::Pose{
                .position = {
                    .x = depart_offset[0],
                    .y = depart_offset[1],
                    .z = depart_offset[2]
                },
                .orientation = pose.orientation
            }, movement_speed_m_s, acceleration_m_s2, true);

            log(logging::LogType::INFO, "UsecaseWeld: Moved to depart pose, waiting for resume...");
        },
        [this, movement_speed_m_s, acceleration_m_s2](const rc::Pose& pose) { 
            log(logging::LogType::INFO, "UsecaseWeld: Resuming weld, moving back to weld pose...");

            log(logging::LogType::INFO, "UsecaseWeld: Target pose after resume: [" + std::to_string(pose.position.x) + ", " + std::to_string(pose.position.y) + ", " + std::to_string(pose.position.z) + "]");

            robot_wrapper_.moveLinear(pose, movement_speed_m_s, acceleration_m_s2, true);

            log(logging::LogType::INFO, "UsecaseWeld: Resuming weld..."); 
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            log(logging::LogType::INFO, "UsecaseWeld: Weld resumed.");
        }
    );
    if (move_res.error || move_res.control_request == UsecaseWeld::AsyncResult::ControlRequest::STOP) 
    {
        log(logging::LogType::INFO, "UsecaseWeld: Stopping weld due to error/stop request...");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        log(logging::LogType::INFO, "UsecaseWeld: Stopped...");

        if (move_res.error) return std::unexpected(*move_res.error);
        else handleControlRequests(false, true); // if stopped during weld, stop welding first, then handle stop request
    }

    log(logging::LogType::INFO, "UsecaseWeld: Welding completed, stopping weld...");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    log(logging::LogType::INFO, "UsecaseWeld: Moving to depart pose...");

    move_res = moveLinear(depart_pose, movement_speed_m_s, acceleration_m_s2, true);
    if (move_res.error) return std::unexpected(*move_res.error);
    if (move_res.control_request == UsecaseWeld::AsyncResult::ControlRequest::STOP) handleControlRequests(false, true); // if stopped during depart, handle stop request

    log(logging::LogType::INFO, "UsecaseWeld: Weld completed.");

    return std::expected<void, uw::helper::ErrorInfo>{};
}


UsecaseWeld::AsyncResult UsecaseWeld::moveLinear(const rc::Pose& pose, double speed, double acceleration, bool allow_pause, std::function<void(const rc::Pose&)> before_pause_callback, std::function<void(const rc::Pose&)> after_resume_callback)
{
    while (true)
    {
        rc::MoveRequestResult res = robot_wrapper_.moveLinear(pose, speed, acceleration, false);
        if (!res.success_)
        {
            return AsyncResult{ 
                .control_request = AsyncResult::ControlRequest::NONE, 
                .error = uw::helper::ErrorInfo::WithDetails(
                    1, 
                    "UsecaseWeld: Failed to send move linear command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)
                ) 
            };
        }
        auto async_result = asyncWaitForFinish(res.action_id_, allow_pause);

        if (async_result.error)
        {
            return async_result; // propagate error from asyncWaitForFinish
        }
        else if (async_result.control_request == AsyncResult::ControlRequest::STOP || async_result.control_request == AsyncResult::ControlRequest::NONE)
        {
            return async_result; // either finished successfully with no control request, or was stopped, in both cases just return
        }
        else if (async_result.control_request == AsyncResult::ControlRequest::PAUSE)
        {
            rc::StatusMessage current_status;
            if (!robot_wrapper_.getLastStatus(current_status))
            {
                return AsyncResult{ 
                    .control_request = AsyncResult::ControlRequest::PAUSE, 
                    .error = uw::helper::ErrorInfo::WithDetails(2, "UsecaseWeld: Failed to get robot status after pause request.") 
                };
            }

            log(logging::LogType::INFO, "UsecaseWeld: Move linear action paused at pose: [" + std::to_string(current_status.end_effector_pose.position.x) + ", " + std::to_string(current_status.end_effector_pose.position.y) + ", " + std::to_string(current_status.end_effector_pose.position.z) + "], target pose: [" + std::to_string(pose.position.x) + ", " + std::to_string(pose.position.y) + ", " + std::to_string(pose.position.z) + "].");

            if (before_pause_callback) before_pause_callback(current_status.end_effector_pose);
            log(logging::LogType::INFO, "UsecaseWeld: Action paused. Waiting for resume...");
            handleControlRequests(true, true); // handle pause (waits until resume) and stop (throw StopException if stop is requested during pause)
            log(logging::LogType::INFO, "UsecaseWeld: Resuming action...");
            if (after_resume_callback) after_resume_callback(current_status.end_effector_pose);
        }
        else
        {
            // Unexpected control request, return error
            return AsyncResult{ 
                .control_request = AsyncResult::ControlRequest::NONE, 
                .error = uw::helper::ErrorInfo::WithDetails(3, "UsecaseWeld: Received unexpected control request from asyncWaitForFinish.") 
            };
        }
    }
}


UsecaseWeld::AsyncResult UsecaseWeld::asyncWaitForFinish(uint64_t action_id, bool allow_pause)
{
    AsyncResult::ControlRequest request = AsyncResult::ControlRequest::NONE;
    while (robot_wrapper_.isActionActive(action_id))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (request != AsyncResult::ControlRequest::NONE)
        {
            continue; // already requested cancel, just wait for action to end
        }

        auto [pause_requested, stop_requested] = checkControlRequests();
        if (stop_requested || (pause_requested && allow_pause))
        {
            request = stop_requested ? AsyncResult::ControlRequest::STOP : AsyncResult::ControlRequest::PAUSE;

            log(logging::LogType::INFO, "UsecaseWeld: " + std::string(request == AsyncResult::ControlRequest::STOP ? "Stop" : "Pause") + " requested, cancelling robot action " + std::to_string(action_id) + ".");

            rc::MoveRequestResult cancel_res = robot_wrapper_.cancelAction(action_id);
            if (!cancel_res.success_)
            {
                return AsyncResult{ 
                    .control_request = request, 
                    .error = uw::helper::ErrorInfo::WithDetails(4, "UsecaseWeld: Failed to send cancel command to robot for action " + std::to_string(action_id) + ": " + (cancel_res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : cancel_res.err_message_)) 
                };
            }
        }
    }

    return AsyncResult{ 
        .control_request = request, 
        .error = std::nullopt 
    };
}


std::expected<void, uw::helper::ErrorInfo> UsecaseWeld::deserializeSceneResponse(
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
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecaseWeld: Failed to deserialize scene response message."));
    }

    // Read Response struct from message data
    if (message_data.data_.size() < sizeof(sdh::Response))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseWeld: Scene response data too small."));
    }
    
    sdh::Response scene_response;
    std::memcpy(&scene_response, message_data.data_.data(), sizeof(sdh::Response));
    
    // Validate scene response version
    if (scene_response.version != sdh::SCENE_DETECTION_MESSAGE_VERSION)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "UsecaseWeld: Scene response has invalid version: " + std::to_string(scene_response.version) + ", expected " + std::to_string(sdh::SCENE_DETECTION_MESSAGE_VERSION) + "."));
    }
    
    // Validate scene response type (must be READ_SCENE)
    if (scene_response.req_type != sdh::ReqType::READ_SCENE)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "UsecaseWeld: Scene response has unexpected request type: " + std::to_string(static_cast<int>(scene_response.req_type)) + ", expected READ_SCENE."));
    }
    
    // Parse scene response blob
    out_detected_boxes.clear();
    if (scene_response.count > 0)
    {
        if (message_data.blobs_.size() != 1)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "UsecaseWeld: Scene response indicates " + std::to_string(scene_response.count) + " boxes but blob data is missing or invalid."));
        }
        const void* blob_data = message_data.blobs_[0].data();
        size_t blob_size = message_data.blobs_[0].size();
        if (!scene_response.parseScene(blob_data, blob_size, out_detected_boxes))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(9, "UsecaseWeld: Failed to parse scene response data."));
        }
    }
    // If count is 0, out_detected_boxes will be empty, which is valid
    
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseWeld::deserializeTrajectory(
    const std::vector<uint8_t>& pose_data,
    std::vector<pm::Pose>& out_pose
)
{
    // Match the serialization format from processCustomMessageOrResponse:
    // 7 doubles: x, y, z, qx, qy, qz, qw
    using aergo::module::helpers::serialization_helper::deserialization::BufferReader;
    BufferReader reader(pose_data.data(), pose_data.size());
    uint64_t pose_count = 0;
    if (!reader.read<uint64_t>(pose_count))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(10, "UsecaseWeld: Failed to read pose count from parameter value."));
    }
    out_pose.clear();
    out_pose.reserve(pose_count);
    for (uint64_t i = 0; i < pose_count; ++i)
    {
        pm::Pose p{};
        if (!reader.read<double>(p.x) ||
            !reader.read<double>(p.y) ||
            !reader.read<double>(p.z) ||
            !reader.read<double>(p.qx) ||
            !reader.read<double>(p.qy) ||
            !reader.read<double>(p.qz) ||
            !reader.read<double>(p.qw))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(11, "UsecaseWeld: Failed to read pose " + std::to_string(i) + " from parameter value."));
        }
        out_pose.push_back(p);
    }
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseWeld::deserializeAdvancedParameters(
    const std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
    double& out_movement_speed_mm_s,
    double& out_welding_speed_mm_s,
    double& out_acceleration_mm_s2,
    double& out_weld_offset_mm,
    double& out_approach_distance_mm
)
{
    // Validate advanced parameters count (Movement speed, Welding speed, Acceleration, Weld offset, Approach distance)
    if (advanced_parameter_values.size() != 5)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(11, "UsecaseWeld: Expected 5 advanced parameter values, got " + std::to_string(advanced_parameter_values.size())));
    }

    // Validate each advanced parameter has exactly one value
    for (size_t i = 0; i < 5; ++i)
    {
        if (advanced_parameter_values[i].size() != 1)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(12, "UsecaseWeld: Advanced parameter " + std::to_string(i) + " expected 1 value, got " + std::to_string(advanced_parameter_values[i].size())));
        }
        if (!std::holds_alternative<double>(advanced_parameter_values[i][0].value_))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(13, "UsecaseWeld: Advanced parameter " + std::to_string(i) + " expected double, got " + std::to_string(advanced_parameter_values[i][0].value_.index())));
        }
    }

    // Parse advanced parameters (with units)
    out_movement_speed_mm_s = std::get<double>(advanced_parameter_values[0][0].value_);
    out_welding_speed_mm_s = std::get<double>(advanced_parameter_values[1][0].value_);
    out_acceleration_mm_s2 = std::get<double>(advanced_parameter_values[2][0].value_);
    out_weld_offset_mm = std::get<double>(advanced_parameter_values[3][0].value_);
    out_approach_distance_mm = std::get<double>(advanced_parameter_values[4][0].value_);

    // Validate advanced parameter values (include units in messages)
    if (out_movement_speed_mm_s <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(14, "UsecaseWeld: Movement speed [mm/s] must be positive, got " + std::to_string(out_movement_speed_mm_s) + " mm/s."));
    }
    if (out_welding_speed_mm_s <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(15, "UsecaseWeld: Welding speed [mm/s] must be positive, got " + std::to_string(out_welding_speed_mm_s) + " mm/s."));
    }
    if (out_acceleration_mm_s2 <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(16, "UsecaseWeld: Acceleration [mm/s²] must be positive, got " + std::to_string(out_acceleration_mm_s2) + " mm/s²."));
    }
    if (out_weld_offset_mm < 0 || out_weld_offset_mm > 30)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(17, "UsecaseWeld: Weld offset [mm] must be in range 0 to 30, got " + std::to_string(out_weld_offset_mm) + " mm."));
    }
    if (out_approach_distance_mm < 20 || out_approach_distance_mm > 200)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(18, "UsecaseWeld: Approach distance [mm] must be in range 20 to 200, got " + std::to_string(out_approach_distance_mm) + " mm."));
    }

    return std::expected<void, uw::helper::ErrorInfo>{};
}