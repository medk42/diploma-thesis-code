#include "module_helpers/activation_wrapper/activation_wrapper.h"
#include "module_helpers/activation_wrapper/message_types.h"


#include <nlohmann/json.hpp>


#include <unordered_map>
#include <cstring>



using namespace aergo::module::helpers::activation_wrapper;
using namespace aergo::module::helpers::parameter_description;
using namespace aergo::module;
using json = nlohmann::json;


ActivationWrapper::ActivationWrapper(std::unique_ptr<aergo::module::IModule> module, ParameterList* parameters_)
: valid_(false), module_ref_(std::move(module)), parameters_(parameters_), activated_(false), message_wait_{false, 0, 0}
{
    if (module_ref_.get() == nullptr || parameters_ == nullptr)
    {
        return;
    }

    base_module_ref_ = module_ref_->query<aergo::module::BaseModule>();
    activable_module_ref_ = module_ref_->query<IActivableModule>();
    if (base_module_ref_ == nullptr)
    {
        return;
    }
    if (activable_module_ref_ == nullptr)
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Wrapped module does not implement required interfaces.");
        return;
    }

    dynamic_allocator_ = base_module_ref_->createDynamicAllocator();
    if (dynamic_allocator_ == nullptr)
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Failed to create dynamic allocator.");
        return;
    }
    
    const ModuleInfo* module_info = base_module_ref_->getModuleInfo();
    bool found = false;
    for (uint32_t response_producer_id = 0; response_producer_id < module_info->response_producer_count_; ++response_producer_id)
    {
        if (module_info->response_producers_[response_producer_id].channel_type_identifier_ == nullptr)
        {
            continue;
        }
        
        std::string channel_type_identifier = module_info->response_producers_[response_producer_id].channel_type_identifier_;

        if (channel_type_identifier == aergo::module::helpers::activation_wrapper::message_types::activation_response_producer.channel_type_identifier_)
        {
            expected_response_producer_id_ = response_producer_id;
            found = true;
            break;
        }
    }

    if (!found)
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Wrapped module does not have required response channel.");
        return;
    }

    if (!initializeDefaultParameters())
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Failed to initialize default parameters.");
        return;
    }

    valid_ = true;
}



void ActivationWrapper::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (message_wait_.expected_.load(std::memory_order_relaxed)) // quick check to filter out unnecessary locking
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (message_wait_.expected_.load(std::memory_order_acquire)) // double check after locking
        {
            auto& param = parameters_->getParameters()[message_wait_.param_id_];
            if (param.custom_channel_type_ == CustomChannelType::SUBSCRIBE && param.custom_channel_id_ == subscribe_consumer_id)
            {
                setCustomValueOnReceive(IActivableModule::ProcessingChannelType::MESSAGE, subscribe_consumer_id, source_channel, message);
                return;
            }
        }
    }
    
    if (activated_.load(std::memory_order_relaxed))
    {
        module_ref_->processMessage(subscribe_consumer_id, source_channel, message);
    }
}



aergo::module::ResponseData ActivationWrapper::processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (response_producer_id == expected_response_producer_id_)
    {
        if (message.data_len_ != sizeof(message_types::Request))
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid message size.");
            return { .success_ = false };
        }

        auto* request = reinterpret_cast<message_types::Request*>(message.data_);

        if (request->request_type_ == message_types::ReqType::SET_VALUE)
        {
            if (message.blob_count_ != 1 || message.blobs_ == nullptr || !message.blobs_[0].valid())
            {
                base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid blob count.");
                return { .success_ = false };
            }
        }
        else if (message.blob_count_ != 0)
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid blob count.");
            return { .success_ = false };
        }

        
        auto [response, extra_data] = processActivationRequest(*request, message.blobs_); // process request, locked inside

        return aergo::module::ResponseData{ 
            .success_ = true, 
            .data_ = std::vector<uint8_t>((uint8_t*)(&response), (uint8_t*)(&response) + sizeof(response)), 
            .blobs_ = extra_data.valid() ? std::vector<message::SharedDataBlob>{extra_data} : std::vector<message::SharedDataBlob>{} 
        };
    }
    else if (activated_.load(std::memory_order_relaxed))
    {
        return module_ref_->processRequest(response_producer_id, source_channel, message);
    }

    return { .success_ = false };
}



