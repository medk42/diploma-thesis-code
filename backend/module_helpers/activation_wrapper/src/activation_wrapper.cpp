#include "module_helpers/activation_wrapper/activation_wrapper.h"
#include "module_helpers/activation_wrapper/message_types.h"



#include <unordered_map>



using namespace aergo::module::helpers::activation_wrapper;
using namespace aergo::module;


ActivationWrapper::ActivationWrapper(aergo::module::IModule* module, aergo::module::ModuleInfo module_info, params::ParameterList* parameters_)
: valid_(false), module_ref_(module), parameters_(parameters_), activated_(false), message_wait_{false, 0, 0}
{
    if (module_ref_ == nullptr || parameters_ == nullptr)
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid constructor parameters.");
        return;
    }

    base_module_ref_ = module_ref_->query<aergo::module::BaseModule>();
    activable_module_ref_ = module_ref_->query<IActivableModule>();
    if (activable_module_ref_ == nullptr || base_module_ref_ == nullptr)
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
    
    bool found = false;
    for (uint32_t response_producer_id = 0; response_producer_id < module_info.response_producer_count_; ++response_producer_id)
    {
        if (module_info.response_producers_[response_producer_id].channel_type_identifier_ == nullptr)
        {
            continue;
        }
        
        std::string channel_type_identifier = module_info.response_producers_[response_producer_id].channel_type_identifier_;

        if (channel_type_identifier == aergo::module::helpers::activation_wrapper::message_types::resp_type_identifier)
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
    if (activated_)
    {
        module_ref_->processMessage(subscribe_consumer_id, source_channel, message);
    }
    else if (message_wait_.expected_)
    {
        auto& param = parameters_->getParameters()[message_wait_.param_id_];
        if (param.custom_channel_type_ == params::CustomChannelType::SUBSCRIBE && param.custom_channel_id_ == subscribe_consumer_id)
        {
            setCustomValueOnReceive(message);
        }
    }
}



void ActivationWrapper::processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (activated_ && response_producer_id != expected_response_producer_id_)
    {
        module_ref_->processRequest(response_producer_id, source_channel, message);
    }
    else if (response_producer_id == expected_response_producer_id_)
    {
        if (message.data_len_ != sizeof(message_types::Request))
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid message size.");
            return;
        }

        auto* request = reinterpret_cast<message_types::Request*>(message.data_);

        if (request->request_type_ == message_types::ReqType::SET_VALUE && request->parameter_type_ != params::ParameterType::CUSTOM)
        {
            if (message.blob_count_ != 1 || message.blobs_ == nullptr || !message.blobs_[0].valid())
            {
                base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid blob count.");
                return;
            }
        }
        else if (message.blob_count_ != 0)
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid blob count.");
            return;
        }

        
        auto [response, extra_data] = processActivationRequest(*request, message.blobs_);

        aergo::module::message::MessageHeader resp_message {
            .data_ = (uint8_t*)(&response),
            .data_len_ = sizeof(response),
            .blobs_ = extra_data.valid() ? &extra_data : nullptr,
            .blob_count_ = extra_data.valid() ? 1u : 0u,
            .success_ = true
        };
        base_module_ref_->sendResponse(response_producer_id, source_channel, message.id_, message);
    }
}



void ActivationWrapper::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (activated_)
    {
        module_ref_->processResponse(request_consumer_id, source_channel, message);
    }
    else if (message_wait_.expected_)
    {
        auto& param = parameters_->getParameters()[message_wait_.param_id_];
        if (param.custom_channel_type_ == params::CustomChannelType::REQUEST && param.custom_channel_id_ == request_consumer_id)
        {
            setCustomValueOnReceive(message);
        }
    }
}



bool ActivationWrapper::valid() noexcept
{
    if (module_ref_ == nullptr || module_ref_->valid() == false)
    {
        return false;
    }

    return valid_;
}



