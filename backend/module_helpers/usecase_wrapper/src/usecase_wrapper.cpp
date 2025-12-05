#include "module_helpers/usecase_wrapper/usecase_wrapper.h"
#include "module_helpers/usecase_wrapper/message_types.h"

using namespace aergo::module::helpers::usecase_wrapper;
using namespace aergo::module;



UsecaseWrapper::UsecaseWrapper(
    std::unique_ptr<aergo::module::IModule> module,
    std::string param_name,
    std::string param_desc,
    p_desc::ParameterList auto_parameters,
    p_desc::ParameterList manual_parameters,
    p_desc::ParameterList advanced_parameters
)
: valid_(false), module_ref_(std::move(module)), 
  auto_parameters_(std::move(auto_parameters)), manual_parameters_(std::move(manual_parameters)), 
  advanced_parameters_(std::move(advanced_parameters)),
  param_name_(std::move(param_name)), param_desc_(std::move(param_desc))
{
    if (module_ref_.get() == nullptr)
    {
        return;
    }

    base_module_ref_ = module_ref_->query<aergo::module::BaseModule>();
    if (base_module_ref_ == nullptr)
    {
        return;
    }

    usecase_module_ref_ = module_ref_->query<aergo::module::helpers::usecase_wrapper::IUsecaseModule>();
    if (!usecase_module_ref_)
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: Wrapped module does not implement IUsecaseModule interface.");
        return;
    }

    const aergo::module::ModuleInfo* module_info = module_ref_->getModuleInfo();
    if (module_info == nullptr)
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: Wrapped module has no ModuleInfo.");
        return;
    }

    if (!validateParameterList(module_info, auto_parameters_, true, false) ||
        !validateParameterList(module_info, manual_parameters_, false, false) ||
        !validateParameterList(module_info, advanced_parameters_, false, true))
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: Invalid parameter list(s) provided.");
        return;
    }

    if (!base_module_ref_->getResponseChannelByName(message_types::usecase_response_producer.channel_type_identifier_, expected_response_producer_id_))
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: Wrapped module does not have required response channel.");
        return;
    }

    dynamic_allocator_ = base_module_ref_->createDynamicAllocator();
    if (!dynamic_allocator_)
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: Failed to create dynamic allocator.");
        return;
    }

    // Currently no special initialization is needed
    valid_ = true;
}



void UsecaseWrapper::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    {
        std::lock_guard<std::mutex> lock(custom_read_mutex_);
        if (message_await_list_.size() > 0)
        {
            auto it = std::find_if(
                message_await_list_.begin(), 
                message_await_list_.end(), 
                [subscribe_consumer_id](const CustomMessageAwaitInfo& info) { 
                    return info.channel_id_ == subscribe_consumer_id && info.seen_;  // we already saw it in onIngress
                }
            );
            if (it != message_await_list_.end())
            {
                CustomMessageDoneInfo done_info {
                    .read_task_id_ = it->read_task_id_,
                    .success_ = true,
                    .result_ = {}
                };
                serialize::pushMessage(done_info.result_, message);

                message_done_list_.emplace_back(std::move(done_info));
                message_await_list_.erase(it);
                return;
            }
        }
    }

    module_ref_->processMessage(subscribe_consumer_id, source_channel, message);
}



aergo::module::ResponseData UsecaseWrapper::processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (response_producer_id == expected_response_producer_id_)
    {
        return processUsecaseRequest(message);
    }
    else
    {
        return module_ref_->processRequest(response_producer_id, source_channel, message);
    }
}



void UsecaseWrapper::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    {
        std::lock_guard<std::mutex> lock(custom_read_mutex_);
        if (response_await_list_.size() > 0)
        {
            uint64_t response_id = message.id_;
            auto it = std::find_if(
                response_await_list_.begin(), 
                response_await_list_.end(), 
                [request_consumer_id,response_id](const CustomMessageAwaitInfo& info) { 
                    return info.channel_id_ == request_consumer_id && info.request_id_ == response_id && info.seen_; // we already saw it in onIngress
                }
            );
            if (it != response_await_list_.end())
            {
                if (message.success_)
                {
                    CustomMessageDoneInfo done_info {
                        .read_task_id_ = it->read_task_id_,
                        .success_ = true,
                        .result_ = {}
                    };
                    serialize::pushMessage(done_info.result_, message);
                    response_done_list_.emplace_back(std::move(done_info));
                }
                else
                {
                    response_done_list_.emplace_back(CustomMessageDoneInfo{
                        .read_task_id_ = it->read_task_id_,
                        .success_ = false,
                        .result_ = {}
                    });
                }
                response_await_list_.erase(it);
                return;
            }
        }
    }
    
    module_ref_->processResponse(request_consumer_id, source_channel, message);
}