void ActivationWrapper::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (message_wait_.expected_.load(std::memory_order_relaxed)) // quick check to filter out unnecessary locking
    {
        std::lock_guard<std::mutex> lock(mutex_); // accessing message_wait_, we need synchronization
        if (message_wait_.expected_.load(std::memory_order_acquire)) // double check after locking
        {
            auto& param = parameters_->getParameters()[message_wait_.param_id_];
            if (param.custom_channel_type_ == CustomChannelType::REQUEST && param.custom_channel_id_ == request_consumer_id && message.id_ == message_wait_.request_id_)
            {
                setCustomValueOnReceive(IActivableModule::ProcessingChannelType::RESPONSE, request_consumer_id, source_channel, message);
                return;
            }
        }
    }

    if (activated_.load(std::memory_order_relaxed))
    {
        module_ref_->processResponse(request_consumer_id, source_channel, message);
    }
}



bool ActivationWrapper::valid() noexcept
{
    if (module_ref_ == nullptr || module_ref_->valid() == false) // no need to synchronize, module_ref_/valid is not changed after initialization
    {
        return false;
    }

    return valid_;
}



void* ActivationWrapper::query_capability(const std::type_info& id) noexcept
{
    return module_ref_->query_capability(id); // no need to synchronize, module_ref_ is not changed after initialization
}



aergo::module::IModule::IngressDecision ActivationWrapper::onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
{
    if (kind == aergo::module::IModule::ProcessingType::REQUEST && local_channel_id == expected_response_producer_id_)
    {
        return aergo::module::IModule::IngressDecision::ACCEPT;
    }

    if (activated_.load(std::memory_order_relaxed))
    {
        return module_ref_->onIngress(kind, local_channel_id, src, msg, queue_status);
    }

    if (message_wait_.expected_.load(std::memory_order_relaxed)) { // quick check to filter out unnecessary locking
        std::lock_guard<std::mutex> lock(mutex_);  // accessing message_wait_, we need synchronization

        if (message_wait_.expected_.load(std::memory_order_acquire))
        {
            auto& param = parameters_->getParameters()[message_wait_.param_id_];
            if ((kind == aergo::module::IModule::ProcessingType::MESSAGE && param.custom_channel_type_ == CustomChannelType::SUBSCRIBE && param.custom_channel_id_ == local_channel_id) ||
                (kind == aergo::module::IModule::ProcessingType::RESPONSE && param.custom_channel_type_ == CustomChannelType::REQUEST && param.custom_channel_id_ == local_channel_id && msg.id_ == message_wait_.request_id_))
            {
                return aergo::module::IModule::IngressDecision::ACCEPT;
            }
        }
    }

    return aergo::module::IModule::IngressDecision::DROP;
}



bool ActivationWrapper::threadStart(uint32_t timeout_ms) noexcept
{
    return module_ref_->threadStart(timeout_ms);
}



bool ActivationWrapper::threadStop(uint32_t timeout_ms) noexcept
{
    return module_ref_->threadStop(timeout_ms);
}



