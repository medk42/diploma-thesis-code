#pragma once

#include "module_helpers/base_usecase/base_usecase.h"
#include "module_helpers/usecase_wrapper/helper_types.h"
#include "module_common/module_interface_.h"
#include "module_helpers/robot_wrapper/robot_wrapper.h"

#include <expected>

namespace aergo::default_modules::usecase_move_trajectory
{
    namespace uw = aergo::module::helpers::usecase_wrapper;
    namespace rc = aergo::module::helpers::robot_interface::robot_control;

    class UsecaseMoveTrajectory : public aergo::module::helpers::base_usecase::BaseUsecase
    {
    public:
        UsecaseMoveTrajectory(
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

        ~UsecaseMoveTrajectory() noexcept override = default;

        virtual bool valid() noexcept override { return robot_wrapper_.valid(); }

        virtual aergo::module::IModule::IngressDecision onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src, const aergo::module::message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept override;
        virtual void processResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;
        virtual void processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;

        virtual ProcessingResult processCustomMessageOrResponse(
            ProcessingChannelType channel_type, 
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
        struct AsyncResult
        {
            // true if the command was stopped by a stop request, false if it finished normally
            bool stopped;

            // ErrorInfo if an error occurred during execution or while sending the command, std::nullopt otherwise
            std::optional<uw::helper::ErrorInfo> error;
        };

        AsyncResult asyncWaitForFinish(uint64_t action_id);
        std::expected<void, uw::helper::ErrorInfo> validatePose(const nlohmann::json& pose_json, int error_code_base);
        rc::Pose extractPoseFromJson(const nlohmann::json& pose_json);

        rc::RobotWrapper robot_wrapper_;
        bool valid_{ false };
        uint32_t pen_message_intent_subscribe_channel_id_{ 0 };
    };
}