bool UsecaseWrapper::valid() noexcept
{
    if (module_ref_ == nullptr || module_ref_->valid() == false) // no need to synchronize, module_ref_/valid is not changed after initialization
    {
        return false;
    }

    return valid_;
}



void* UsecaseWrapper::query_capability(const std::type_info& id) noexcept
{
    return module_ref_->query_capability(id); // no need to synchronize, module_ref_ is not changed after initialization
}



aergo::module::IModule::IngressDecision UsecaseWrapper::onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
{
    if (kind == aergo::module::IModule::ProcessingType::REQUEST && local_channel_id == expected_response_producer_id_)
    {
        return aergo::module::IModule::IngressDecision::ACCEPT;
    }
    else if (kind == aergo::module::IModule::ProcessingType::RESPONSE)
    {
        std::lock_guard<std::mutex> lock(custom_read_mutex_);
        if (response_await_list_.size() > 0)
        {
            uint64_t response_id = msg.id_;
            auto it = std::find_if(
                response_await_list_.begin(), 
                response_await_list_.end(), 
                [local_channel_id,response_id](const CustomMessageAwaitInfo& info) { 
                    return info.channel_id_ == local_channel_id && info.request_id_ == response_id && !info.seen_;
                }
            );
            if (it != response_await_list_.end())
            {
                if (queue_status == QueueStatus::QUEUE_FULL)
                {
                    response_done_list_.emplace_back(CustomMessageDoneInfo{
                        .read_task_id_ = it->read_task_id_,
                        .success_ = false,
                        .result_ = {}
                    });
                    response_await_list_.erase(it);

                    base_module_ref_->log(aergo::module::logging::LogType::WARNING, "UsecaseWrapper: Dropping custom parameter response due to full queue.");
                    return aergo::module::IModule::IngressDecision::DROP;
                }
                else
                {
                    it->seen_ = true;
                    return aergo::module::IModule::IngressDecision::ACCEPT;
                }
            }
        }
    }
    else if (kind == aergo::module::IModule::ProcessingType::MESSAGE)
    {
        std::lock_guard<std::mutex> lock(custom_read_mutex_);
        if (message_await_list_.size() > 0)
        {
            auto it = std::find_if(
                message_await_list_.begin(), 
                message_await_list_.end(), 
                [local_channel_id](const CustomMessageAwaitInfo& info) { 
                    return info.channel_id_ == local_channel_id && !info.seen_; 
                }
            );
            if (it != message_await_list_.end())
            {
                if (queue_status == QueueStatus::QUEUE_FULL)
                {
                    message_done_list_.emplace_back(CustomMessageDoneInfo{
                        .success_ = false,
                        .result_ = {}
                    });
                    message_await_list_.erase(it);
                    
                    base_module_ref_->log(aergo::module::logging::LogType::WARNING, "UsecaseWrapper: Dropping custom parameter message due to full queue.");
                    return aergo::module::IModule::IngressDecision::DROP;
                }
                else
                {
                    it->seen_ = true;
                    return aergo::module::IModule::IngressDecision::ACCEPT;
                }
            }
        }
    }
    
    return module_ref_->onIngress(kind, local_channel_id, src, msg, queue_status);
}




bool UsecaseWrapper::threadStart(uint32_t timeout_ms) noexcept
{
    return module_ref_->threadStart(timeout_ms);
}


bool UsecaseWrapper::threadStop(uint32_t timeout_ms) noexcept
{
    return module_ref_->threadStop(timeout_ms);
}



ISerializableModule::SaveData UsecaseWrapper::save() noexcept
{
    return module_ref_->save(); // we have no data to save ourselves
}



bool UsecaseWrapper::load(ISerializableModule::SaveData data) noexcept
{
    return module_ref_->load(std::move(data)); // we have no data to load ourselves
}


const ModuleInfo* UsecaseWrapper::getModuleInfo() const noexcept
{
    return module_ref_->getModuleInfo();
}