bool ActivationWrapper::initializeDefaultParameters()
{
    if (parameters_ == nullptr)
    {
        return false;
    }

    auto& params = parameters_->getParameters();
    parameter_values_.resize(params.size());

    for (size_t i = 0; i < params.size(); ++i)
    {
        auto& param = params[i];
        uint16_t list_size = param.as_list_ ? param.list_size_min_ : 1;
        parameter_values_[i].resize(list_size); // CUSTOM will only be resized, not set to anything
        for (uint16_t j = 0; j < list_size; ++j)
        {
            ParameterValueOpt default_value = string_conversions::parseDefaultValue(param);
            switch (param.type_)
            {
                case ParameterType::BOOL:
                {
                    if (default_value)
                    {
                        bool bvalue = std::get<bool>(*default_value);
                        parameter_values_[i][j].resize(1);
                        parameter_values_[i][j][0] = bvalue ? 1 : 0;
                        break;
                    }
                    else
                    {
                        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid default BOOL parameter value.");
                        return false;
                    }
                    break;
                }
                case ParameterType::LONG:
                {
                    if (!default_value)
                    {
                        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid default LONG parameter value.");
                        return false;
                    }
                    int64_t value = std::get<int64_t>(*default_value);

                    parameter_values_[i][j].resize(sizeof(int64_t));
                    memcpy(&parameter_values_[i][j][0], &value, sizeof(int64_t));
                    break;
                }
                case ParameterType::DOUBLE:
                {
                    if (!default_value)
                    {
                        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid default DOUBLE parameter value.");
                        return false;
                    }
                    double dvalue = std::get<double>(*default_value);

                    parameter_values_[i][j].resize(sizeof(double));
                    memcpy(&parameter_values_[i][j][0], &dvalue, sizeof(double));
                    break;
                }
                case ParameterType::STRING:
                {
                    if (!default_value)
                    {
                        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid default STRING parameter value.");
                        return false;
                    }
                    const std::string& strvalue = std::get<std::string>(*default_value);
                    parameter_values_[i][j].resize(strvalue.size());
                    memcpy(&parameter_values_[i][j][0], strvalue.data(), strvalue.size());
                    break;
                }
                    
                case ParameterType::ENUM:
                {
                    if (!default_value)
                    {
                        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid default ENUM parameter value.");
                        return false;
                    }

                    size_t enum_id = static_cast<size_t>(std::get<int32_t>(*default_value));
                    parameter_values_[i][j].resize(sizeof(size_t));
                    memcpy(&parameter_values_[i][j][0], &enum_id, sizeof(size_t));

                    break;
                }
            }
        }
    }

    return true;
}



std::tuple<message_types::Response, message::SharedDataBlob> ActivationWrapper::processActivationRequest(message_types::Request& request, message::SharedDataBlob* blob)
{
    std::lock_guard<std::mutex> lock(mutex_);

    handleActivationTask();

    message_types::Response response;
    response.request_type_ = request.request_type_;
    response.activated_ = activated_.load(std::memory_order_acquire);

    bool parameter_change_forbidden = response.activated_ || message_wait_.expected_.load(std::memory_order_acquire) || activation_task_.get() != nullptr; // do not allow changing parameters while activated or waiting for CUSTOM message

    switch (request.request_type_)
    {
        case message_types::ReqType::READ_ACTIVATION_PARAMETERS: // any time
        {
            std::string activation_params = parameters_->toString();
            message::SharedDataBlob data_blob = dynamic_allocator_->allocate(activation_params.size());
            if (!data_blob.valid() || data_blob.size() != activation_params.size())
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            memcpy(data_blob.data(), activation_params.data(), activation_params.size());
            response.result_ = message_types::Result::SUCCESS;
            return {response, data_blob};
        }
        case message_types::ReqType::GET_STATUS: // any time
        {
            response.result_ = (message_wait_.expected_.load(std::memory_order_relaxed) || activation_task_.get() != nullptr) ? message_types::Result::RUNNING : message_types::Result::SUCCESS;
            if (activation_task_.get() != nullptr)
            {
                response.progress_ = activable_module_ref_->getActivationProgress();
            }
            return {response, {}};
        }
        case message_types::ReqType::ACTIVATE: // only when not activated, in process of de/activating and not waiting for CUSTOM message
        {
            if (response.activated_ || message_wait_.expected_.load(std::memory_order_relaxed) || activation_task_.get() != nullptr)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            if (areParametersValid() == false)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            activation_task_ = std::make_unique<async_helpers::AsyncTask<bool>>([this](const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) { return activable_module_ref_->activate(parameter_values_, cancel_flag, cancelled); });
            activation_task_->start();
            response.result_ = message_types::Result::RUNNING;
            response.progress_ = activable_module_ref_->getActivationProgress();
            return {response, {}};
        }
        case message_types::ReqType::DEACTIVATE: // only when activated, in process of de/activating and not waiting for CUSTOM message
        {
            if (!response.activated_ || message_wait_.expected_.load(std::memory_order_relaxed) || activation_task_.get() != nullptr)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            activation_task_ = std::make_unique<async_helpers::AsyncTask<bool>>([this](const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) { return activable_module_ref_->deactivate(cancel_flag, cancelled); });
            activation_task_->start();
            response.result_ = message_types::Result::RUNNING;
            response.progress_ = activable_module_ref_->getActivationProgress();
            return {response, {}};
        }
        case message_types::ReqType::SET_VALUE: // only when not activated, not in process of de/activating and not waiting for CUSTOM message
        {
            if (parameter_change_forbidden)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }
            return setValue(request, blob, response);
        }
        case message_types::ReqType::READ_VALUES: // any time
        {
            return readValues(request, blob, response);
        }
        case message_types::ReqType::LIST_ADD: // only when not activated, not in process of de/activating and not waiting for CUSTOM message
        {
            if (parameter_change_forbidden)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }
            return listAdd(request, blob, response);
        }
        case message_types::ReqType::LIST_REMOVE: // only when not activated, not in process of de/activating and not waiting for CUSTOM message
        {
            if (parameter_change_forbidden)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }
            return listRemove(request, blob, response);
        }
        case message_types::ReqType::CANCEL_TASK: // when task is running, i.e. waiting for CUSTOM message or in the process of activating/deactivating
        {
            if (activation_task_.get() != nullptr)
            {
                activation_task_->cancel();
                response.result_ = message_types::Result::FAIL;
            }
            else if (message_wait_.expected_.load(std::memory_order_relaxed))
            {
                message_wait_.expected_.store(false, std::memory_order_release);
                response.result_ = message_types::Result::SUCCESS;
            }
            else
            {
                response.result_ = message_types::Result::FAIL;
            }
            return {response, {}};
        }
        default:
        {
            response.result_ = message_types::Result::FAIL;
            return {response, {}};
        }
    }
}



