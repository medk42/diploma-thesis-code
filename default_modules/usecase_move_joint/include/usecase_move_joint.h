#pragma once

#include "module_helpers/base_usecase/base_usecase.h"
#include "module_helpers/usecase_wrapper/helper_types.h"
#include "module_common/module_interface_.h"
#include "module_helpers/robot_wrapper/robot_wrapper.h"

#include <expected>

namespace aergo::default_modules::usecase_move_joint
{
    namespace uw = aergo::module::helpers::usecase_wrapper;
    namespace rc = aergo::module::helpers::robot_interface::robot_control;

    class UsecaseMoveJoint : public aergo::module::helpers::base_usecase::BaseUsecase
    {
    public:
        UsecaseMoveJoint(
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

        ~UsecaseMoveJoint() noexcept override = default;

        virtual bool valid() noexcept override { return valid_; }

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
        /// @brief If stop_on_stop_request is true, the function will monitor for stop requests, if a stop request is received the function will cancel the robot action first and then abort the program after the robot reports completion (by calling handleControlRequests with allow_stop = true). If false, the function will not monitor for stop requests and will return as soon as the robot reports action completion.
        std::expected<void, uw::helper::ErrorInfo> asyncWaitForFinish(uint64_t action_id, bool stop_on_stop_request);

        rc::RobotWrapper robot_wrapper_;
        bool valid_{ false };
        uint32_t robot_interface_status_channel_id_{ 0 };
    };
}