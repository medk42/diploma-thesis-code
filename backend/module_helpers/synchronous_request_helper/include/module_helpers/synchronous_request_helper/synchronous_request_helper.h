#pragma once

#include "module_common/module_interface_.h"
#include "module_common/base_module.h"

#include <map>
#include <optional>
#include <span>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <chrono>
#include <type_traits>

namespace aergo::module::helpers::synchronous_request_helper
{
    struct RequestChannelInfo
    {
        const uint32_t local_channel_id_;      // ID of the local request consumer channel to send requests from
        const ChannelIdentifier target_channel_;  // target response channel (module and channel ID)
    };



    enum class RequestResult : uint8_t
    { 
        SUCCESS,                  // request sent and response received successfully
        FAILURE,                  // request sent, response indicates failure (success == false)
        ALLOC_FAILED,             // failed to allocate memory for request blobs or request blob is provided, but allocator is nullptr
        UNKNOWN_REQ_ENUM,         // request type does not match any configured request channel
        MISMATCHED_RESPONSE_TYPE, // response received, but data is either nullptr or of incorrect size
        TIMEOUT,                  // request sent, no response received within timeout
        CANCELLED,                // request sent, but waiting for response was cancelled before it was received
        QUEUE_FULL,               // request sent, but failed to receive response because local queue was full
        ALREADY_PENDING           // request is already pending, cannot send another
    };



    /// @brief Helper class to perform synchronous requests to other modules.
    /// Wraps around IModule and provides synchronous request functionality.
    /// @tparam ReqEnumT Enum type defining request types.
    template <typename ReqEnumT>
    class SynchronousRequestHelper
    {
    public:
        static_assert(std::is_enum_v<ReqEnumT>, "ReqT must be an enum type");

        /// @param request_channels map of request channels for synchronous requests
        /// @param base_module reference to base module to allow sending requests and logging, must not be nullptr
        SynchronousRequestHelper(std::map<ReqEnumT, RequestChannelInfo> request_channels, aergo::module::BaseModule& base_module);

        inline virtual ~SynchronousRequestHelper() = default;

        bool handlesIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src) const noexcept;
        aergo::module::IModule::IngressDecision onIngress(aergo::module::IModule::ProcessingType kind, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept;

        bool handlesResponse(uint32_t request_consumer_id, ChannelIdentifier src) const noexcept;
        void processResponse(message::MessageHeader message) noexcept;
        
        /// @brief Send a synchronous request and wait for response or timeout (blocking call until response, timeout or asynchronous cancel call).
        /// Request is sent on the request channel id and target_channel mapped to the specified request type.
        /// Waiting for response can be cancelled by calling cancelRequest().
        /// Sending another request while a previous request is still pending will return ALREADY_PENDING.
        /// After a request is finished, result is indicated by the return value, response data is filled into response_data and response blobs into response_blobs (if not nullptr).
        /// 
        /// Return RequestResult::SUCCESS only if response was received successfully and indicates success (response_data.success_ == true).
        /// Return RequestResult::FAILURE if response was received successfully but indicates failure (response_data.success_ == false).
        /// Return RequestResult::ALLOC_FAILED if memory allocation for request blobs failed or blob data is provided but allocator is nullptr.
        /// Return RequestResult::UNKNOWN_REQ_ENUM if the request type does not match any configured request channel.
        /// Return RequestResult::MISMATCHED_RESPONSE_TYPE if response was received but data is either nullptr or of incorrect size.
        /// Return RequestResult::TIMEOUT if no response was received within the specified timeout.
        /// Return RequestResult::CANCELLED if waiting for response was cancelled before response was received.
        /// Return RequestResult::QUEUE_FULL if response came but could not be processed because local queue was full.
        /// Return RequestResult::ALREADY_PENDING if called asynchronously while a previous request is still pending.
        /// @tparam Treq type of request data, must be trivially copyable
        /// @tparam Tresp type of response data, must be trivially copyable, size must match expected response size
        /// @tparam ByteT byte type for request blob data (size 1)
        /// @param request_type type of request to send (used to find request channel) - e.g. ReqEnumT::RobotRequest, ReqEnumT::SceneRequest, ...
        /// @param request_data request data to send
        /// @param blob_data optional blob data to send with the request or nullopt for no blob (empty span will end up allocating 0 byte blob)
        /// @param response_data reference to response data object to fill on successful response, size must match expected response size
        /// @param response_blobs optional pointer to vector to fill with response blobs on successful response, can be nullptr if blobs are not needed
        /// @param allocator allocator to use for request blob allocation, must not be nullptr if blob_data is provided
        /// @param timeout_ms timeout in milliseconds to wait for response, 0 means no timeout (wait indefinitely)
        /// @return result of the request
        template <typename Treq, typename Tresp, typename ByteT>
        RequestResult sendSynchronousRequest(
            ReqEnumT request_type,
            Treq& request_data,
            std::optional<std::span<ByteT>> blob_data,
            Tresp& response_data,
            std::vector<std::vector<std::byte>>* response_blobs = nullptr,
            aergo::module::IAllocator* allocator = nullptr,
            uint32_t timeout_ms = 0
        ) noexcept;

        /// @brief Cancel waiting for a synchronous request response. Does nothing if no request is pending.
        void cancelRequest() noexcept;

    private:
        aergo::module::BaseModule& base_module_ref_; // reference to base module to allow sending requests and logging
        const std::map<ReqEnumT, RequestChannelInfo> request_channels_; // map of request channels for synchronous requests

        std::mutex mutex_; // mutex for thread safety
        bool request_pending_{false}; // is a request currently pending
        uint64_t pending_request_id_{0}; // ID of the pending request

        std::condition_variable request_cv_; // condition variable to signal response arrival
        bool response_available_{false}; // is response available
        bool cancelled_{false}; // was waiting for response cancelled
        bool response_queue_full_{false}; // was response dropped due to full queue
        message::MessageHeader* response_data_{nullptr}; // pointer to response data

        std::condition_variable response_cv_; // condition variable to signal response processed

    };