bool UsecaseWrapper::validateParameterList(const aergo::module::ModuleInfo* module_info, const p_desc::ParameterList& params, bool only_custom_allowed, bool default_values_required) const
{
    const auto& parameters = params.getParameters();
    for (const auto& param : parameters)
    {
        if (only_custom_allowed && param.type_ != p_desc::ParameterType::CUSTOM)
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: Parameter type validation failed - only CUSTOM type allowed; parameter name: " + param.param_name_);
            return false;
        }
        if (param.type_ == p_desc::ParameterType::CUSTOM)
        {
            if (param.custom_channel_type_ == helpers::parameter_description::CustomChannelType::SUBSCRIBE 
             && param.custom_channel_id_ > module_info->subscribe_consumer_count_)
            {
                base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: Parameter custom channel validation failed - subscribe channel id out of range; parameter name: " + param.param_name_);
                return false;
            }
            if (param.custom_channel_type_ == helpers::parameter_description::CustomChannelType::REQUEST 
             && param.custom_channel_id_ > module_info->request_consumer_count_)
            {
                base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: Parameter custom channel validation failed - request channel id out of range; parameter name: " + param.param_name_);
                return false;
            }
        }
        if (!only_custom_allowed && param.type_ == p_desc::ParameterType::CUSTOM)
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: Parameter type validation failed - CUSTOM type not allowed; parameter name: " + param.param_name_);
            return false;
        }
        if (default_values_required && !p_desc::string_conversions::parseDefaultValue(param).has_value())
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: Parameter default value validation failed - default value required; parameter name: " + param.param_name_);
            return false;
        }
    }
    return true;
}



aergo::module::ResponseData UsecaseWrapper::processUsecaseRequest(message::MessageHeader message) noexcept
{
    message_types::Request request;
    if (!message.readAs<message_types::Request>(request))
    {
        return ResponseData::createFailure(); // unsuccessful, or invalid message
    }
    
    if ((request.req_type_ == message_types::ReqType::CREATE_COMMAND ||
         request.req_type_ == message_types::ReqType::PROGRAM_READ_VISUALIZATION ||
         request.req_type_ == message_types::ReqType::PROGRAM_START_REAL ||
         request.req_type_ == message_types::ReqType::PROGRAM_START_SIMULATED) &&
        (message.blob_count_ == 0 || message.blobs_ == nullptr || !message.blobs_[0].valid()))
    {
        return ResponseData::createFailure(); // missing required blob
    }


    switch (request.req_type_)
    {
        case message_types::ReqType::READ_COMMAND_PARAMETERS:
            return handleReadCommandParameters();
        case message_types::ReqType::READ_CUSTOM_PARAMETER_START:
            return handleReadCustomParameterStart(request.param_id_);
        case message_types::ReqType::READ_CUSTOM_PARAMETER_CHECK:
            return handleReadCustomParameterCheck(request.task_id_, request.cancel_);
        case message_types::ReqType::CREATE_COMMAND:
            return handleCreateCommand(message.blobs_[0]);
        case message_types::ReqType::PROGRAM_READ_VISUALIZATION:
            return handleProgramReadVisualization(message.blobs_[0]);
        case message_types::ReqType::PROGRAM_START_REAL:
            return handleProgramStart(message.blobs_[0], false);
        case message_types::ReqType::PROGRAM_START_SIMULATED:
            return handleProgramStart(message.blobs_[0], true);
        case message_types::ReqType::PROGRAM_PAUSE:
        case message_types::ReqType::PROGRAM_STATUS:
        case message_types::ReqType::PROGRAM_RESUME:
        case message_types::ReqType::PROGRAM_STOP:
        case message_types::ReqType::PROGRAM_REMOVE:
            return handleProgramCommand(request.req_type_, request.task_id_);
        default:
            return ResponseData::createFailure(); // unknown request type
    }

    return ResponseData::createFailure(); // should not reach here
}


aergo::module::ResponseData UsecaseWrapper::handleReadCommandParameters() noexcept
{
    // serialize parameters
    const char* module_type_identifier = base_module_ref_->getModuleInfo()->module_type_identifier_;
    std::vector<uint8_t> out;

    serialize::pushParameters(
        out, 
        module_type_identifier,
        strlen(module_type_identifier),
        param_name_.c_str(),
        param_name_.size(),
        param_desc_.c_str(),
        param_desc_.size(),
        auto_parameters_, 
        manual_parameters_, 
        advanced_parameters_
    );

    return ResponseData::createResponse(
        message_types::Response{ .result_ = message_types::Result::SUCCESS }, 
        std::span(out), 
        dynamic_allocator_.get()
    );
}