std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> ActivationWrapper::setValue(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response)
{
    if (request.param_id_ >= parameters_->getParameters().size()) // invalid parameter ID
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }

    auto& param = parameters_->getParameters()[request.param_id_];
    auto& param_value = parameter_values_[request.param_id_];
    if (param.type_ != request.parameter_type_) // parameter type mismatch
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }
    
    size_t list_id = request.list_id_;
    if (param.as_list_)
    {
        if (list_id >= param_value.size()) // out of range, use LIST_ADD to extend the list first
        {
            response.result_ = message_types::Result::FAIL;
            return {response, {}};
        }
    }
    else
    {
        list_id = 0;
    }

    auto& chosen_value = param_value[list_id];

    switch (param.type_)
    {
        case ParameterType::BOOL:
        {
            if (blob->size() != 1 || (blob->data()[0] != 0 && blob->data()[0] != 1))
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            chosen_value.resize(1);
            chosen_value[0] = blob->data()[0];
            response.result_ = message_types::Result::SUCCESS;
            return {response, {}};
        }
        case ParameterType::LONG:
        {
            if (blob->size() != sizeof(int64_t))
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            int64_t value;
            memcpy(&value, blob->data(), sizeof(int64_t));

            if (param.limit_min_ && value < (int64_t)param.min_value_long_)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }
            if (param.limit_max_ && value > (int64_t)param.max_value_long_)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            chosen_value.resize(sizeof(int64_t));
            memcpy(&chosen_value[0], &value, sizeof(int64_t));
            response.result_ = message_types::Result::SUCCESS;
            return {response, {}};
        }
        case ParameterType::DOUBLE:
        {
            if (blob->size() != sizeof(double))
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            double value;
            memcpy(&value, blob->data(), sizeof(double));

            if (param.limit_min_ && value < param.min_value_double_)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }
            if (param.limit_max_ && value > param.max_value_double_)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            chosen_value.resize(sizeof(double));
            memcpy(&chosen_value[0], &value, sizeof(double));
            response.result_ = message_types::Result::SUCCESS;
            return {response, {}};
        }
        case ParameterType::STRING:
        {
            chosen_value.resize(blob->size());
            memcpy(&chosen_value[0], blob->data(), blob->size());
            response.result_ = message_types::Result::SUCCESS;
            return {response, {}};
        }
        case ParameterType::ENUM:
        {
            if (blob->size() != sizeof(size_t))
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            size_t enum_id;
            memcpy(&enum_id, blob->data(), sizeof(size_t));
            if (enum_id >= param.enum_values_.size())
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            chosen_value.resize(sizeof(size_t));
            memcpy(&chosen_value[0], &enum_id, sizeof(size_t));
            response.result_ = message_types::Result::SUCCESS;
            return {response, {}};
        }
        case ParameterType::CUSTOM:
        {
            if (blob->size() != 1 || (blob->data()[0] != 0 && blob->data()[0] != 1))
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }
            bool custom_set_requested = blob->data()[0] == 1;

            if (custom_set_requested)
            {
                if (param.custom_channel_type_ == CustomChannelType::REQUEST)
                {
                    bool success = activable_module_ref_->sendRequestFromActivation(parameters_->getParameters(), request.param_id_, message_wait_.request_id_);
                    if (!success)
                    {
                        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Failed to send CUSTOM parameter request.");
                        response.result_ = message_types::Result::FAIL;
                        return {response, {}};
                    }
                }

                message_wait_.param_id_ = request.param_id_;
                message_wait_.list_id_ = list_id;
                message_wait_.expected_.store(true, std::memory_order_release);

                response.result_ = message_types::Result::RUNNING;
                return {response, {}};
            }
            else
            {
                chosen_value.clear();

                response.result_ = message_types::Result::SUCCESS;
                return {response, {}};
            }
            
            break;
        }
        default:
        {
            response.result_ = message_types::Result::FAIL;
            return {response, {}};
        }
    }
}