void* ActivationWrapper::query_capability(const std::type_info& id) noexcept
{
    return module_ref_->query_capability(id);
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
        if (!param.as_list_)
        {
            parameter_values_[i].resize(1); // CUSTOM will only be resized, not set to anything

            switch (param.type_)
            {
                case params::ParameterType::BOOL:
                {
                    if (param.default_value_ == "1")
                    {
                        parameter_values_[i][0].resize(1);
                        parameter_values_[i][0][0] = 1;
                    }
                    else if (param.default_value_ == "0" || param.default_value_.empty())
                    {
                        parameter_values_[i][0].resize(1);
                        parameter_values_[i][0][0] = 0;
                    }
                    else
                    {
                        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid default BOOL parameter value.");
                        return false;
                    }
                    break;
                }
                case params::ParameterType::LONG:
                {
                    int64_t value = 0;
                    if (!param.default_value_.empty())
                    {
                        try
                        {
                            value = std::stoll(param.default_value_);
                        }
                        catch (...)
                        {
                            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid default LONG parameter value.");
                            return false;
                        }
                    }
                    if (param.limit_min_ && value < param.min_value_long_)
                    {
                        value = param.min_value_long_;
                    }
                    if (param.limit_max_ && value > param.max_value_long_)
                    {
                        value = param.max_value_long_;
                    }

                    parameter_values_[i][0].resize(sizeof(int64_t));
                    memcpy(&parameter_values_[i][0][0], &value, sizeof(int64_t));
                    break;
                }
                case params::ParameterType::DOUBLE:
                {
                    double dvalue = 0;
                    if (!param.default_value_.empty())
                    {
                        try
                        {
                            dvalue = std::stod(param.default_value_);
                        }
                        catch (...)
                        {
                            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid default DOUBLE parameter value.");
                            return false;
                        }
                    }
                    if (param.limit_min_ && dvalue < param.min_value_double_)
                    {
                        dvalue = param.min_value_double_;
                    }
                    if (param.limit_max_ && dvalue > param.max_value_double_)
                    {
                        dvalue = param.max_value_double_;
                    }

                    parameter_values_[i][0].resize(sizeof(double));
                    memcpy(&parameter_values_[i][0][0], &dvalue, sizeof(double));
                    break;
                }
                case params::ParameterType::STRING:
                {
                    parameter_values_[i][0].resize(param.default_value_.size());
                    memcpy(&parameter_values_[i][0][0], param.default_value_.data(), param.default_value_.size());
                    break;
                }
                    
                case params::ParameterType::ENUM:
                {
                    size_t enum_id = 0;
                    bool found = false;
                    if (param.default_value_.empty() && param.enum_values_.size() > 0)
                    {
                        found = true;
                    }
                    else
                    {
                        for (; enum_id < param.enum_values_.size(); ++enum_id)
                        {
                            if (param.enum_values_[enum_id] == param.default_value_)
                            {
                                found = true;
                                break;
                            }
                        }
                    }
                    

                    if (found)
                    {
                        parameter_values_[i][0].resize(sizeof(size_t));
                        memcpy(&parameter_values_[i][0][0], &enum_id, sizeof(size_t));
                    }
                    else
                    {
                        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "ActivationWrapper: Invalid default ENUM parameter value.");
                        return false;
                    }

                    break;
                }
            }
        }
    }

    return true;
}



