#pragma once

#include "module_common/module_interface_.h"
#include "message_types.h"

namespace aergo::module::helpers::activation_wrapper
{
    class IActivableModule
    {
    public:
        virtual ~IActivableModule() = default;

        /// @brief Activate the module with specified parameters. During long operation check cancel_flag periodically and return if set to true.
        /// If the activation is cancelled, set cancelled to true and the module should stay deactivated.
        /// @return true if activation finished successfully, false if failed
        virtual bool activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) = 0;

        /// @brief Deactivate the module. During long operation check cancel_flag periodically and return if set to true. 
        /// If the deactivation is cancelled, set cancelled to true and the module should stay activated.
        /// @return true if deactivation finished successfully, false failed
        virtual bool deactivate(const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) = 0;

        /// @brief Send request from activation wrapper to the module. Used to request CUSTOM parameter values.
        /// @param request_consumer_id ID of the request consumer channel to send the request to
        virtual void sendRequestFromActivation(uint32_t request_consumer_id) = 0;

        /// @brief Get current de/activation status of the module.
        virtual message_types::ProgressData getActivationProgress() = 0;

        /// @brief Check if the module is currently activated, used to determine if module is active after load. 
        virtual bool isActivated() = 0;
    };
}