std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> ActivationWrapper::readValues(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response)
{

    std::vector<uint8_t> all_parameters_data;
    
    size_t param_count = parameter_values_.size();
        all_parameters_data.insert(all_parameters_data.end(),
            reinterpret_cast<const uint8_t*>(&param_count),
            reinterpret_cast<const uint8_t*>(&param_count) + sizeof(size_t));

    const auto& params = parameters_->getParameters();
    for (size_t param_idx = 0; param_idx < parameter_values_.size(); ++param_idx) {
        const auto& param_list = parameter_values_[param_idx];
        // Store the number of items in the list
        size_t list_size = param_list.size();
        all_parameters_data.insert(all_parameters_data.end(),
            reinterpret_cast<const uint8_t*>(&list_size),
            reinterpret_cast<const uint8_t*>(&list_size) + sizeof(size_t));

        for (const auto& item : param_list) {
            size_t item_size = item.size();
            // If parameter is CUSTOM, write 0 and skip data
            if (params[param_idx].type_ == ParameterType::CUSTOM) {
                size_t custom_item_size = 1;
                all_parameters_data.insert(all_parameters_data.end(),
                    reinterpret_cast<const uint8_t*>(&custom_item_size),
                    reinterpret_cast<const uint8_t*>(&custom_item_size) + sizeof(size_t));
                all_parameters_data.push_back(item_size > 0 ? 1 : 0); // 1 if data is present, 0 if empty
            } else {
                all_parameters_data.insert(all_parameters_data.end(),
                    reinterpret_cast<const uint8_t*>(&item_size),
                    reinterpret_cast<const uint8_t*>(&item_size) + sizeof(size_t));
                all_parameters_data.insert(all_parameters_data.end(), item.begin(), item.end());
            }
        }
    }

    message::SharedDataBlob all_parameters_blob = dynamic_allocator_->allocate(all_parameters_data.size());
    if (!all_parameters_blob.valid() || all_parameters_blob.size() != all_parameters_data.size())
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }
    memcpy(all_parameters_blob.data(), all_parameters_data.data(), all_parameters_data.size());

    response.result_ = message_types::Result::SUCCESS;
    return {response, all_parameters_blob};
}



std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> ActivationWrapper::listAdd(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response)
{
    if (request.param_id_ >= parameters_->getParameters().size()) // invalid parameter ID
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }

    auto& param = parameters_->getParameters()[request.param_id_];
    auto& param_value = parameter_values_[request.param_id_];
    if (param.type_ != request.parameter_type_) // parameter type mismatch
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }
    
    if (!param.as_list_) // not a list
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }

    if (param.list_size_max_ != 0 && param_value.size() >= static_cast<size_t>(param.list_size_max_))
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }

    param_value.emplace_back(); // add new empty item to the list
    response.result_ = message_types::Result::SUCCESS;
    return {response, {}};
}