aergo::module::ResponseData UsecaseWrapper::handleReadCustomParameterStart(uint32_t param_id) noexcept
{
    // find parameter in auto_parameters_
    const auto& parameters = auto_parameters_.getParameters();
    if (param_id >= parameters.size())
    {
        return ResponseData::createResponse(
            message_types::Response{ .result_ = message_types::Result::FAIL }
        );
    }

    const auto& param = parameters[param_id];
    
    std::lock_guard<std::mutex> lock(custom_read_mutex_);
    uint64_t read_task_id = next_custom_read_task_id_++;
    if (param.custom_channel_type_ == helpers::parameter_description::CustomChannelType::SUBSCRIBE)
    {
        message_await_list_.emplace_back( 
            CustomMessageAwaitInfo{ 
                .read_task_id_ = read_task_id, 
                .channel_id_ = param.custom_channel_id_, 
                .request_id_ = 0,
                .seen_ = false
            }
        );
    }
    else
    {
        uint64_t request_id = usecase_module_ref_->sendRequestFromUsecase(param.custom_channel_id_);
        response_await_list_.emplace_back( 
            CustomMessageAwaitInfo{ 
                .read_task_id_ = read_task_id, 
                .channel_id_ = param.custom_channel_id_, 
                .request_id_ = request_id,
                .seen_ = false
            }
        );
    }

    return ResponseData::createResponse(
        message_types::Response{ .result_ = message_types::Result::SUCCESS, .task_id_ = read_task_id }
    );
}



aergo::module::ResponseData UsecaseWrapper::handleReadCustomParameterCheck(uint64_t task_id, bool cancel_flag) noexcept
{
    std::lock_guard<std::mutex> lock(custom_read_mutex_);

    // check in response_await_list_
    auto it_resp_await = std::find_if(response_await_list_.begin(), response_await_list_.end(),
        [task_id](const CustomMessageAwaitInfo& info) { 
            return info.read_task_id_ == task_id; 
        }
    );
    if (it_resp_await != response_await_list_.end())
    {
        if (cancel_flag)
        {
            response_await_list_.erase(it_resp_await);
            return ResponseData::createResponse(
                message_types::Response{ .result_ = message_types::Result::SUCCESS }
            );
        }
        else
        {
            return ResponseData::createResponse(
                message_types::Response{ .result_ = message_types::Result::IN_PROGRESS }
            );
        }
    }

    // check in response_done_list_
    auto it_resp_done = std::find_if(response_done_list_.begin(), response_done_list_.end(),
        [task_id](const CustomMessageDoneInfo& info) { 
            return info.read_task_id_ == task_id; 
        }
    );
    if (it_resp_done != response_done_list_.end())
    {
        auto resp_data = std::move(it_resp_done->result_);
        bool success = it_resp_done->success_;
        response_done_list_.erase(it_resp_done);

        if (success)
        {
            return ResponseData::createResponse(
                message_types::Response{ .result_ = message_types::Result::SUCCESS }, 
                std::span(resp_data), 
                dynamic_allocator_.get()
            );
        }
        else
        {
            return ResponseData::createResponse(
                message_types::Response{ .result_ = message_types::Result::FAIL }
            );
        }
    }

    // check in message_await_list_
    auto it_msg_await = std::find_if(message_await_list_.begin(), message_await_list_.end(),
        [task_id](const CustomMessageAwaitInfo& info) { 
            return info.read_task_id_ == task_id; 
        }
    );
    if (it_msg_await != message_await_list_.end())
    {
        if (cancel_flag)
        {
            message_await_list_.erase(it_msg_await);
            return ResponseData::createResponse(
                message_types::Response{ .result_ = message_types::Result::SUCCESS }
            );
        }
        else
        {
            return ResponseData::createResponse(
                message_types::Response{ .result_ = message_types::Result::IN_PROGRESS }
            );
        }
    }

    // check in message_done_list_
    auto it_msg_done = std::find_if(message_done_list_.begin(), message_done_list_.end(),
        [task_id](const CustomMessageDoneInfo& info) { 
            return info.read_task_id_ == task_id; 
        }
    );
    if (it_msg_done != message_done_list_.end())
    {
        auto msg_data = std::move(it_msg_done->result_);
        bool success = it_msg_done->success_;
        message_done_list_.erase(it_msg_done);

        if (success)
        {
            return ResponseData::createResponse(
                message_types::Response{ .result_ = message_types::Result::SUCCESS }, 
                std::span(msg_data), 
                dynamic_allocator_.get()
            );
        }
        else
        {
            return ResponseData::createResponse(
                message_types::Response{ .result_ = message_types::Result::FAIL }
            );
        }
    }

    return ResponseData::createResponse(
        message_types::Response{ .result_ = message_types::Result::ID_INVALID }
    );
}



