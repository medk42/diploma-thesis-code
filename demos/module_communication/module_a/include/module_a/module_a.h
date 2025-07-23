#pragma once

#include "module_common/module_wrapper.h"
#include <chrono>
#include <random>

namespace aergo::demo_modules_1::module_a
{
    class ModuleA : public aergo::module::ModuleWrapper
    {
    public:
        ModuleA(aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id);

    protected:
        void processMessageImpl(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) override;
        void processRequestImpl(uint32_t response_producer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) override;
        void processResponseImpl(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) override;
        void cycleImpl() override;

    private:
        uint64_t next_small_message_;
        uint64_t next_large_message_;

        int32_t small_message_counter_;

        std::mt19937 gen_;
        std::uniform_int_distribution<int> dist_;
        std::unique_ptr<aergo::module::Allocator> large_fixed_allocator_;

        std::unique_ptr<aergo::module::Allocator> request_dynamic_allocator_;

        inline uint64_t nowMs()
        {
            using namespace std::chrono;
            return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        }
    };
}