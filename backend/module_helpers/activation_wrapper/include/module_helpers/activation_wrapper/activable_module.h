#pragma once

#include "module_common/module_interface_.h"
#include "parameter_description.h"
#include "message_types.h"

namespace aergo::module::helpers::activation_wrapper
{
    class IActivableModule
    {
    public:
        /// @brief Activate the module with specified parameters. During long operation check cancel_flag periodically and return if set to true.
        /// @return true if activation finished successfully, false if cancelled (or failed)
        virtual bool activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, std::atomic<bool>& cancel_flag) = 0;

        /// @brief Deactivate the module. During long operation check cancel_flag periodically and return if set to true.
        /// @return true if deactivation finished successfully, false if cancelled (or failed)
        virtual bool deactivate(std::atomic<bool>& cancel_flag) = 0;

        /// @brief Send request from activation wrapper to the module. Used to request CUSTOM parameter values.
        /// @param request_consumer_id ID of the request consumer channel to send the request to
        virtual uint64_t sendRequestFromActivation(uint32_t request_consumer_id) = 0;

        /// @brief Get current de/activation status of the module.
        virtual message_types::ProgressData getActivationProgress() = 0;
    };
}