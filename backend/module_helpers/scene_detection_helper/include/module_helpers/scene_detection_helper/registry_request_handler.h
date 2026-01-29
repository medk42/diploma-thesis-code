#pragma once

#include "module_common/module_interface_.h"
#include "module_common/base_module.h"
#include "message_types.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <optional>

namespace aergo::module::helpers::scene_detection_helper
{
    class RegistryRequestHandler
    {
    public:

        RegistryRequestHandler(
            uint32_t scene_detection_request_channel_id,
            module::BaseModule* module
        );

        ~RegistryRequestHandler() noexcept;

        bool valid() const noexcept { return valid_; }

        bool handlesIngress(
            aergo::module::IModule::ProcessingType kind,
            uint32_t local_channel_id,
            aergo::module::ChannelIdentifier src,
            const aergo::module::message::MessageHeader& message
        ) const noexcept
        {
            std::lock_guard<std::mutex> lock(registry_mutex_);
            return kind == aergo::module::IModule::ProcessingType::REQUEST && local_channel_id == scene_detection_request_channel_id_ && src == target_channel_
            && message.id_ == registry_request_id_;
        }

        aergo::module::IModule::IngressDecision processIngress(
            aergo::module::IModule::ProcessingType kind,
            uint32_t local_channel_id,
            aergo::module::ChannelIdentifier src,
            const aergo::module::message::MessageHeader& message,
            aergo::module::IModule::QueueStatus queue_status
        ) noexcept
        {
            std::lock_guard<std::mutex> lock(registry_mutex_);
            return kind == aergo::module::IModule::ProcessingType::REQUEST && local_channel_id == scene_detection_request_channel_id_ && src == target_channel_
            && message.id_ == registry_request_id_ ? aergo::module::IModule::IngressDecision::ACCEPT : aergo::module::IModule::IngressDecision::DROP;
        }

        bool handlesResponse(
            uint32_t request_consumer_id,
            aergo::module::ChannelIdentifier source_channel,
            const aergo::module::message::MessageHeader& message
        ) const noexcept
        {
            std::lock_guard<std::mutex> lock(registry_mutex_);
            return request_consumer_id == scene_detection_request_channel_id_ && source_channel == target_channel_ && message.id_ == registry_request_id_;
        }

        // Returns true if this helper fully handled the response (i.e. it was a registry response)
        bool processResponse(const aergo::module::message::MessageHeader& message) noexcept;

        // Thread-safe snapshot of the current registered boxes
        std::optional<std::vector<RegisteredBox>> getRegisteredBoxesSnapshot() const;

    private:
        void registryRequestThread();

        uint32_t scene_detection_request_channel_id_{ 0 };
        module::BaseModule* module_{ nullptr };

        aergo::module::ChannelIdentifier target_channel_{};

        std::thread registry_request_thread_;
        std::atomic<bool> registry_thread_stop_{ false };
        mutable std::mutex registry_mutex_;
        std::condition_variable registry_cv_;
        bool registry_received_{ false };
        uint64_t registry_request_id_{ 0 };
        std::vector<RegisteredBox> registered_boxes_;

        bool valid_{ false };
    };

    inline RegistryRequestHandler::RegistryRequestHandler(
        uint32_t scene_detection_request_channel_id,
        module::BaseModule* module
    )
        : scene_detection_request_channel_id_(scene_detection_request_channel_id)
        , module_(module)
    {
        if (!module_)
        {
            return;
        }

        // Load and validate the scene detection request channel info
        aergo::module::InputChannelMapInfo::IndividualChannelInfo channel_info =
            module_->getRequestChannelInfo(scene_detection_request_channel_id_);
        if (channel_info.channel_identifier_ == nullptr || channel_info.channel_identifier_count_ == 0)
        {
            module_->log(module::logging::LogType::ERROR,
                 "RegistryRequestHandler: invalid scene detection request channel (no identifiers).");
            return;
        }

        // By contract there should be exactly one channel; pick the first
        target_channel_ = channel_info.channel_identifier_[0];

        valid_ = true;

        // Start background thread to request object registry
        registry_thread_stop_.store(false);
        registry_received_ = false;
        registry_request_thread_ = std::thread(&RegistryRequestHandler::registryRequestThread, this);
    }