aergo::module::ResponseData UsecaseWrapper::handleCreateCommand(message::SharedDataBlob& blob) noexcept
{
    if (!blob.valid())
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: handleCreateCommand received invalid blob.");
        return ResponseData::createResponse(
            message_types::Response{ .result_ = message_types::Result::FAIL }
        );
    }

    deserialize::des::BufferReader reader(blob.data(), blob.size());
    std::vector<std::vector<helper::ParameterTypeValue>> auto_parameter_values_;
    std::vector<std::vector<helper::ParameterTypeValue>> required_parameter_values_;
    std::vector<std::vector<helper::ParameterTypeValue>> advanced_parameter_values_;

    if (!deserialize::readParameterValues(reader, auto_parameter_values_) ||
        !deserialize::readParameterValues(reader, required_parameter_values_) ||
        !deserialize::readParameterValues(reader, advanced_parameter_values_))
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: handleCreateCommand failed to deserialize parameter values.");
        return ResponseData::createResponse(
            message_types::Response{ .result_ = message_types::Result::FAIL }
        );
    }

    if (!validateParameterValues(auto_parameters_, auto_parameter_values_) ||
        !validateParameterValues(manual_parameters_, required_parameter_values_) ||
        !validateParameterValues(advanced_parameters_, advanced_parameter_values_))
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: handleCreateCommand received invalid parameter values.");
        return ResponseData::createResponse(
            message_types::Response{ .result_ = message_types::Result::FAIL }
        );
    }

    nlohmann::json command_data_json;
    auto res = usecase_module_ref_->createCommandFromParameters(
        auto_parameters_,
        manual_parameters_,
        advanced_parameters_,
        auto_parameter_values_,
        required_parameter_values_,
        advanced_parameter_values_,
        command_data_json
    );
    if (!res)
    {
        auto& error = res.error();

        if (error.has_details_)
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: handleCreateCommand failed to create command from parameters, error " + std::to_string(error.error_code_) + ": " + error.error_message_);

            // include error info in response
            std::vector<uint8_t> out;
            serialize::pushErrorInfo(out, error);

            return ResponseData::createResponse(
                message_types::Response{ .result_ = message_types::Result::FAIL },
                std::span(out),
                dynamic_allocator_.get()
            );
        }
        else
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: handleCreateCommand failed to create command from parameters.");
            return ResponseData::createResponse(
                message_types::Response{ .result_ = message_types::Result::FAIL }
            );
        }
    }

    std::string command_data_json_str;
    try
    {
        command_data_json_str = command_data_json.dump(-1, ' ', true, nlohmann::json::error_handler_t::strict);
    }
    catch (const std::exception& e)
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, std::string("UsecaseWrapper: handleCreateCommand failed to serialize command JSON: ") + e.what());
        return ResponseData::createResponse(
            message_types::Response{ .result_ = message_types::Result::FAIL }
        );
    }
    

    return ResponseData::createResponse(
        message_types::Response{ .result_ = message_types::Result::SUCCESS },
        std::span(reinterpret_cast<const uint8_t*>(command_data_json_str.data()), command_data_json_str.size()),
        dynamic_allocator_.get()
    );
}


