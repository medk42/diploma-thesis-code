#pragma once

#include "module_helpers/base_usecase/base_usecase.h"
#include "module_helpers/usecase_wrapper/helper_types.h"
#include "module_common/module_interface_.h"
#include "module_helpers/robot_wrapper/robot_wrapper.h"

#include <expected>
#include <optional>
#include <tuple>
#include <string>

namespace aergo::default_modules::demo_robot_control
{
    namespace uw = aergo::module::helpers::usecase_wrapper;
    namespace rc = aergo::module::helpers::robot_interface::robot_control;

    class DemoRobotControl : public aergo::module::helpers::base_usecase::BaseUsecase
    {
    public:
        DemoRobotControl(
            const char* data_path, 
            aergo::module::ICore* core, 
            aergo::module::InputChannelMapInfo channel_map_info, 
            const aergo::module::logging::ILogger* logger, 
            uint64_t module_id, 
            const aergo::module::ModuleInfo* module_info,
            bool supports_multi_program,
            bool supports_pause,
            bool supports_stop
        ) : BaseUsecase(
                data_path, core, channel_map_info, 
                logger, module_id, module_info,
                supports_multi_program, supports_pause, supports_stop
            ),
            robot_wrapper_(*this)
        {}

        ~DemoRobotControl() noexcept override = default;

        virtual bool valid() noexcept override { return robot_wrapper_.valid(); }

        virtual aergo::module::IModule::IngressDecision onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src, const aergo::module::message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept override;
        virtual void processResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;
        virtual void processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;

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

        virtual std::expected<void, uw::helper::ErrorInfo> validateParameters(
            const nlohmann::json& command_json
        ) override;

        virtual std::expected<void, uw::helper::ErrorInfo> runProgram(
            const nlohmann::json& command_json, 
            bool simulated
        ) override;
        
    private:
        std::expected<void, uw::helper::ErrorInfo> asyncWaitForFinish(uint64_t action_id);

        rc::RobotWrapper robot_wrapper_;
    };
}