std::tuple<message_types::Response, aergo::module::message::SharedDataBlob> ActivationWrapper::listRemove(message_types::Request& request, message::SharedDataBlob* blob, message_types::Response response)
{
    if (request.param_id_ >= parameters_->getParameters().size()) // invalid parameter ID
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }

    auto& param = parameters_->getParameters()[request.param_id_];
    auto& param_value = parameter_values_[request.param_id_];
    if (param.type_ != request.parameter_type_) // parameter type mismatch
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }
    
    if (!param.as_list_) // not a list
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }

    if (request.list_id_ >= param_value.size())
    {
        response.result_ = message_types::Result::FAIL;
        return {response, {}};
    }

    param_value.erase(param_value.begin() + request.list_id_);

    response.result_ = message_types::Result::SUCCESS;
    return {response, {}};
}



void ActivationWrapper::handleActivationTask()
{
    if (activation_task_.get() != nullptr)
    {
        auto state = activation_task_->getState();
        if (state == async_helpers::AsyncTaskState::COMPLETED || state == async_helpers::AsyncTaskState::CANCELLED)
        {
            if (state == async_helpers::AsyncTaskState::COMPLETED && activation_task_->getResult().value()) // COMPLETED = task finished running; getResult() = task returned true, successfully performed activation/deactivation
            {
                bool activated = activated_.load(std::memory_order_relaxed);
                activated_.store(!activated, std::memory_order_release); // currently activated_ is false when activating, true when deactivating
            }
            activation_task_.reset();
        };
    }
}



void ActivationWrapper::setCustomValueOnReceive(
    IActivableModule::ProcessingChannelType channel_type, 
    uint32_t subscribe_consumer_id, 
    ChannelIdentifier source_channel, 
    message::MessageHeader message
)
{
    
    auto& chosen_value = parameter_values_[message_wait_.param_id_][message_wait_.list_id_];

    std::vector<uint8_t> out;

    IActivableModule::ProcessingResult result = activable_module_ref_->processCustomMessageOrResponse(
        channel_type,
        subscribe_consumer_id,
        source_channel,
        message,
        out
    );

    if (result == IActivableModule::ProcessingResult::DROP)
    {
        if (channel_type == IActivableModule::ProcessingChannelType::MESSAGE)
        {
            return; // do not change chosen_value, just exit and wait for another message
        }
        else
        {
            result = IActivableModule::ProcessingResult::ACCEPT; // for responses, we cannot wait for another response, so treat DROP as ACCEPT
        }
    }
    
    if (result == IActivableModule::ProcessingResult::ACCEPT)
    {
        out.clear();

        // helper to append POD
        auto append_pod = [&](const auto& v) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
            out.insert(out.end(), p, p + sizeof(v));
        };

        uint64_t message_data_len = (message.data_ == nullptr) ? 0 : message.data_len_;

        // data_len_
        append_pod(message_data_len);

        // inline data
        if (message_data_len > 0) {
            out.insert(out.end(), message.data_, message.data_ + message_data_len);
        }

        // blob_count_
        append_pod(message.blob_count_);

        for (uint64_t blob_id = 0; blob_id < message.blob_count_; ++blob_id)
        {
            if (message.blobs_[blob_id].valid())
            {
                uint64_t blob_size = message.blobs_[blob_id].size();
                uint8_t* blob_data = message.blobs_[blob_id].data();

                if (blob_data == nullptr)
                {
                    blob_size = 0;
                }

                append_pod(blob_size);
                if (blob_size > 0)
                {
                    out.insert(out.end(), blob_data, blob_data + blob_size);
                }
            }
            else
            {
                uint64_t blob_size = 0;
                append_pod(blob_size);
            }
        }
    }
    // else only ACCEPT_REPLACE remains, but that already pushed its data to out

    chosen_value = std::move(out);

    message_wait_.expected_.store(false, std::memory_order_release);
}



