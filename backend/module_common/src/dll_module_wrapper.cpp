#include "module_common/dll_module_wrapper.h"

#include <chrono>

using namespace aergo::module;
using namespace aergo::module::dll;



DllModuleWrapper::DllModuleWrapper(std::unique_ptr<aergo::module::IModule> module, const aergo::module::ModuleInfo* module_info, const aergo::module::logging::ILogger* logger)
: module_(std::move(module)), module_info_(module_info), logger_(logger), metrics_(module_info)
{
    if (module_ == nullptr || !module_->valid() || module_info_ == nullptr)
    {
        throw std::invalid_argument("DllModuleWrapper: Invalid constructor parameters.");
    }

    messages_channel_count_ = module_info_->subscribe_consumer_count_;
    requests_channel_count_ = module_info_->response_producer_count_;
    responses_channel_count_ = module_info_->request_consumer_count_;
    
    uint32_t total_channels = messages_channel_count_ + requests_channel_count_ + responses_channel_count_;
    prioritized_queues_.resize(total_channels);
    regular_queues_.resize(total_channels);
    is_queue_prioritized_.resize(total_channels, false);
    queue_capacities_.resize(total_channels, 4); // default capacity

    // Determine which channels are prioritized and their capacities
    for (uint32_t i = 0; i < messages_channel_count_; ++i)
    {
        if (module_info_->subscribe_consumers_[i].prioritized_)
        {
            is_queue_prioritized_[i] = true;
        }
        if (module_info_->subscribe_consumers_[i].message_queue_capacity_ >= 1)
        {
            queue_capacities_[i] = module_info_->subscribe_consumers_[i].message_queue_capacity_;
        }
        else
        {
            queue_capacities_[i] = 1; // minimum capacity
        }
    }
    for (uint32_t i = 0; i < requests_channel_count_; ++i)
    {
        uint32_t idx = messages_channel_count_ + i;
        if (module_info_->response_producers_[i].prioritized_)
        {
            is_queue_prioritized_[idx] = true;
        }
        if (module_info_->response_producers_[i].message_queue_capacity_ >= 1)
        {
            queue_capacities_[idx] = module_info_->response_producers_[i].message_queue_capacity_;
        }
        else
        {
            queue_capacities_[idx] = 1; // minimum capacity
        }
    }
    for (uint32_t i = 0; i < responses_channel_count_; ++i)
    {
        uint32_t idx = messages_channel_count_ + requests_channel_count_ + i;
        if (module_info_->request_consumers_[i].prioritized_)
        {
            is_queue_prioritized_[idx] = true;
        }
        if (module_info_->request_consumers_[i].message_queue_capacity_ >= 1)
        {
            queue_capacities_[idx] = module_info_->request_consumers_[i].message_queue_capacity_;
        }
        else
        {
            queue_capacities_[idx] = 1; // minimum capacity
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
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}



bool DllModuleWrapper::threadStop(uint32_t timeout_ms) noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);
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

        metrics_.printLogs(logger_);

        return true;
    }
    else
    {
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

    bool is_prioritized = is_queue_prioritized_[idx];
    uint16_t capacity = queue_capacities_[idx];
    std::queue<ProcessingData>& target_queue = is_prioritized ? prioritized_queues_[idx] : regular_queues_[idx];

    std::unique_lock<std::mutex> lock(mutex_);
    
    bool queue_full = (target_queue.size() >= capacity);
    aergo::module::IModule::QueueStatus queue_status = queue_full ? aergo::module::IModule::QueueStatus::QUEUE_FULL : aergo::module::IModule::QueueStatus::NORMAL;
    aergo::module::IModule::IngressDecision decision = module_->onIngress(type, local_channel_id, source_channel, message, queue_status);

    metrics_.record(idx, target_queue.size(), decision, queue_full); 

    if (decision == aergo::module::IModule::IngressDecision::DROP || (decision == aergo::module::IModule::IngressDecision::ACCEPT && queue_full))
    {
        return; // drop message
    }
    else if (decision == aergo::module::IModule::IngressDecision::ACCEPT_DROP_QUEUE_FIRST)
    {
        if (queue_full)
        {
            target_queue.pop(); // drop oldest message
        }
    }
    else if (decision == aergo::module::IModule::IngressDecision::ACCEPT_REPLACE_QUEUE)
    {
        while (!target_queue.empty())
        {
            target_queue.pop(); // clear the queue
        }
    }

    // Copy data and blobs to ensure they remain valid
    std::vector<uint8_t> data(message.data_, message.data_ + message.data_len_);
    message.data_ = data.data();

    std::vector<message::SharedDataBlob> blobs(message.blobs_, message.blobs_ + message.blob_count_);
    message.blobs_ = blobs.data();

    ProcessingData processing_data {
        .processing_type_ = type,
        .local_channel_id_ = local_channel_id,
        .source_channel_ = source_channel,
        .message_ = message,
        .data_ = std::move(data),
        .blobs_ = std::move(blobs)
    };

    target_queue.push(std::move(processing_data));

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

        ProcessingData processing_data; 
        if (!popRegularProcessingData(processing_data))
        {
            continue;
        }

        lock.unlock();
        switch (processing_data.processing_type_)
        {
            case aergo::module::IModule::ProcessingType::MESSAGE:
                module_->processMessage(processing_data.local_channel_id_, processing_data.source_channel_, processing_data.message_);
                break;
            case aergo::module::IModule::ProcessingType::REQUEST:
                module_->processRequest(processing_data.local_channel_id_, processing_data.source_channel_, processing_data.message_);
                break;
            case aergo::module::IModule::ProcessingType::RESPONSE:
                module_->processResponse(processing_data.local_channel_id_, processing_data.source_channel_, processing_data.message_);
                break;
        }
        lock.lock();
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

        ProcessingData processing_data;
        if (!popPrioritizedProcessingData(processing_data))
        {
            continue;
        }

        lock.unlock();
        switch (processing_data.processing_type_)
        {
            case aergo::module::IModule::ProcessingType::MESSAGE:
                module_->processMessage(processing_data.local_channel_id_, processing_data.source_channel_, processing_data.message_);
                break;
            case aergo::module::IModule::ProcessingType::REQUEST:
                module_->processRequest(processing_data.local_channel_id_, processing_data.source_channel_, processing_data.message_);
                break;
            case aergo::module::IModule::ProcessingType::RESPONSE:
                module_->processResponse(processing_data.local_channel_id_, processing_data.source_channel_, processing_data.message_);
                break;
        }
        lock.lock();
    }
    --prioritized_worker_running_count_;
}



