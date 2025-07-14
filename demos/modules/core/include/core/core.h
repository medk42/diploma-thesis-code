#pragma once

#include "module_common/module_interface_threads.h"

namespace aergo::core
{
    class Core : public aergo::module::ICore
    {
    public:
        Core();

        void initialize(const char* modules_dir, const char* data_dir);

        virtual void sendMessage(uint64_t source_module_id, uint64_t publish_producer_id, aergo::module::message::MessageHeader message) override final;
        virtual void sendResponse(uint64_t source_module_id, uint64_t response_producer_id, aergo::module::message::MessageHeader message) override final;
        virtual void sendRequest(uint64_t source_module_id, uint64_t request_consumer_id, uint64_t target_module_id, aergo::module::message::MessageHeader message) override final;
        virtual aergo::module::IAllocatorCore* createDynamicAllocator() override final;
        virtual aergo::module::IAllocatorCore* createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots) override final;
        virtual void deleteAllocator(aergo::module::IAllocatorCore* allocator) override final;

    private:
    };
}