bool ActivationWrapper::areParametersValid()
{
    auto& params = parameters_->getParameters();

    if (params.size() != parameter_values_.size())
    {
        return false;
    }

    for (size_t i = 0; i < params.size(); ++i)
    {
        auto& param = params[i];
        auto& param_value = parameter_values_[i];

        if (param.as_list_)
        {
            if (param_value.size() < static_cast<size_t>(param.list_size_min_) || (param.list_size_max_ != 0 && param_value.size() > static_cast<size_t>(param.list_size_max_))) 
                return false;
        }
        else
        {
            if (param_value.size() != 1)
                return false;
        }

        for (const auto& item : param_value)
        {
            switch (param.type_)
            {
                case ParameterType::BOOL:
                {
                    if (item.size() != 1 || (item[0] != 0 && item[0] != 1))
                    {
                        return false;
                    }
                    break;
                }
                case ParameterType::LONG:
                {
                    if (item.size() != sizeof(int64_t))
                    {
                        return false;
                    }

                    int64_t value;
                    memcpy(&value, item.data(), sizeof(int64_t));

                    if (param.limit_min_ && value < (int64_t)param.min_value_long_)
                    {
                        return false;
                    }
                    if (param.limit_max_ && value > (int64_t)param.max_value_long_)
                    {
                        return false;
                    }
                    break;
                }
                case ParameterType::DOUBLE:
                {
                    if (item.size() != sizeof(double))
                    {
                        return false;
                    }

                    double value;
                    memcpy(&value, item.data(), sizeof(double));

                    if (param.limit_min_ && value < param.min_value_double_)
                    {
                        return false;
                    }
                    if (param.limit_max_ && value > param.max_value_double_)
                    {
                        return false;
                    }
                    break;
                }
                case ParameterType::STRING:
                {
                    // any size is valid
                    break;
                }
                case ParameterType::ENUM:
                {
                    if (item.size() != sizeof(size_t))
                    {
                        return false;
                    }

                    size_t enum_id;
                    memcpy(&enum_id, item.data(), sizeof(size_t));
                    if (enum_id >= param.enum_values_.size())
                    {
                        return false;
                    }
                    break;
                }
                case ParameterType::CUSTOM:
                {
                    // any size is valid
                    break;
                }
                default:
                {
                    return false;
                }
            }
        }
    }

    return true;
}
            


ISerializableModule::SaveData ActivationWrapper::save() noexcept
{
    aergo::module::ISerializableModule::SaveData inner_save_data(std::move(module_ref_->save()));
    
    if (!inner_save_data.success_)
    {
        return { .success_ = false }; // inner module failed to save
    }

    aergo::module::ISerializableModule::SaveData save_data;
    save_data.success_ = true;
    save_data.supports_saving_ = true;
    save_data.schema_version_ = SCHEMA_VERSION; // version 1 = ActivationWrapper save data

    json module_data;
    module_data["inner_module"]["supports_saving"] = inner_save_data.supports_saving_;
    if (inner_save_data.supports_saving_)
    {
        module_data["inner_module"]["schema_version"] = inner_save_data.schema_version_;
        
        
        try 
        {
            module_data["inner_module"]["json_header"] = json::parse(inner_save_data.json_header_);
        }
        catch (...)
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Failed to parse inner module JSON save data.");
            return { .success_ = false };            
        }

        module_data["inner_module"]["blobs_count"] = inner_save_data.blobs_.size();
    }

    for (auto& blob : inner_save_data.blobs_)
    {
        save_data.blobs_.push_back(ISerializableModule::SavedBlob {
            .name_ = "inner_" + blob.name_,
            .data_ = std::move(blob.data_),
        });
    }

    module_data["parameter_values"] = json::array();

    const auto& parameters = parameters_->getParameters();
    for (size_t i = 0; i < parameter_values_.size(); ++i)
    {
        json param_data;
        const auto& param_type = parameters[i];
        const auto& param_value = parameter_values_[i];
        
        param_data["type"] = static_cast<int>(param_type.type_);
        param_data["values"] = json::array();
        for (size_t j = 0; j < param_value.size(); ++j)
        {
            const auto& single_param_value = param_value[j];
            if (param_type.type_ != ParameterType::CUSTOM)
            {
                param_data["values"].push_back(std::vector<uint8_t>(single_param_value));
            }
            else
            {
                std::string custom_file_name = "custom_" + std::to_string(i) + "_" + std::to_string(j) + ".bin";
                param_data["values"].push_back(custom_file_name);
                save_data.blobs_.push_back(ISerializableModule::SavedBlob {
                    .name_ = custom_file_name,
                    .data_ = single_param_value,
                });
            }
        }
        
        
        module_data["parameter_values"].push_back(param_data);
    }

    save_data.json_header_ = module_data.dump();

    return save_data;
}



