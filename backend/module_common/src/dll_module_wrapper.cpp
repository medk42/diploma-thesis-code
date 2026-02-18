#include "module_common/dll_module_wrapper.h"

#include <chrono>
#include <algorithm>
#include <cstring>

using namespace aergo::module;
using namespace aergo::module::dll;



static_assert(processing_data_alignment >= std::hardware_destructive_interference_size,
                  "processing_data_alignment must be >= hardware_destructive_interference_size");



DllModuleWrapper::DllModuleWrapper(std::unique_ptr<aergo::module::IModule> module, aergo::module::ICore* core, uint64_t module_id, const aergo::module::logging::ILogger* logger)
: module_(std::move(module)), core_(core), module_id_(module_id), logger_(logger)
{
    if (module_ == nullptr || !module_->valid())
    {
        throw std::invalid_argument("DllModuleWrapper: Invalid constructor parameters.");
    }

    module_info_ = module_->getModuleInfo();

    if (module_info_ == nullptr)
    {
        throw std::invalid_argument("DllModuleWrapper: ModuleInfo is nullptr.");
    }

    metrics_ = std::make_unique<Metrics>(module_info_);

    messages_channel_count_ = module_info_->subscribe_consumer_count_;
    requests_channel_count_ = module_info_->response_producer_count_;
    responses_channel_count_ = module_info_->request_consumer_count_;
    
    uint32_t total_channels = messages_channel_count_ + requests_channel_count_ + responses_channel_count_;
    available_queues_.reserve(total_channels);
    to_be_processed_queues_.reserve(total_channels);
    is_queue_prioritized_.resize(total_channels, false);

    // Determine which channels are prioritized and their capacities
    for (uint32_t i = 0; i < messages_channel_count_; ++i)
    {
        if (module_info_->subscribe_consumers_[i].prioritized_)
        {
            is_queue_prioritized_[i] = true;
        }

        size_t required_capacity = module_info_->subscribe_consumers_[i].message_queue_capacity_;
        if (required_capacity < 1) required_capacity = 1; // minimum capacity
        available_queues_.emplace_back(required_capacity);
        to_be_processed_queues_.emplace_back(required_capacity);
        for (size_t j = 0; j < required_capacity; ++j)
        {
            if (available_queues_.back().tryPush(std::make_unique<ProcessingData>()))
            {
                logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Failed to initialize available ProcessingData pool.");
            }
        }
    }
    for (uint32_t i = 0; i < requests_channel_count_; ++i)
    {
        uint32_t idx = messages_channel_count_ + i;
        if (module_info_->response_producers_[i].prioritized_)
        {
            is_queue_prioritized_[idx] = true;
        }

        size_t required_capacity = module_info_->response_producers_[i].message_queue_capacity_;
        if (required_capacity < 1) required_capacity = 1; // minimum capacity
        available_queues_.emplace_back(required_capacity);
        to_be_processed_queues_.emplace_back(required_capacity);
        for (size_t j = 0; j < required_capacity; ++j)
        {
            if (available_queues_.back().tryPush(std::make_unique<ProcessingData>()))
            {
                logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Failed to initialize available ProcessingData pool.");
            }
        }
    }
    for (uint32_t i = 0; i < responses_channel_count_; ++i)
    {
        uint32_t idx = messages_channel_count_ + requests_channel_count_ + i;
        if (module_info_->request_consumers_[i].prioritized_)
        {
            is_queue_prioritized_[idx] = true;
        }

        size_t required_capacity = module_info_->request_consumers_[i].message_queue_capacity_;
        if (required_capacity < 1) required_capacity = 1; // minimum capacity
        available_queues_.emplace_back(required_capacity);
        to_be_processed_queues_.emplace_back(required_capacity);
        for (size_t j = 0; j < required_capacity; ++j)
        {
            if (available_queues_.back().tryPush(std::make_unique<ProcessingData>()))
            {
                logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Failed to initialize available ProcessingData pool.");
            }
        }
    }
}



