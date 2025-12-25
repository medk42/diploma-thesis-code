#pragma once

#include "module_common/module_interface_.h"
#include "message_types.h"
#include "module_helpers/parameter_description/parameter_description.h"

namespace aergo::module::helpers::activation_wrapper
{
    namespace p_desc = aergo::module::helpers::parameter_description;

    class IActivableModule
    {
    public:
        /// @brief Processing result for custom parameter loading. Together with the processCustomMessageOrResponse function,
        /// allows the activable module to control how incoming messages / responses are handled for CUSTOM parameter loading.
        /// ACCEPT: accept the incoming message / response as is, use the standard serialization.
        /// ACCEPT_REPLACE: accept the incoming message / response, but replace the data with custom data provided by the module.
        /// DROP: drop the incoming message, it does not contain the data needed for CUSTOM parameter loading (only for subscribe, 
        /// if used for request, behavior is same as ACCEPT).
        enum class ProcessingResult : uint8_t
        {
            ACCEPT         = 0,  // accept message as is
            ACCEPT_REPLACE = 1,  // accept message, but replace data with custom data
            DROP           = 2,  // drop message, wait for next
        };

        enum class ProcessingChannelType : uint8_t
        {
            MESSAGE = 0,
            RESPONSE = 1,
        };

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
        /// @param auto_parameters parameters that are set from the input Consumer channels (subscribe/request) - only CUSTOM type allowed (value or list of values)
        /// @param param_id ID of the parameter to request (index in auto_parameters)
        /// @param out_request_id output parameter to store generated request ID 
        /// @return true if request was sent successfully, false otherwise
        virtual bool sendRequestFromActivation(const std::vector<p_desc::ParameterDescription>& auto_parameters, const uint32_t param_id, uint64_t& out_request_id) = 0;

        /// @brief Process incoming message or response for CUSTOM parameter loading.
        /// This function is called when a message or response is received that may contain data for a CUSTOM parameter.
        /// The activable module can decide whether to accept the message/response as is, accept it but replace the data with custom data,
        /// or drop it and wait for the next message (only for subscribe, if used for request, behavior is same as ACCEPT).
        /// @param channel_type type of the channel (MESSAGE or RESPONSE)
        /// @param consumer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source request channel (module and channel ID)
        /// @param message the incoming message or response
        /// @param out_data_replace output parameter to store custom data to replace the message data with, if ACCEPT_REPLACE is returned
        /// @return ProcessingResult indicating how to handle the incoming message/response
        virtual ProcessingResult processCustomMessageOrResponse(ProcessingChannelType channel_type, uint32_t consumer_id, ChannelIdentifier source_channel, message::MessageHeader message, std::vector<uint8_t>& out_data_replace) = 0;

        /// @brief Get current de/activation status of the module.
        virtual message_types::ProgressData getActivationProgress() = 0;

        /// @brief Check if the module is currently activated, used to determine if module is active after load. 
        virtual bool isActivated() = 0;
    };
}