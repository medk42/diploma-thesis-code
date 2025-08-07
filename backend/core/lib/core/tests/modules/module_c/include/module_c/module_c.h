#pragma once

#include "module_common/module_wrapper.h"

namespace aergo::tests::core_1
{
    class ModuleC : public aergo::module::ModuleWrapper
    {
    public:
        ModuleC(aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id) : ModuleWrapper(core, channel_map_info, logger, module_id, 10) {}

    protected:
        void processMessageImpl(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) override {}
        void processRequestImpl(uint32_t response_producer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) override {}
        void processResponseImpl(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) override {}
        void cycleImpl() override {}
    };
}