#pragma once

#include "module_common/module_interface_.h"
#include "module_common/base_module.h"
#include "serialization_helper.h"
#include "usecase_module_interface.h"

#include <memory>
#include <mutex>
#include <atomic>

namespace aergo::module::helpers::usecase_wrapper
{
    class UsecaseWrapper : public aergo::module::IModule
    {
    public:
        /// @param module module to wrap
        /// @param param_name name of the usecase
        /// @param param_desc description of the usecase
        /// @param auto_parameters parameters that are set from the input Consumer channels (subscribe/request) - only CUSTOM type allowed (value or list of values)
        /// @param required_parameters parameters that are required to be set by the user before activation - any non-CUSTOM type allowed (value or list of values)
        /// @param advanced_parameters parameters that are optional to set by the user before activation - any non-CUSTOM type allowed, need to have default value (value or list of values)
        UsecaseWrapper(
            std::unique_ptr<aergo::module::IModule> module, 
            std::string param_name,
            std::string param_desc,
            p_desc::ParameterList auto_parameters, 
            p_desc::ParameterList manual_parameters, 
            p_desc::ParameterList advanced_parameters
        );
        
        virtual void processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;
        virtual aergo::module::ResponseData processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;
        virtual void processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;
        virtual bool valid() noexcept override;
        virtual void* query_capability(const std::type_info& id) noexcept override;
        virtual aergo::module::IModule::IngressDecision onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept override;
        virtual bool threadStart(uint32_t timeout_ms) noexcept override;
        virtual bool threadStop(uint32_t timeout_ms) noexcept override;
        virtual ISerializableModule::SaveData save() noexcept override;
        virtual bool load(ISerializableModule::SaveData data) noexcept override;
        virtual const ModuleInfo* getModuleInfo() const noexcept override;

    private:

        /// @param params parameter list to validate
        /// @param only_custom_allowed true if only custom allowed, false if only non-custom allowed
        /// @param default_values_required true if default values are required, false otherwise (default values may be provided or not)
        /// @return true if parameter list is valid, false otherwise
        bool validateParameterList(const aergo::module::ModuleInfo* module_info, const p_desc::ParameterList& params, bool only_custom_allowed, bool default_values_required) const;

        aergo::module::ResponseData processUsecaseRequest(message::MessageHeader message) noexcept;
        aergo::module::ResponseData handleReadCommandParameters() noexcept;
        aergo::module::ResponseData handleReadCustomParameterStart(uint32_t param_id) noexcept;
        aergo::module::ResponseData handleReadCustomParameterCheck(uint64_t task_id, bool cancel_flag) noexcept;
        aergo::module::ResponseData handleCreateCommand(message::SharedDataBlob& blob) noexcept;
        aergo::module::ResponseData handleProgramReadVisualization(message::SharedDataBlob& blob) noexcept;
        aergo::module::ResponseData handleProgramStart(message::SharedDataBlob& blob, bool simulated) noexcept;
        aergo::module::ResponseData handleProgramCommand(message_types::ReqType command_type, uint64_t task_id) noexcept;

    
        struct CustomMessageAwaitInfo
        {
            uint64_t read_task_id_; 
            uint32_t channel_id_; 
            uint64_t request_id_; 
            bool seen_;
        };

        struct CustomMessageDoneInfo
        {
            uint64_t read_task_id_; 
            bool success_;
            std::vector<uint8_t> result_; 
        };

        std::unique_ptr<aergo::module::IModule> module_ref_;        // reference to module to send IModule calls to; only changed during initialization, no need to synchronize
        aergo::module::BaseModule* base_module_ref_;                // reference to base module to allow sending messages and logging
        IUsecaseModule* usecase_module_ref_;                        // reference to usecase module to allow sending requests from usecase wrapper
        std::string param_name_;                                    // name of the usecase
        std::string param_desc_;                                    // description of the usecase

        bool valid_;                                                // is the wrapper valid (correctly initialized); only changed during initialization, no need to synchronize
        uint32_t expected_response_producer_id_;                    // ID of the response producer channel for usecase responses
        aergo::module::BaseModule::AllocatorPtr dynamic_allocator_; // dynamic allocator for responses

        p_desc::ParameterList auto_parameters_;                     // parameters that are set from the input Consumer channels (subscribe/request) - only CUSTOM type allowed (value or list of values)
        p_desc::ParameterList manual_parameters_;                   // parameters that are required to be set by the user before activation - any non-CUSTOM type allowed (value or list of values)
        p_desc::ParameterList advanced_parameters_;                 // parameters that are optional to set by the user before activation - any non-CUSTOM type allowed, need to have default value (value or list of values)

        std::mutex custom_read_mutex_;                              // mutex to protect access to response_await_list_, message_await_list_ and next_custom_read_task_id_
        uint64_t next_custom_read_task_id_{0};                      // next request ID for usecase requests
        std::vector<CustomMessageAwaitInfo> response_await_list_;   // list of pending CUSTOM parameter read responses
        std::vector<CustomMessageAwaitInfo> message_await_list_;    // list of pending CUSTOM parameter read requests
        std::vector<CustomMessageDoneInfo> response_done_list_;     // list of completed CUSTOM parameter read responses
        std::vector<CustomMessageDoneInfo> message_done_list_;      // list of completed CUSTOM parameter read requests

    };
}