DllModuleWrapper::~DllModuleWrapper()
{
    stop_threads_ = true;
    prioritized_worker_cv_.notify_all();
    regular_worker_cv_.notify_all();

    for (auto& thread : prioritized_worker_threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    for (auto& thread : regular_worker_threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}



bool DllModuleWrapper::threadStart(uint32_t timeout_ms) noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (prioritized_worker_threads_.size() > 0 || regular_worker_threads_.size() > 0)
    {
        return false; // already started
    }
    
    uint8_t prioritized_workers_count = (module_info_->prioritized_workers_count_ > 0) ? module_info_->prioritized_workers_count_ : 1;
    uint8_t regular_workers_count = (module_info_->regular_workers_count_ > 0) ? module_info_->regular_workers_count_ : 1;

    stop_threads_ = false;
    prioritized_worker_running_count_ = 0;
    regular_worker_running_count_ = 0;

    for (uint16_t i = 0; i < prioritized_workers_count; ++i)
    {
        prioritized_worker_threads_.emplace_back(&DllModuleWrapper::prioritizedWorkerThreadFunc, this);
    }
    
    for (uint16_t i = 0; i < regular_workers_count; ++i)
    {
        regular_worker_threads_.emplace_back(&DllModuleWrapper::regularWorkerThreadFunc, this);
    }

    lock.unlock();

    // wait for threads to start
    auto start_time = nowMs();
    while (nowMs() - start_time < timeout_ms)
    {
        if (prioritized_worker_running_count_ == prioritized_workers_count && regular_worker_running_count_ == regular_workers_count)
        {
            auto time_used = nowMs() - start_time;
            if (time_used >= timeout_ms) return false;
            return module_->threadStart((uint32_t)(timeout_ms - time_used)); // notify module to start its threads if any
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}



bool DllModuleWrapper::threadStop(uint32_t timeout_ms) noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);

    // clear queues to remove SharedDataBlob references before allocators are destroyed in destructors
    module_stopping_ = true;
    std::for_each(to_be_processed_queues_.begin(), to_be_processed_queues_.end(), [this](auto& q) {
        while (true) {
            auto data_exp = q.tryPop();
            if (!data_exp.has_value()) break;
            auto data_ptr = std::move(data_exp.value());

            if (data_ptr->processing_type_ == aergo::module::IModule::ProcessingType::REQUEST) {
                // send failed response for pending requests
                sendFailedResponse(data_ptr->local_channel_id_, data_ptr->source_channel_, data_ptr->message_.id_);
            }
            data_ptr->blobs_.clear(); // release SharedDataBlob references
        }
     });

    if (prioritized_worker_threads_.size() == 0 && regular_worker_threads_.size() == 0)
    {
        return false; // not running
    }

    stop_threads_ = true;
    
    lock.unlock();

    prioritized_worker_cv_.notify_all();
    regular_worker_cv_.notify_all();

    auto start_time = nowMs();
    while (nowMs() - start_time < timeout_ms)
    {
        if (prioritized_worker_running_count_ == 0 && regular_worker_running_count_ == 0)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (prioritized_worker_running_count_ == 0 && regular_worker_running_count_ == 0)
    {
        for (auto& thread : prioritized_worker_threads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        prioritized_worker_threads_.clear();

        for (auto& thread : regular_worker_threads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        regular_worker_threads_.clear();

        metrics_->printLogs(logger_);

        auto time_used = nowMs() - start_time;
        if (time_used >= timeout_ms) return false;
        return module_->threadStop((uint32_t)(timeout_ms - time_used)); // notify module to stop its threads if any
    }
    else
    {
        logger_->log(logging::LogType::WARNING, "DllModuleWrapper: Failed to stop all worker threads within timeout.");
        return false;
    }
}



void DllModuleWrapper::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    pushProcessingData(aergo::module::IModule::ProcessingType::MESSAGE, subscribe_consumer_id, source_channel, message);
}



void DllModuleWrapper::processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    pushProcessingData(aergo::module::IModule::ProcessingType::REQUEST, response_producer_id, source_channel, message);
}



void DllModuleWrapper::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    pushProcessingData(aergo::module::IModule::ProcessingType::RESPONSE, request_consumer_id, source_channel, message);
}



void DllModuleWrapper::pushProcessingData(aergo::module::IModule::ProcessingType type, uint32_t local_channel_id, ChannelIdentifier source_channel, message::MessageHeader message)
{
    uint32_t idx;
    switch (type)
    {
        case aergo::module::IModule::ProcessingType::MESSAGE:
            if (local_channel_id >= messages_channel_count_) return;
            idx = local_channel_id;
            break;
        case aergo::module::IModule::ProcessingType::REQUEST:
            if (local_channel_id >= requests_channel_count_) return;
            idx = messages_channel_count_ + local_channel_id;
            break;
        case aergo::module::IModule::ProcessingType::RESPONSE:
            if (local_channel_id >= responses_channel_count_) return;
            idx = messages_channel_count_ + requests_channel_count_ + local_channel_id;
            break;
        default:
            return;
    }

    if (idx >= available_queues_.size())
    {
        logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Invalid channel index in pushProcessingData.");
        return; // invalid channel index
    }

    bool is_prioritized = is_queue_prioritized_[idx];
    RingBuffer<std::unique_ptr<ProcessingData>>& available_queue = available_queues_[idx];
    RingBuffer<std::unique_ptr<ProcessingData>>& target_queue = to_be_processed_queues_[idx];

    std::unique_lock<std::mutex> lock(mutex_);

    if (module_stopping_)
    {
        // module is stopping, drop new messages
        if (type == aergo::module::IModule::ProcessingType::REQUEST)
        {
            sendFailedResponse(local_channel_id, source_channel, message.id_);
        }
        return;
    }
    
    size_t queue_size = available_queue.capacity() - available_queue.size();
    bool slot_available = !available_queue.empty();
    bool steal_available = !target_queue.empty(); // can only steal if there is at least one item in the processing queue

    aergo::module::IModule::QueueStatus queue_status = aergo::module::IModule::QueueStatus::NORMAL;
    if (!slot_available)
    {
        queue_status = steal_available ? aergo::module::IModule::QueueStatus::QUEUE_FULL_CAN_DROP : aergo::module::IModule::QueueStatus::QUEUE_FULL;
    }
    aergo::module::IModule::IngressDecision decision = module_->onIngress(type, local_channel_id, source_channel, message, queue_status);

    metrics_->record(idx, queue_size, decision, !slot_available); 

    // also always drop if queue capacity is 0 (should not happen, but just in case)
    if (decision == aergo::module::IModule::IngressDecision::DROP || 
        queue_status == aergo::module::IModule::QueueStatus::QUEUE_FULL || 
        (queue_status == aergo::module::IModule::QueueStatus::QUEUE_FULL_CAN_DROP && decision == aergo::module::IModule::IngressDecision::ACCEPT) ||
        available_queue.capacity() == 0)
    {
        if (type == aergo::module::IModule::ProcessingType::REQUEST)
        {
            sendFailedResponse(local_channel_id, source_channel, message.id_);
        }
        return; // drop message
    }
    else if (decision == aergo::module::IModule::IngressDecision::ACCEPT_DROP_QUEUE_FIRST)
    {
        if (steal_available) // we know capacity >= 1 here, so there is at least one item to drop
        {
            std::unique_ptr<ProcessingData> data_ptr = std::move(*target_queue.tryPop());
            data_ptr->blobs_.clear(); // release SharedDataBlob references
            if (data_ptr->processing_type_ == aergo::module::IModule::ProcessingType::REQUEST)
            {
                sendFailedResponse(data_ptr->local_channel_id_, data_ptr->source_channel_, data_ptr->message_.id_);
            }

            auto res = available_queue.tryPush(std::move(data_ptr)); // return to available pool (should always succeed)
            if (res.has_value()) // should not happen
            {
                logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Unexpected error - failed to return ProcessingData to available pool.");
            }
        }
        else 
        {
            logger_->log(logging::LogType::WARNING, "DllModuleWrapper: ACCEPT_DROP_QUEUE_FIRST decision made but no messages to drop.");
        }
    }
    else if (decision == aergo::module::IModule::IngressDecision::ACCEPT_REPLACE_QUEUE)
    {
        while (true)
        {
            std::optional<std::unique_ptr<ProcessingData>> data_opt = target_queue.tryPop();
            if (!data_opt.has_value()) break; // queue is empty

            std::unique_ptr<ProcessingData> data_ptr = std::move(data_opt.value());
            data_ptr->blobs_.clear(); // release SharedDataBlob references
            if (data_ptr->processing_type_ == aergo::module::IModule::ProcessingType::REQUEST)
            {
                sendFailedResponse(data_ptr->local_channel_id_, data_ptr->source_channel_, data_ptr->message_.id_);
            }

            auto res = available_queue.tryPush(std::move(data_ptr)); // return to available pool (should always succeed)
            if (res.has_value()) // should not happen
            {
                logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Unexpected error - failed to return ProcessingData to available pool.");
            }
        }
    }

    // Now there is guaranteed space in the queue
    std::optional<std::unique_ptr<ProcessingData>> data_opt = available_queue.tryPop();
    if (!data_opt.has_value())
    {
        logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Unexpected error - no available ProcessingData to push.");
        // should not happen
        if (type == aergo::module::IModule::ProcessingType::REQUEST)
        {
            sendFailedResponse(local_channel_id, source_channel, message.id_);
        }
        return;
    }
    std::unique_ptr<ProcessingData> data_ptr = std::move(data_opt.value());

    if (data_ptr->blobs_.size() > 0)
    {
        logger_->log(logging::LogType::WARNING, "DllModuleWrapper: Reusing ProcessingData with non-empty blobs_. Clearing previous blobs.");
        data_ptr->blobs_.clear(); // release previous SharedDataBlob references
    }

    data_ptr->processing_type_ = type;
    data_ptr->local_channel_id_ = local_channel_id;
    data_ptr->source_channel_ = source_channel;
    data_ptr->message_ = message;

    data_ptr->data_.reserve(message.data_len_);     // ensure enough capacity before memcpy
    std::memcpy(data_ptr->data_.data(), message.data_, message.data_len_); // raw copy message data
    data_ptr->message_.data_ = data_ptr->data_.data();
    
    // Use insert to call copy constructor of SharedDataBlob
    data_ptr->blobs_.insert(data_ptr->blobs_.end(), message.blobs_, message.blobs_ + message.blob_count_);
    data_ptr->message_.blobs_ = data_ptr->blobs_.data();

    auto res = target_queue.tryPush(std::move(data_ptr));; // push to processing queue
    if (res.has_value()) // should not happen
    {
        logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Unexpected error - failed to push ProcessingData to processing queue.");
        if (type == aergo::module::IModule::ProcessingType::REQUEST)
        {
            sendFailedResponse(local_channel_id, source_channel, message.id_);
        }
        return;
    }

    lock.unlock();

    if (is_prioritized)
    {
        prioritized_worker_cv_.notify_one();
    }
    else
    {
        regular_worker_cv_.notify_one();
    }
}



void DllModuleWrapper::sendFailedResponse(uint32_t local_channel_id, ChannelIdentifier source_channel, uint64_t message_id)
{
    core_->sendResponse({
            .module_id_ = module_id_, 
            .local_channel_id_ = local_channel_id
        },
        source_channel, {
            .data_ = nullptr,
            .data_len_ = 0,
            .blobs_ = nullptr,
            .blob_count_ = 0,
            .id_ = message_id,
            .timestamp_ns_ = nowNs(),
            .success_ = false
        }
    );
}



void DllModuleWrapper::regularWorkerThreadFunc()
{
    std::unique_lock<std::mutex> lock(mutex_);
    ++regular_worker_running_count_;
    while (!stop_threads_)
    {
        regular_worker_cv_.wait(lock, [&] { return stop_threads_ || !regularQueuesEmpty(); });
        if (stop_threads_)
        {
            break;
        }

        auto index_opt = getNonEmptyRegularQueue();
        if (!index_opt.has_value()) continue;
        
        auto data_opt = to_be_processed_queues_[index_opt.value()].tryPop();
        if (!data_opt.has_value())
        {
            logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Unexpected error - failed to pop from non-empty regular queue.");
            continue;
        }
        auto data_ptr = std::move(data_opt.value());

        lock.unlock();

        switch (data_ptr->processing_type_)
        {
            case aergo::module::IModule::ProcessingType::MESSAGE:
                module_->processMessage(data_ptr->local_channel_id_, data_ptr->source_channel_, data_ptr->message_);
                break;
            case aergo::module::IModule::ProcessingType::REQUEST:
            {
                auto resp = module_->processRequest(data_ptr->local_channel_id_, data_ptr->source_channel_, data_ptr->message_);
                core_->sendResponse({
                        .module_id_ = module_id_, 
                        .local_channel_id_ = data_ptr->local_channel_id_
                    },
                    data_ptr->source_channel_, {
                        .data_ = resp.data_.data(),
                        .data_len_ = static_cast<uint64_t>(resp.data_.size()),
                        .blobs_ = resp.blobs_.data(),
                        .blob_count_ = static_cast<uint64_t>(resp.blobs_.size()),
                        .id_ = data_ptr->message_.id_,
                        .timestamp_ns_ = nowNs(),
                        .success_ = resp.success_
                    }
                );
                break;
            }
                
            case aergo::module::IModule::ProcessingType::RESPONSE:
                module_->processResponse(data_ptr->local_channel_id_, data_ptr->source_channel_, data_ptr->message_);
                break;
        }

        data_ptr->blobs_.clear(); // release SharedDataBlob references

        lock.lock();

        auto res = available_queues_[index_opt.value()].tryPush(std::move(data_ptr)); // return to available pool (should always succeed)
        if (res.has_value()) // should not happen
        {
            logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Unexpected error - failed to return ProcessingData to available pool in regular worker.");
        }
    }
    --regular_worker_running_count_;
}



void DllModuleWrapper::prioritizedWorkerThreadFunc()
{
    std::unique_lock<std::mutex> lock(mutex_);
    ++prioritized_worker_running_count_;
    while (!stop_threads_)
    {
        prioritized_worker_cv_.wait(lock, [&] { return stop_threads_ || !prioritizedQueuesEmpty(); });
        if (stop_threads_)
        {
            break;
        }
        
        auto index_opt = getNonEmptyPrioritizedQueue();
        if (!index_opt.has_value()) continue;
        
        auto data_opt = to_be_processed_queues_[index_opt.value()].tryPop();
        if (!data_opt.has_value())
        {
            logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Unexpected error - failed to pop from non-empty prioritized queue.");
            continue;
        }
        auto data_ptr = std::move(data_opt.value());

        lock.unlock();

        aergo::module::ResponseData resp;
        switch (data_ptr->processing_type_)
        {
            case aergo::module::IModule::ProcessingType::MESSAGE:
                module_->processMessage(data_ptr->local_channel_id_, data_ptr->source_channel_, data_ptr->message_);
                break;
            case aergo::module::IModule::ProcessingType::REQUEST:
                resp = module_->processRequest(data_ptr->local_channel_id_, data_ptr->source_channel_, data_ptr->message_);
                core_->sendResponse({
                        .module_id_ = module_id_, 
                        .local_channel_id_ = data_ptr->local_channel_id_
                    },
                    data_ptr->source_channel_, {
                        .data_ = resp.data_.data(),
                        .data_len_ = static_cast<uint64_t>(resp.data_.size()),
                        .blobs_ = resp.blobs_.data(),
                        .blob_count_ = static_cast<uint64_t>(resp.blobs_.size()),
                        .id_ = data_ptr->message_.id_,
                        .timestamp_ns_ = nowNs(),
                        .success_ = resp.success_
                    }
                );
                break;
            case aergo::module::IModule::ProcessingType::RESPONSE:
                module_->processResponse(data_ptr->local_channel_id_, data_ptr->source_channel_, data_ptr->message_);
                break;
        }

        data_ptr->blobs_.clear(); // release SharedDataBlob references

        lock.lock();

        auto res = available_queues_[index_opt.value()].tryPush(std::move(data_ptr)); // return to available pool (should always succeed)
        if (res.has_value()) // should not happen
        {
            logger_->log(logging::LogType::ERROR, "DllModuleWrapper: Unexpected error - failed to return ProcessingData to available pool in prioritized worker.");
        }
    }
    --prioritized_worker_running_count_;
}



bool DllModuleWrapper::regularQueuesEmpty()
{
    for (size_t idx = 0; idx < to_be_processed_queues_.size(); ++idx)
    {
        if (!is_queue_prioritized_[idx] && !to_be_processed_queues_[idx].empty())
        {
            return false;
        }
    }

    return true;
}



bool DllModuleWrapper::prioritizedQueuesEmpty()
{
    for (size_t idx = 0; idx < to_be_processed_queues_.size(); ++idx)
    {
        if (is_queue_prioritized_[idx] && !to_be_processed_queues_[idx].empty())
        {
            return false;
        }
    }

    return true;
}



std::optional<size_t> DllModuleWrapper::getNonEmptyRegularQueue()
{
    for (size_t i = 0; i < to_be_processed_queues_.size(); ++i)
    {
        size_t idx = (next_regular_queue_idx_ + i) % to_be_processed_queues_.size(); // modulo is safe, if size is 0, the outer loop will not run
        if (!is_queue_prioritized_[idx] && !to_be_processed_queues_[idx].empty())
        {
            return idx;
        }
    }

    return std::nullopt;
} 



std::optional<size_t> DllModuleWrapper::getNonEmptyPrioritizedQueue()
{
    for (size_t i = 0; i < to_be_processed_queues_.size(); ++i)
    {
        size_t idx = (next_prioritized_queue_idx_ + i) % to_be_processed_queues_.size(); // modulo is safe, if size is 0, the outer loop will not run
        if (is_queue_prioritized_[idx] && !to_be_processed_queues_[idx].empty())
        {
            return idx;
        }
    }

    return std::nullopt;
}



aergo::module::IModule* DllModuleWrapper::getModule()
{
    return module_.get();
}



int64_t DllModuleWrapper::nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}



aergo::module::message::SharedDataBlob DllModuleWrapper::save(aergo::module::IAllocator* allocator) noexcept
{
    aergo::module::ISerializableModule::SaveData save_data(std::move(module_->save()));

    if (!save_data.success_)
    {
        return aergo::module::message::SharedDataBlob(); // invalid blob
    }

    std::vector<uint8_t> serialized_data;
    if (!save_toolkit::serialize(save_data, serialized_data))
    {
        return aergo::module::message::SharedDataBlob(); // invalid blob
    }

    auto blob = allocator->allocate(serialized_data.size());
    if (!blob.valid() || blob.size() < serialized_data.size())
    {
        return aergo::module::message::SharedDataBlob(); // invalid blob
    }

    std::memcpy(blob.data(), serialized_data.data(), serialized_data.size());
    return blob;
}



bool DllModuleWrapper::load(uint8_t* data, uint64_t data_size) noexcept
{
    aergo::module::ISerializableModule::SaveData save_data;
    
    save_toolkit::deserialize(data, data_size, save_data);

    return module_->load(std::move(save_data));
}