#pragma once

#include "module_helpers/base_usecase/base_usecase.h"
#include "module_helpers/usecase_wrapper/helper_types.h"
#include "module_helpers/mixed_buffer_allocator/mixed_buffered_allocator.h"
#include "module_common/module_interface_.h"
#include "module_helpers/synchronous_request_helper/synchronous_request_helper.h"

#include <expected>
#include <optional>

namespace aergo::default_modules::demo_robot_control
{
    namespace uw = aergo::module::helpers::usecase_wrapper;
    namespace sync_req = aergo::module::helpers::synchronous_request_helper;

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
        );

        ~DemoRobotControl() noexcept override = default;

        virtual bool valid() noexcept override { return valid_; }

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
            nlohmann::json& out_command_json
        ) override;

        virtual std::expected<void, uw::helper::ErrorInfo> validateParameters(
            const nlohmann::json& command_json
        ) override;

        virtual std::expected<void, uw::helper::ErrorInfo> runProgram(
            const nlohmann::json& command_json, 
            bool simulated
        ) override;
        
    private:
        enum class RequestType { ROBOT_REQUEST };

        std::unique_ptr<aergo::module::helpers::mixed_buffer_allocator::MixedBufferedAllocator> mixed_allocator_;
        std::unique_ptr<sync_req::SynchronousRequestHelper<RequestType>> sync_request_helper_;
        bool valid_ {false};

        uint32_t robot_request_channel_;
        uint32_t robot_status_channel_;
        uint32_t robot_finished_channel_;

        std::optional<uint64_t> running_action_id_;
    };
}