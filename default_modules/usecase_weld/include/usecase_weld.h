#pragma once

#include "module_helpers/base_usecase/base_usecase.h"
#include "module_helpers/usecase_wrapper/helper_types.h"
#include "module_common/module_interface_.h"
#include "module_helpers/robot_wrapper/robot_wrapper.h"
#include "module_helpers/scene_detection_helper/message_types.h"
#include "module_helpers/pose_utils/pose_utils.h"
#include "module_helpers/pen_messages/message_types.h"
#include "module_helpers/scene_detection_helper/registry_request_handler.h"

#include <expected>
#include <vector>

namespace aergo::default_modules::usecase_weld
{
    namespace uw = aergo::module::helpers::usecase_wrapper;
    namespace rc = aergo::module::helpers::robot_interface::robot_control;
    namespace p_desc = aergo::module::helpers::parameter_description;
    namespace pm = aergo::module::helpers::pen_messages;
    namespace sdh = aergo::module::helpers::scene_detection_helper;

    class UsecaseWeld : public aergo::module::helpers::base_usecase::BaseUsecase
    {
    public:
        UsecaseWeld(
            const char* data_path, 
            aergo::module::ICore* core, 
            aergo::module::InputChannelMapInfo channel_map_info, 
            const aergo::module::logging::ILogger* logger, 
            uint64_t module_id, 
            const aergo::module::ModuleInfo* module_info,
            bool supports_multi_program,
            bool supports_pause,
            bool supports_stop
        );

        ~UsecaseWeld() noexcept = default;

        virtual bool valid() noexcept override { return robot_wrapper_.valid(); }

        virtual bool sendRequestFromUsecase(const std::vector<p_desc::ParameterDescription>& auto_parameters, const uint32_t param_id, uint64_t& out_request_id) override;

        virtual aergo::module::IModule::IngressDecision onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src, const aergo::module::message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept override;
        virtual void processResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;
        virtual void processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;

        virtual uw::IUsecaseModule::ProcessingResult processCustomMessageOrResponse(
            uw::IUsecaseModule::ProcessingChannelType channel_type, 
            uint32_t consumer_id, 
            aergo::module::ChannelIdentifier source_channel, 
            aergo::module::message::MessageHeader message, 
            std::vector<uint8_t>& out_data_replace
        ) override;

        virtual std::expected<void, uw::helper::ErrorInfo> createCommandFromParameters(
            const uw::p_desc::ParameterList& auto_parameters,
            const uw::p_desc::ParameterList& required_parameters,
            const uw::p_desc::ParameterList& advanced_parameters,
            std::vector<std::vector<uw::helper::ParameterTypeValue>>& auto_parameter_values,
            std::vector<std::vector<uw::helper::ParameterTypeValue>>& required_parameter_values,
            std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
            nlohmann::json& out_command_json,
            uw::IUsecaseModule::UsecaseVisualization& out_visualization
        ) override;

    protected:
        virtual std::expected<void, uw::helper::ErrorInfo> validateParameters(
            const nlohmann::json& command_json
        ) override;

        virtual std::expected<void, uw::helper::ErrorInfo> runProgram(
            const nlohmann::json& command_json, 
            bool simulated
        ) override;

    private:
        std::expected<void, uw::helper::ErrorInfo> asyncWaitForFinish(uint64_t action_id);
        std::expected<void, uw::helper::ErrorInfo> moveLinear(const rc::Pose& pose, double speed, double acceleration);
        bool sendSceneDetectionRequest(uint64_t& out_request_id);

        
        // Helper functions for parameter deserialization
        static std::expected<void, uw::helper::ErrorInfo> deserializeSceneResponse(
            const std::vector<uint8_t>& scene_data,
            std::vector<aergo::module::helpers::scene_detection_helper::DetectedBox>& out_detected_boxes
        );
        
        static std::expected<void, uw::helper::ErrorInfo> deserializeTrajectory(
            const std::vector<uint8_t>& pose_data,
            std::vector<pm::Pose>& out_pose
        );
        
        static std::expected<void, uw::helper::ErrorInfo> deserializeAdvancedParameters(
            const std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
            double& out_movement_speed_mm_s,
            double& out_welding_speed_mm_s,
            double& out_acceleration_mm_s2,
            double& out_weld_offset_mm,
            double& out_approach_distance_mm
        );
        
        // Helper function for weld seam detection
        struct WeldDetectionResult
        {
            uint64_t box_id;
            aergo::module::helpers::pose_utils::SE3 T_box_weld; // SE3 transformation from box frame to weld frame
            cv::Vec3d box_touch_point; // Touch point on the box
        };
        
        std::expected<WeldDetectionResult, uw::helper::ErrorInfo> detectWeldSeam(
            const aergo::module::helpers::scene_detection_helper::Pose& weld_pose,
            double weld_detection_distance,
            const std::vector<aergo::module::helpers::scene_detection_helper::DetectedBox>& detected_boxes
        );

        enum class RequestType { SCENE_DETECTION_REQUEST };

        rc::RobotWrapper robot_wrapper_;
        bool valid_{ false };
        uint32_t pen_message_intent_subscribe_channel_id_{ 0 };
        uint32_t scene_detection_request_channel_id_{ 0 };
        
        std::unique_ptr<sdh::RegistryRequestHandler> registry_request_handler_;
    };
}