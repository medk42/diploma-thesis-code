#pragma once

#include "module_common/module_interface_.h"
#include "module_common/base_module.h"
#include "activable_module.h"
#include "module_helpers/async_helpers/async_task.h"

#include <tuple>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

namespace aergo::module::helpers::activation_wrapper
{
    /// @brief Wrapper for modules that can be activated/deactivated with parameters.
    /// Holds back all messages/requests/responses until the module is activated.
    class ActivationWrapper : public aergo::module::IModule
    {
        constexpr static uint32_t SCHEMA_VERSION = 1; // schema version for saved data

    public:
        ActivationWrapper(std::unique_ptr<aergo::module::IModule> module, aergo::module::helpers::parameter_description::ParameterList* parameters_);
        
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
        /// @brief Initialize parameters to specified default values (or system defaults if not specified).
        /// Every parameter aside from CUSTOM and lists is initialized.
        /// @return true on success, false on failure
        bool initializeDefaultParameters();
        std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> processActivationRequest(message_types::Request& request, aergo::module::message::SharedDataBlob* blob);
        std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> setValue(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response);
        std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> readValues(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response);
        std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> listAdd(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response);
        std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> listRemove(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response);
        void handleActivationTask();
        void setCustomValueOnReceive(message::MessageHeader message);

        /// @brief Check if current parameter values are valid (types, ranges, enum values, list sizes).
        /// @return true if valid, false otherwise
        bool areParametersValid();

        bool valid_;                              // is the wrapper valid (correctly initialized); only changed during initialization, no need to synchronize

        std::unique_ptr<aergo::module::IModule> module_ref_;      // reference to module to send IModule calls to; only changed during initialization, no need to synchronize
        BaseModule* base_module_ref_;             // reference to base module to allow sending messages and logging
        IActivableModule* activable_module_ref_;  // reference to activable module to allow activation/deactivation logic
        aergo::module::helpers::parameter_description::ParameterList* parameters_;       // reference to parameter list; only changed during initialization, no need to synchronize

        aergo::module::BaseModule::AllocatorPtr dynamic_allocator_; // dynamic allocator for responses (READ_VALUE, READ_ACTIVATION_PARAMETERS)

        uint32_t expected_response_producer_id_;  // ID of the response producer channel for activation wrapper messages

        std::atomic<bool> activated_;                      // is the module currently activated; be careful when assigning, activated_ is accessed without locking mutex_
        std::unique_ptr<async_helpers::AsyncTask<bool>> activation_task_; // currently running activation/deactivation task (if any)

        std::vector<std::vector<std::vector<uint8_t>>> parameter_values_; // current parameter values (for lists, each list entry is a vector of bytes; for non-lists, only first entry is used)

        struct {
            std::atomic<bool> expected_;  // true if we are waiting for a CUSTOM message/response, false otherwise
            size_t param_id_;             // ID of the parameter we are changing
            size_t list_id_;              // ID into the list inside the parameter (0 for non-lists)
        } message_wait_;                  // waiting for CUSTOM message/response.

        std::mutex mutex_; // mutex for thread safety
    };
}