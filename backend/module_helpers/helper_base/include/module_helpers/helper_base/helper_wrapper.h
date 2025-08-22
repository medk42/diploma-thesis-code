#pragma once

#include "module_common/base_module.h"
#include "module_common/module_interface_.h"
#include "helper_base.h"

#include <map>

namespace aergo::module::helpers::helper_base
{
    class HelperWrapper : IModule
    {
    public:
        HelperWrapper(std::unique_ptr<aergo::module::BaseModule> base_module, std::vector<std::unique_ptr<IHelperBase>> helper_wrappers);
        virtual ~HelperWrapper() = default;

        /// @return true if initialized successfully, false otherwise (multiple wrappers expect the same channel, base module nullptr)
        bool initialize(aergo::module::ModuleInfo module_info);

        void processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;
        void processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;
        void processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;
        void cycleImpl() noexcept override;
        void* query_capability(const std::type_info& id) noexcept override;

    private:

        std::unique_ptr<aergo::module::BaseModule> base_module_;
        std::vector<std::unique_ptr<IHelperBase>> helper_wrappers_;

        std::map<uint32_t, size_t> process_message_map_;
        std::map<uint32_t, size_t> process_request_map_;
        std::map<uint32_t, size_t> process_response_map_;
    };
}