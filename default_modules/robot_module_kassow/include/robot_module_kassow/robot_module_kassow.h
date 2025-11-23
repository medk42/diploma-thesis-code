#pragma once

#include "module_common/base_module.h"
#include "module_helpers/robot_interface/message_types.h"
#include "module_helpers/activation_wrapper/activable_module.h"
#include "robot_module_kassow/rpc/rpc_transport.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <span>
#include <vector>
#include <string>

namespace aergo::default_modules::robot_module_kassow
{
    class RobotModuleKassow : public aergo::module::BaseModule, public aergo::module::helpers::activation_wrapper::IActivableModule
    {
    public:
        RobotModuleKassow(const char* data_path,
                          aergo::module::ICore* core,
                          aergo::module::InputChannelMapInfo channel_map_info,
                          const aergo::module::logging::ILogger* logger,
                          uint64_t module_id,
                          const aergo::module::ModuleInfo* module_info);
        ~RobotModuleKassow() noexcept override;

        bool valid() noexcept override { return valid_; }
        void* query_capability(const std::type_info& id) noexcept override;

        IngressDecision onIngress(ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src, const aergo::module::message::MessageHeader& msg, QueueStatus queue_status) noexcept override;
        void processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;
        aergo::module::ResponseData processRequest(uint32_t response_producer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;
        void processResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;

        bool threadStart(uint32_t timeout_ms) noexcept override;
        bool threadStop(uint32_t timeout_ms) noexcept override;

        bool activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) override;
        bool deactivate(const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) override;
        void sendRequestFromActivation(uint32_t request_consumer_id) override { } // No activation-time requests needed.
        aergo::module::helpers::activation_wrapper::message_types::ProgressData getActivationProgress() override
        { return {.progress_type_ = aergo::module::helpers::activation_wrapper::message_types::ProgressType::NONE}; }

        bool isActivated() override { return activated_.load(std::memory_order_acquire); }

        aergo::module::ISerializableModule::SaveData save() noexcept override;
        bool load(aergo::module::ISerializableModule::SaveData data) noexcept override;

    private:
        void asyncPollLoop();
        bool ensureConnected();
        void forwardStatus(const aergo::module::helpers::robot_interface::StatusMessage& status, std::span<const std::byte> blob);
        void forwardFinished(const aergo::module::helpers::robot_interface::FinishedMessage& finished, std::span<const std::byte> blob);

        class LoggerAdapter : public aergo::robot::kassow::rpc::RpcLogger
        {
        public:
            explicit LoggerAdapter(const aergo::module::logging::ILogger* logger) : logger_(logger) {}
            void setLogger(const aergo::module::logging::ILogger* logger) { logger_ = logger; }
            void log(aergo::robot::kassow::rpc::RpcLogType type, const char* message) const noexcept override;
        private:
            const aergo::module::logging::ILogger* logger_;
        };

        bool valid_;
        uint32_t response_channel_id_;
        uint32_t status_publish_id_;
        uint32_t finished_publish_id_;

        LoggerAdapter rpc_logger_;
        std::unique_ptr<aergo::robot::kassow::rpc::RpcClient> rpc_client_;
        aergo::module::BaseModule::AllocatorPtr allocator_;

        std::thread async_thread_;
        std::atomic<bool> stop_async_;

        std::string kassow_host_ {"127.0.0.1"};
        uint16_t kassow_port_ {15050};
        uint32_t request_timeout_ms_ {200};
        uint32_t poll_interval_ms_ {10};
        uint32_t reconnect_wait_ms_ {500};

        std::atomic<bool> activated_;
        std::mutex activation_mutex_;
    };
}