bool UsecaseWrapper::validateParameterValues(const p_desc::ParameterList& param_desc_list, const std::vector<std::vector<helper::ParameterTypeValue>>& param_values) const
{
    const auto& parameters = param_desc_list.getParameters();
    if (parameters.size() != param_values.size())
    {
        return false;
    }

    for (size_t i = 0; i < parameters.size(); ++i)
    {
        const auto& param_desc = parameters[i];
        const auto& values = param_values[i];

        if (param_desc.as_list_)
        {
            if (values.size() < param_desc.list_size_min_ || (param_desc.list_size_max_ != 0 && values.size() > param_desc.list_size_max_))
            {
                return false; // list size out of bounds
            }
        }
        else if (values.size() != 1)
        {
            return false; // expected single value
        }

        for (const auto& value : values)
        {
            if (!param_desc.checkValid(value.value_))
            {
                return false;
            }
        }
    }

    return true;
}



aergo::module::ResponseData UsecaseWrapper::handleProgramReadVisualization(message::SharedDataBlob& blob) noexcept
{
    if (!blob.valid())
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: handleProgramReadVisualization received invalid blob.");
        return ResponseData::createResponse(
            message_types::Response{ .result_ = message_types::Result::FAIL }
        );
    }

    std::string command_data_json(reinterpret_cast<const char*>(blob.data()), blob.size());
    
    // TODO we don't support visualization yet
    return ResponseData::createResponse(
        message_types::Response{ .result_ = message_types::Result::FAIL }
    );
}



aergo::module::ResponseData UsecaseWrapper::handleProgramStart(message::SharedDataBlob& blob, bool simulated) noexcept
{
    if (!blob.valid())
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: handleProgramStart received invalid blob.");
        return ResponseData::createResponse(
            message_types::Response{ .result_ = message_types::Result::FAIL }
        );
    }

    nlohmann::json command_data_json = nlohmann::json::parse(blob.data(), blob.data() + blob.size(), nullptr, false); // false == non-throwing parse
    if (command_data_json.is_discarded())
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseWrapper: handleProgramStart failed to parse command JSON.");
        return ResponseData::createResponse(
            message_types::Response{ .result_ = message_types::Result::FAIL }
        );
    }
    
    uint64_t task_id = 0;
    helper::ErrorInfo error_info;
    message_types::Result start_response = usecase_module_ref_->programStart(command_data_json, simulated, task_id, error_info);
    
    if (start_response == message_types::Result::SUCCESS)
    {
        return ResponseData::createResponse(
            message_types::Response{ .result_ = message_types::Result::SUCCESS, .task_id_ = task_id }
        );
    }
    else if (start_response == message_types::Result::FAIL && error_info.has_details_)
    {
        // include error info in response
        std::vector<uint8_t> out;
        serialize::pushErrorInfo(out, error_info);

        return ResponseData::createResponse(
            message_types::Response{ .result_ = message_types::Result::FAIL },
            std::span(out),
            dynamic_allocator_.get()
        );
    }
    else
    {
        base_module_ref_->log(aergo::module::logging::LogType::WARNING, "UsecaseWrapper: handleProgramStart failed to start program.");
        return ResponseData::createResponse(
            message_types::Response{ .result_ = start_response }
        );
    }
}



aergo::module::ResponseData UsecaseWrapper::handleProgramCommand(message_types::ReqType command_type, uint64_t task_id) noexcept
{
    message_types::Response response;
    response.task_id_ = task_id;
    helper::ErrorInfo error_info;

    switch (command_type)
    {
        case message_types::ReqType::PROGRAM_STATUS:
            response.result_ = usecase_module_ref_->programStatus(task_id, response.program_status_, error_info);
            break;
        case message_types::ReqType::PROGRAM_PAUSE:
            response.result_ = usecase_module_ref_->programPause(task_id);
            break;
        case message_types::ReqType::PROGRAM_RESUME:
            response.result_ = usecase_module_ref_->programResume(task_id);
            break;
        case message_types::ReqType::PROGRAM_STOP:
            response.result_ = usecase_module_ref_->programStop(task_id);
            break;
        case message_types::ReqType::PROGRAM_REMOVE:
            response.result_ = usecase_module_ref_->programRemove(task_id);
            break;
        default:
            response.result_ = message_types::Result::FAIL;
            break;
    }

    if (command_type == message_types::ReqType::PROGRAM_STATUS 
        && response.result_ == message_types::Result::SUCCESS
        && error_info.has_details_)
    {
        // include error info in response (mainly for FAILED status)
        std::vector<uint8_t> out;
        serialize::pushErrorInfo(out, error_info);

        return ResponseData::createResponse(
            response,
            std::span(out),
            dynamic_allocator_.get()
        );
    }

    return ResponseData::createResponse(response);
}