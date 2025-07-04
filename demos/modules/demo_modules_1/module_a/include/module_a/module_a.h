#pragma once

#include "module_common/module_wrapper.h"

namespace aergo::demo_modules_1::module_a
{
    class ModuleA : public aergo::module::ModuleWrapper
    {
    public:
        ModuleA(aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id);

    protected:
        void processMessageImpl(uint64_t subscribe_consumer_id, uint64_t module_id, aergo::module::message::MessageHeader message) override;
        void processRequestImpl(uint64_t response_producer_id, aergo::module::message::MessageHeader message) override;
        void processResponseImpl(uint64_t request_consumer_id, uint64_t module_id, aergo::module::message::MessageHeader message) override;
        void cycleImpl() override;
    };
}