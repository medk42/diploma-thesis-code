#pragma once

#include "module_common/module_interface_.h"
#include "module_common/base_module.h"
#include "activable_module.h"
#include "async_task.h"

#include <tuple>
#include <vector>

namespace aergo::module::helpers::activation_wrapper
{
    class ActivationWrapper : public virtual aergo::module::IModule
    {
    public:
        ActivationWrapper(aergo::module::IModule* module, aergo::module::ModuleInfo module_info, params::ParameterList* parameters_);
        
        virtual void processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;
        virtual void processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;
        virtual void processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;
        virtual void cycleImpl() noexcept override;
        virtual bool valid() noexcept override;
        virtual void* query_capability(const std::type_info& id) noexcept override;

    private:
        /// @brief Initialize parameters to specified default values (or system defaults if not specified).
        /// Every parameter aside from CUSTOM and lists is initialized.
        /// @return true on success, false on failure
        bool initializeDefaultParameters();
        std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> processActivationRequest(message_types::Request& request, aergo::module::message::SharedDataBlob* blob);
        std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> setValue(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response);
        std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> readValues(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response);
        std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> listRemove(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response);
        void handleActivationTask();

        bool valid_;                              // is the wrapper valid (correctly initialized)

        aergo::module::IModule* module_ref_;      // reference to module to send IModule calls to
        BaseModule* base_module_ref_;             // reference to base module to allow sending messages and logging
        IActivableModule* activable_module_ref_;  // reference to activable module to allow activation/deactivation logic
        params::ParameterList* parameters_;       // reference to parameter list

        aergo::module::BaseModule::AllocatorPtr dynamic_allocator_; // dynamic allocator for responses (READ_VALUE, READ_ACTIVATION_PARAMETERS)

        uint32_t expected_response_producer_id_;  // ID of the response producer channel for activation wrapper messages

        bool activated_;                          // is the module currently activated
        std::unique_ptr<AsyncTask> activation_task_; // currently running activation/deactivation task (if any)

        std::vector<std::vector<std::vector<uint8_t>>> parameter_values_; // current parameter values (for lists, each list entry is a vector of bytes; for non-lists, only first entry is used)

        struct {
            bool expected_;  // true if we are waiting for a CUSTOM message/response, false otherwise
            size_t param_id_; // ID of the parameter we are changing
            size_t list_id_; // ID into the list inside the parameter (0 for non-lists)
        } message_wait_; // waiting for CUSTOM message/response.
    };
}