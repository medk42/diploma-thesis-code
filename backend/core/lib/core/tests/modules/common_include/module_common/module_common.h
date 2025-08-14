#pragma once

#include "module_common/base_module.h"

namespace aergo::tests::core_1
{
    class ModuleCommon : public aergo::module::BaseModule
    {
    public:
        ModuleCommon(const char* data_path, aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id)
        : BaseModule(data_path, core, channel_map_info, logger, module_id)
        {
            std::string log_msg = std::string( "Initializing module, ID: ") + std::to_string(module_id) + std::string(", data: ") + data_path;
            logger->log(aergo::module::logging::LogType::INFO, log_msg.c_str());
        }

        void publish(uint32_t source_channel_id, int value)
        {
            sendMessage(source_channel_id, {
                .data_ = (uint8_t*) &value,
                .data_len_ = sizeof(value),
                .blobs_ = nullptr,
                .blob_count_ = 0
            });
        }

        uint64_t request(uint32_t source_channel_id, aergo::module::ChannelIdentifier target, int value)
        {
            return sendRequest(source_channel_id, target, {
                .data_ = (uint8_t*) &value,
                .data_len_ = sizeof(value),
                .blobs_ = nullptr,
                .blob_count_ = 0
            });
        }

        enum class msg_type { INVALID, MESSAGE, REQUEST, RESPONSE };

        msg_type last_msg_type_ = msg_type::INVALID;
        int last_msg_data_;
        uint64_t last_msg_id_;
        uint32_t last_channel_id_;
        aergo::module::ChannelIdentifier last_source_channel_;

    protected:
        void processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override
        {
            if (message.data_len_ != sizeof(int))
            {
                log(aergo::module::logging::LogType::ERROR, "Message not int!");
                std::terminate();
            }

            last_msg_type_ = msg_type::MESSAGE;
            last_msg_data_ = *message.data_;
            last_channel_id_ = subscribe_consumer_id;
            last_source_channel_ = source_channel;
        }

        void processRequest(uint32_t response_producer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override
        {
            if (message.data_len_ != sizeof(int))
            {
                log(aergo::module::logging::LogType::ERROR, "Message not int!");
                std::terminate();
            }

            last_msg_type_ = msg_type::REQUEST;
            last_msg_data_ = *message.data_;
            last_msg_id_ = message.id_;
            last_channel_id_ = response_producer_id;
            last_source_channel_ = source_channel;

            sendResponse(response_producer_id, source_channel, message.id_, {
                .data_ = (uint8_t*) &last_msg_data_,
                .data_len_ = sizeof(last_msg_data_),
                .blobs_ = nullptr,
                .blob_count_ = 0,
                .success_ = true
            });
        }
        void processResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override
        {
            if (message.data_len_ != sizeof(int))
            {
                log(aergo::module::logging::LogType::ERROR, "Message not int!");
                std::terminate();
            }

            last_msg_type_ = msg_type::RESPONSE;
            last_msg_data_ = *message.data_;
            last_msg_id_ = message.id_;
            last_channel_id_ = request_consumer_id;
            last_source_channel_ = source_channel;
        }

        void cycleImpl() noexcept override {}
    };
}