bool ActivationWrapper::load(ISerializableModule::SaveData data) noexcept
{
    if (data.schema_version_ != SCHEMA_VERSION || !data.supports_saving_)
    {
        return false; // unsupported schema version or does not support saving
    }

    json json_data;
    
    try
    {
        json_data = json::parse(data.json_header_);
    }
    catch (...)
    {
        return false; // invalid JSON
    }
    
    if (json_data.contains("parameter_values") == false || !json_data["parameter_values"].is_array()
    || json_data["parameter_values"].size() != parameters_->getParameters().size() || json_data.contains("inner_module") == false || !json_data["inner_module"].is_object())
    {
        return false; // missing or invalid parameter values
    }

    auto& parameter_values_data = json_data["parameter_values"];
    parameter_values_.clear();
    parameter_values_.resize(parameter_values_data.size());
    for (size_t i = 0; i < parameter_values_data.size(); ++i)
    {
        auto& param_data = parameter_values_data[i];

        if (param_data.contains("type") == false || !param_data["type"].is_number_integer()
        || param_data.contains("values") == false || !param_data["values"].is_array())
        {
            return false; // missing or invalid parameter data
        }

        int type_int = param_data["type"].get<int>();
        if (type_int < 0 || type_int > static_cast<int>(ParameterType::CUSTOM))
        {
            return false; // invalid parameter type
        }
        ParameterType type = static_cast<ParameterType>(type_int);

        auto& values_data = param_data["values"];

        parameter_values_[i].clear();
        parameter_values_[i].resize(values_data.size());

        for (size_t j = 0; j < values_data.size(); ++j)
        {
            auto& single_value_data = values_data[j];
            if ((type != ParameterType::CUSTOM && !single_value_data.is_array())
            || (type == ParameterType::CUSTOM && !single_value_data.is_string()))
            {
                return false; // invalid non-CUSTOM value
            }

            if (type != ParameterType::CUSTOM)
            {
                
                parameter_values_[i][j] = single_value_data.get<std::vector<uint8_t>>();
            }
            else
            {
                std::string custom_file_name = single_value_data.get<std::string>();
                
                auto it = std::find_if(data.blobs_.begin(), data.blobs_.end(), [&custom_file_name](const ISerializableModule::SavedBlob& blob) { return blob.name_ == custom_file_name; });
                if (it == data.blobs_.end())
                {
                    return false; // missing CUSTOM blob
                }
                parameter_values_[i][j] = std::move(it->data_);
            }
        }
    }

    ISerializableModule::SaveData inner_data;

    auto& inner_json = json_data["inner_module"];
    if (inner_json.contains("supports_saving") == false || !inner_json["supports_saving"].is_boolean())
    {
        return false; // missing or invalid inner module saving support
    }

    inner_data.supports_saving_ = inner_json["supports_saving"].get<bool>();

    if (inner_data.supports_saving_)
    {
        if (inner_json.contains("schema_version") == false || !inner_json["schema_version"].is_number_integer()
        || inner_json.contains("json_header") == false || !inner_json["json_header"].is_object()
        || inner_json.contains("blobs_count") == false || !inner_json["blobs_count"].is_number_integer())
        {
            return false; // missing or invalid inner module save data
        }

        inner_data.schema_version_ = inner_json["schema_version"].get<uint32_t>();
        inner_data.json_header_ = inner_json["json_header"].dump();
        size_t blobs_count = inner_json["blobs_count"].get<size_t>();

        for (const auto& blob : data.blobs_)
        {
            if (blob.name_.rfind("inner_", 0) == 0) // starts with "inner_"
            {
                inner_data.blobs_.push_back(ISerializableModule::SavedBlob {
                    .name_ = blob.name_.substr(6), // remove "inner_" prefix
                    .data_ = std::move(blob.data_),
                });
            }
        }
    }

    bool result = module_ref_->load(std::move(inner_data));
    activated_ = activable_module_ref_->isActivated();

    return result;
}



const ModuleInfo* ActivationWrapper::getModuleInfo() const noexcept
{
    return module_ref_->getModuleInfo();
}