bool DllModuleWrapper::regularQueuesEmpty()
{
    for (const auto& queue : regular_queues_)
    {
        if (!queue.empty())
        {
            return false;
        }
    }

    return true;
}



bool DllModuleWrapper::prioritizedQueuesEmpty()
{
    for (const auto& queue : prioritized_queues_)
    {
        if (!queue.empty())
        {
            return false;
        }
    }

    return true;
}



bool DllModuleWrapper::popRegularProcessingData(ProcessingData& data)
{
    for (uint32_t i = 0; i < regular_queues_.size(); ++i)
    {
        uint32_t idx = (next_regular_queue_idx_ + i) % regular_queues_.size();
        if (!regular_queues_[idx].empty())
        {
            data = std::move(regular_queues_[idx].front());
            regular_queues_[idx].pop();
            next_regular_queue_idx_ = (idx + 1) % regular_queues_.size();
            return true;
        }
    }

    return false;
}



bool DllModuleWrapper::popPrioritizedProcessingData(ProcessingData& data)
{
    for (uint32_t i = 0; i < prioritized_queues_.size(); ++i)
    {
        uint32_t idx = (next_prioritized_queue_idx_ + i) % prioritized_queues_.size();
        if (!prioritized_queues_[idx].empty())
        {
            data = std::move(prioritized_queues_[idx].front());
            prioritized_queues_[idx].pop();
            next_prioritized_queue_idx_ = (idx + 1) % prioritized_queues_.size();
            return true;
        }
    }

    return false;
}



aergo::module::IModule* DllModuleWrapper::getModule()
{
    return module_.get();
}



int64_t DllModuleWrapper::nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}