    inline RegistryRequestHandler::~RegistryRequestHandler() noexcept
    {
        // Stop the registry request thread
        {
            std::lock_guard<std::mutex> lock(registry_mutex_);
            registry_thread_stop_.store(true);
        }
        registry_cv_.notify_one();

        if (registry_request_thread_.joinable())
        {
            registry_request_thread_.join();
        }
    }

    inline bool RegistryRequestHandler::processResponse(
        const aergo::module::message::MessageHeader& message) noexcept
    {
        Response response;
        if (!message.readAs(response) || response.req_type != ReqType::READ_REGISTRY)
        {
            // Not a registry response; let other handlers deal with it
            return false;
        }

        std::lock_guard<std::mutex> lock(registry_mutex_);
        if (!registry_received_ && message.id_ == registry_request_id_)
        {
            // Parse the registry response
            if (message.blob_count_ == 1 && message.blobs_ != nullptr)
            {
                if (response.parseRegistry(message.blobs_[0].data(), message.blobs_[0].size(), registered_boxes_))
                {
                    module_->log(module::logging::LogType::INFO,
                         "RegistryRequestHandler: Received object registry with " +
                         std::to_string(registered_boxes_.size()) + " registered objects.");
                    registry_received_ = true;
                    registry_cv_.notify_one();
                }
                else
                {
                    module_->log(module::logging::LogType::WARNING,
                         "RegistryRequestHandler: Failed to parse registry response.");
                }
            }
            else
            {
                module_->log(module::logging::LogType::WARNING,
                     "RegistryRequestHandler: Registry response missing blob data.");
            }
        }
        else
        {
            module_->log(module::logging::LogType::WARNING,
                 "RegistryRequestHandler: Received registry response, but ID does not match pending request or already "
                 "received.");
        }

        // We handled a READ_REGISTRY response (even if parsing failed), so signal that it
        // should not be passed further to BaseUsecase.
        return true;
    }

    inline std::optional<std::vector<RegisteredBox>> RegistryRequestHandler::getRegisteredBoxesSnapshot() const
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        if (registry_received_)
        {
            return registered_boxes_;
        }
        else
        {
            module_->log(module::logging::LogType::WARNING, "RegistryRequestHandler: Registry not received yet, returning nullopt.");
            return std::nullopt;
        }
    }

    inline void RegistryRequestHandler::registryRequestThread()
    {
        Request request = Request::readRegistry();
        aergo::module::message::MessageHeader message =
            aergo::module::message::MessageHeader::Message(&request);

        while (!registry_thread_stop_.load())
        {
            std::unique_lock<std::mutex> lock(registry_mutex_);
            registry_request_id_ = module_->sendRequest(scene_detection_request_channel_id_, target_channel_, message);

            module_->log(module::logging::LogType::INFO,
                 "RegistryRequestHandler: Sent registry request, waiting for response...");

            // Wait for response or timeout (10 seconds)
            if (registry_cv_.wait_for(lock, std::chrono::seconds(10), [this]()
                {
                    return registry_received_ || registry_thread_stop_.load();
                }))
            {
                if (registry_received_)
                {
                    module_->log(module::logging::LogType::INFO,
                         "RegistryRequestHandler: Registry received successfully, thread finishing.");
                    break; // Exit thread
                }
                // else thread_stop was set, exit loop
            }
            else
            {
                module_->log(module::logging::LogType::WARNING,
                     "RegistryRequestHandler: Registry request timeout without response, will retry. "
                     "Last request id: " + std::to_string(registry_request_id_));
            }
        }

        module_->log(module::logging::LogType::INFO, "RegistryRequestHandler: Registry request thread exiting.");
    }
}