std::tuple<message_types::Response, message::SharedDataBlob> ActivationWrapper::processActivationRequest(message_types::Request& request, message::SharedDataBlob* blob)
{
    handleActivationTask();

    message_types::Response response;
    response.request_type_ = request.request_type_;
    response.activated_ = activated_;

    bool parameter_change_forbidden = activated_ || message_wait_.expected_ || activation_task_.get() != nullptr; // do not allow changing parameters while activated or waiting for CUSTOM message

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
            response.result_ = (message_wait_.expected_ || activation_task_.get() != nullptr) ? message_types::Result::RUNNING : message_types::Result::SUCCESS;
            if (activation_task_.get() != nullptr)
            {
                response.progress_ = activable_module_ref_->getActivationProgress();
            }
            return {response, {}};
        }
        case message_types::ReqType::ACTIVATE: // only when not activated, in process of de/activating and not waiting for CUSTOM message
        {
            if (activated_ || message_wait_.expected_ || activation_task_.get() != nullptr)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            activation_task_ = std::make_unique<AsyncTask>([this](std::atomic<bool>& cancel_flag) { return activable_module_ref_->activate(parameter_values_, cancel_flag); });
            activation_task_->start();
            response.result_ = message_types::Result::RUNNING;
            response.progress_ = activable_module_ref_->getActivationProgress();
            return {response, {}};
        }
        case message_types::ReqType::DEACTIVATE: // only when activated, in process of de/activating and not waiting for CUSTOM message
        {
            if (!activated_ || message_wait_.expected_ || activation_task_.get() != nullptr)
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }

            activation_task_ = std::make_unique<AsyncTask>([this](std::atomic<bool>& cancel_flag) { return activable_module_ref_->deactivate(cancel_flag); });
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
            else if (message_wait_.expected_)
            {
                message_wait_.expected_ = false;
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
        if (list_id >= param_value.size())
        {
            list_id = param_value.size();
            param_value.resize(list_id + 1);
        }
    }
    else
    {
        list_id = 0;
    }

    auto& chosen_value = param_value[list_id];

    if (param.type_ != params::ParameterType::CUSTOM)
    {
        switch (param.type_)
        {
            case params::ParameterType::BOOL:
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
            case params::ParameterType::LONG:
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
            case params::ParameterType::DOUBLE:
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
            case params::ParameterType::STRING:
            {
                chosen_value.resize(blob->size());
                memcpy(&chosen_value[0], blob->data(), blob->size());
                response.result_ = message_types::Result::SUCCESS;
                return {response, {}};
            }
            case params::ParameterType::ENUM:
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
            default:
            {
                response.result_ = message_types::Result::FAIL;
                return {response, {}};
            }
        }
    }
    else
    {
        message_wait_.expected_ = true;
        message_wait_.param_id_ = request.param_id_;
        message_wait_.list_id_ = list_id;

        if (param.custom_channel_type_ == params::CustomChannelType::REQUEST)
        {
            activable_module_ref_->sendRequestFromActivation(param.custom_channel_id_);
        }

        response.result_ = message_types::Result::RUNNING;
        return {response, {}};
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
            if (params[param_idx].type_ == params::ParameterType::CUSTOM) {
                item_size = 0;
                all_parameters_data.insert(all_parameters_data.end(),
                    reinterpret_cast<const uint8_t*>(&item_size),
                    reinterpret_cast<const uint8_t*>(&item_size) + sizeof(size_t));
                // Do not insert item data
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
        if (state == AsyncTaskState::COMPLETED || state == AsyncTaskState::CANCELLED)
        {
            activation_task_.reset();
        };
    }
}



void ActivationWrapper::setCustomValueOnReceive(message::MessageHeader message)
{
    
    auto& chosen_value = parameter_values_[message_wait_.param_id_][message_wait_.list_id_];
    chosen_value.resize(0);
    
    chosen_value.insert(chosen_value.end(),
            reinterpret_cast<const uint8_t*>(&message.data_len_),
            reinterpret_cast<const uint8_t*>(&message.data_len_) + sizeof(uint64_t));
    chosen_value.insert(chosen_value.end(), message.data_, message.data_ + message.data_len_);
    
    for (uint64_t blob_id = 0; blob_id < message.blob_count_; ++blob_id)
    {
        if (message.blobs_[blob_id].valid())
        {
            uint64_t blob_size = message.blobs_[blob_id].size();
            uint8_t* blob_data = message.blobs_[blob_id].data();

            chosen_value.insert(chosen_value.end(),
                reinterpret_cast<const uint8_t*>(&blob_size),
                reinterpret_cast<const uint8_t*>(&blob_size) + sizeof(uint64_t));
            chosen_value.insert(chosen_value.end(), blob_data, blob_data + blob_size);
        }
        else
        {
            uint64_t blob_size = 0;
            chosen_value.insert(chosen_value.end(),
                reinterpret_cast<const uint8_t*>(&blob_size),
                reinterpret_cast<const uint8_t*>(&blob_size) + sizeof(uint64_t));
        }
    }

    message_wait_.expected_ = false;
}