    template <typename ReqEnumT>
    SynchronousRequestHelper<ReqEnumT>::SynchronousRequestHelper(std::map<ReqEnumT, RequestChannelInfo> request_channels, aergo::module::BaseModule& base_module)
        : request_channels_(std::move(request_channels))
        , base_module_ref_(base_module) {}



    template <typename ReqEnumT>
    bool SynchronousRequestHelper<ReqEnumT>::handlesIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src) const noexcept
    {
        if (kind == aergo::module::IModule::ProcessingType::RESPONSE)
        {
            return std::any_of(
                request_channels_.begin(),
                request_channels_.end(),
                [local_channel_id, src](const auto& pair)
                {
                    const auto& channel_info = pair.second;
                    return channel_info.local_channel_id_ == local_channel_id &&
                           channel_info.target_channel_ == src;
                }
            );
        }

        return false;
    }



    template <typename ReqEnumT>
    aergo::module::IModule::IngressDecision SynchronousRequestHelper<ReqEnumT>::onIngress(aergo::module::IModule::ProcessingType kind, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
    {
        if (kind == aergo::module::IModule::ProcessingType::RESPONSE && queue_status == aergo::module::IModule::QueueStatus::QUEUE_FULL)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (request_pending_ && pending_request_id_ == msg.id_)
            {
                response_available_ = true;
                response_queue_full_ = true;
                request_cv_.notify_all();
                return aergo::module::IModule::IngressDecision::DROP;
            }
        }
        
        // if for us, but QUEUE_NORMAL, accept normally to read; otherwise likely not for us, so accept to be safe
        return aergo::module::IModule::IngressDecision::ACCEPT;
    }



    template <typename ReqEnumT>
    bool SynchronousRequestHelper<ReqEnumT>::handlesResponse(uint32_t request_consumer_id, ChannelIdentifier src) const noexcept
    {
        return handlesIngress(aergo::module::IModule::ProcessingType::RESPONSE, request_consumer_id, src); // responses are handled the same way as ingress
    }



    template <typename ReqEnumT>
    void SynchronousRequestHelper<ReqEnumT>::processResponse(message::MessageHeader message) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (request_pending_ && pending_request_id_ == message.id_)
        {
            response_available_ = true;
            response_data_ = &message;
            request_cv_.notify_all();

            // wait until response data is processed, request will indicate completion by clearing response_data_ pointer
            response_cv_.wait(
                lock,
                [this]() { return response_data_ == nullptr; }
            );
        }
    }



    template <typename ReqEnumT>
    void SynchronousRequestHelper<ReqEnumT>::cancelRequest() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (request_pending_)
        {
            cancelled_ = true;
            request_cv_.notify_all();
        }
    }



    template <typename ReqEnumT>
    template <typename Treq, typename Tresp, typename ByteT>
    RequestResult SynchronousRequestHelper<ReqEnumT>::sendSynchronousRequest(
        ReqEnumT request_type,
        Treq& request_data,
        std::optional<std::span<ByteT>> blob_data,
        Tresp& response_data,
        std::vector<std::vector<std::byte>>* response_blobs,
        aergo::module::IAllocator* allocator,
        uint32_t timeout_ms
    ) noexcept
    {
        static_assert(std::is_trivially_copyable_v<Treq>, "Treq must be trivially copyable");
        static_assert(std::is_trivially_copyable_v<Tresp>, "Tresp must be trivially copyable");
        static_assert(sizeof(ByteT) == 1, "ByteT must be a byte type (size 1)");

        std::lock_guard<std::mutex> lock(mutex_);

        if (request_pending_)
        {
            return RequestResult::ALREADY_PENDING;
        }

        if (cancelled_)
        {
            cancelled_ = false;
            return RequestResult::CANCELLED;
        }

        auto it = request_channels_.find(request_type);
        if (it == request_channels_.end())
        {
            base_module_ref_.log(aergo::module::logging::LogType::ERROR, "SynchronousRequestHelper: sendSynchronousRequest failed - unknown request type enum.");
            return RequestResult::UNKNOWN_REQ_ENUM;
        }
        const auto& channel_info = it->second;

        aergo::module::message::SharedDataBlob request_blob;
        bool with_blob = blob_data.has_value();
        if (with_blob)
        {
            if (allocator == nullptr)
            {
                base_module_ref_.log(aergo::module::logging::LogType::WARNING, "SynchronousRequestHelper: sendSynchronousRequest failed - blob data provided but allocator is nullptr.");
                return RequestResult::ALLOC_FAILED;
            }

            request_blob = allocator->allocate(blob_data->size());
            if (!request_blob.valid() || request_blob.size() != blob_data->size())
            {
                base_module_ref_.log(aergo::module::logging::LogType::ERROR, "SynchronousRequestHelper: sendSynchronousRequest failed - allocator failed to allocate memory for request blob.");
                return RequestResult::ALLOC_FAILED;
            }
        }

        aergo::module::message::MessageHeader request_message;
        request_message.data_ = reinterpret_cast<uint8_t*>(&request_data);
        request_message.data_len_ = sizeof(Treq);
        request_message.blobs_ = with_blob ? &request_blob : nullptr;
        request_message.blob_count_ = with_blob ? 1 : 0;
    
        uint64_t request_id = base_module_ref_.sendRequest(channel_info.local_channel_id_, channel_info.target_channel_, request_message);
        request_pending_ = true;
        response_available_ = false;
        response_queue_full_ = false;
        pending_request_id_ = request_id;



        std::chrono::milliseconds wait_for_timeout = std::chrono::milliseconds(timeout_ms == 0 ? std::numeric_limits<uint32_t>::max() : timeout_ms);

        request_cv_.wait_for(
            lock,
            std::chrono::milliseconds(wait_for_timeout),
            [this]() { return response_available_ || cancelled_; }
        );
        
        request_pending_ = false;

        if (cancelled_)
        {
            base_module_ref_.log(aergo::module::logging::LogType::INFO, "SynchronousRequestHelper: sendSynchronousRequest - request waiting was cancelled.");
            cancelled_ = false;
            return RequestResult::CANCELLED;
        }

        if (!response_available_)
        {
            base_module_ref_.log(aergo::module::logging::LogType::WARNING, "SynchronousRequestHelper: sendSynchronousRequest failed - timeout waiting for response with timeout " + std::to_string(timeout_ms) + " ms.");
            return RequestResult::TIMEOUT;
        }

        if (response_queue_full_)
        {
            base_module_ref_.log(aergo::module::logging::LogType::WARNING, "SynchronousRequestHelper: sendSynchronousRequest failed - response could not be processed because local queue was full.");
            response_queue_full_ = false;
            return RequestResult::QUEUE_FULL;
        }

        if (response_data_ == nullptr)
        {
            base_module_ref_.log(aergo::module::logging::LogType::ERROR, "SynchronousRequestHelper: sendSynchronousRequest failed - no response data received.");
            return RequestResult::MISMATCHED_RESPONSE_TYPE; // not exactly correct, but indicates error well
        } 

        if (response_data_->success_ == false)
        {
            base_module_ref_.log(aergo::module::logging::LogType::WARNING, "SynchronousRequestHelper: sendSynchronousRequest failed - response indicates failure.");

            // response indicates failure
            response_data_ = nullptr; // indicate response processed
            response_cv_.notify_all();

            return RequestResult::FAILURE;
        }
        
        if (response_data_->data_ == nullptr || response_data_->data_len_ != sizeof(Tresp) || (response_blobs == nullptr && response_data_->blob_count_ > 0))
        {
            base_module_ref_.log(aergo::module::logging::LogType::ERROR, "SynchronousRequestHelper: sendSynchronousRequest failed - mismatched response data type or size.");

            response_data_ = nullptr; // indicate response processed
            response_cv_.notify_all();

            return RequestResult::MISMATCHED_RESPONSE_TYPE;
        }

        // copy response data
        std::memcpy(&response_data, response_data_->data_, sizeof(Tresp));
        // copy response blobs if requested
        if (response_blobs != nullptr)
        {
            response_blobs->clear();
            response_blobs->reserve(static_cast<size_t>(response_data_->blob_count_));

            for (uint64_t i = 0; i < response_data_->blob_count_; ++i)
            {
                const aergo::module::message::SharedDataBlob& resp_blob = response_data_->blobs_[i];
                if (resp_blob.valid() && resp_blob.size() > 0)
                {
                    std::vector<std::byte> blob_data_vector(resp_blob.size());
                    std::memcpy(blob_data_vector.data(), resp_blob.data(), resp_blob.size());
                    response_blobs->emplace_back(std::move(blob_data_vector));
                }
                else
                {
                    base_module_ref_.log(aergo::module::logging::LogType::WARNING, "SynchronousRequestHelper: sendSynchronousRequest - received invalid or empty response blob on index " + std::to_string(i) + "/" + std::to_string(response_data_->blob_count_) + ".");
                    response_blobs->emplace_back(); // empty blob
                }
            }
        }

        response_data_ = nullptr; // indicate response processed
        response_cv_.notify_all();
        
        return RequestResult::SUCCESS;
    }

}
