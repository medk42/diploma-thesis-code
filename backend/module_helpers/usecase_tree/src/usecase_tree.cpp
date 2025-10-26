#include "module_helpers/usecase_tree/usecase_tree.h"

#include "module_helpers/usecase_wrapper/message_types.h"
#include "module_helpers/usecase_wrapper/serialization_helper.h"
#include "module_common/serialization_helper.h"

#include <ranges>
#include <algorithm>
#include <thread>
#include <chrono>
#include <condition_variable>

using namespace aergo::module::helpers::usecase_tree;


UsecaseTree::UsecaseTree(aergo::module::BaseModule* base_module)
    : base_module_ref_(base_module),
      valid_(false)
{
    if (base_module_ref_ == nullptr)
    {
        return;
    }

    if (!base_module_ref_->getRequestChannelByName(uw::message_types::usecase_request_consumer.channel_type_identifier_, usecase_request_channel_id_))
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree: Failed to get usecase_request_channel_id_ channel ID.");
        return;
    }

    dynamic_allocator_ = base_module_ref_->createDynamicAllocator();
    if (!dynamic_allocator_)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree: Failed to create dynamic allocator.");
        return;
    }

    valid_ = true;
}


void UsecaseTree::handleResponse(ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& message)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto handler_it = response_handlers_.find(message.id_);
    if (handler_it != response_handlers_.end())
    {
        auto handler = handler_it->second;
        response_handlers_.erase(handler_it);
        handler(source_channel, message);
    }
    else
    {
        base_module_ref_->log(logging::LogType::WARNING, "UsecaseTree: Received response with unknown ID.");
    }
}


bool UsecaseTree::updateAvailableUsecases(std::optional<std::function<void()>> on_finish)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (pending_modules_for_update_.size() > 0)
    {
        base_module_ref_->log(logging::LogType::WARNING, "UsecaseTree::updateAvailableUsecases: Update already in progress.");
        return false;
    }

    auto core_ref = base_module_ref_->getCoreControl();
    aergo::module::message::SharedDataBlob existing_channels_blob = core_ref->getExistingResponseChannelsByName(uw::message_types::usecase_response_producer.channel_type_identifier_);

    if (!existing_channels_blob.valid() || existing_channels_blob.data() == nullptr)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::getAllUsecaseResponseChannels: no data");
        return false;
    }

    aergo::module::deserialize::des::BufferReader deserialize(existing_channels_blob.data(), existing_channels_blob.size());

    std::vector<ChannelIdentifier> existing_channels;
    if (!deserialize::readExistingChannels(deserialize, existing_channels))
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::getAllUsecaseResponseChannels: failed to read existing channels");
        return false;
    }

    pending_update_on_finish_ = on_finish;

    for (const auto& channel : existing_channels)
    {
        uw::message_types::Request request { .req_type_ = uw::message_types::ReqType::READ_COMMAND_PARAMETERS };
        aergo::module::message::MessageHeader request_message = aergo::module::message::MessageHeader::Message(&request);

        uint64_t request_id = base_module_ref_->sendRequest(usecase_request_channel_id_, channel, request_message);

        response_handlers_[request_id] = [this](ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& response_message)
        {
            if (pending_modules_for_update_.find(source_channel.producer_module_id_) == pending_modules_for_update_.end())
            {
                base_module_ref_->log(logging::LogType::WARNING, "UsecaseTree::updateAvailableUsecases: Received unexpected response from module " + std::to_string(source_channel.producer_module_id_));
                return;
            }

            pending_modules_for_update_.erase(source_channel.producer_module_id_);

            uw::message_types::Response response;
            if (!response_message.readAs(response))
            {
                base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::updateAvailableUsecases: Failed to read response from module " + std::to_string(source_channel.producer_module_id_));    
                return;
            }

            if (response.result_ != uw::message_types::Result::SUCCESS)
            {
                base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::updateAvailableUsecases: Module " + std::to_string(source_channel.producer_module_id_) + " reported failure in response.");
                return;
            }

            aergo::module::helpers::usecase_wrapper::deserialize::des::BufferReader reader(response_message.data_, response_message.data_len_);

            std::string param_identifier, param_name, param_desc;
            aergo::module::helpers::parameter_description::ParameterList auto_parameters, required_parameters, advanced_parameters;
            
            if (!aergo::module::helpers::usecase_wrapper::deserialize::readParameters(
                reader, 
                param_identifier,
                param_name,
                param_desc,
                auto_parameters,
                required_parameters,
                advanced_parameters
            ))
            {
                base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::updateAvailableUsecases: Failed to read usecase parameters from module " + std::to_string(source_channel.producer_module_id_));
                return;
            }

            auto [it, inserted] = available_usecases_map_.try_emplace(
                param_identifier,
                param_identifier, // copy param_identifier, because it's used as key in the map
                std::move(param_name),
                std::move(param_desc),
                std::move(auto_parameters),
                std::move(required_parameters),
                std::move(advanced_parameters),
                source_channel
            );

            if (!inserted)
            {
                base_module_ref_->log(logging::LogType::WARNING, "UsecaseTree::updateAvailableUsecases: Usecase with identifier '" + param_identifier + "' already exists, skipping.");
            }

            if (pending_modules_for_update_.empty() && pending_update_on_finish_.has_value())
            {
                auto on_finish_callback = pending_update_on_finish_.value();
                pending_update_on_finish_ = std::nullopt;
                on_finish_callback();
            }
        };

        pending_modules_for_update_.insert(channel.producer_module_id_);
    }

    return true;
}


const std::map<std::string, structs::AvailableUsecase>& UsecaseTree::getAvailableUsecases() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return available_usecases_map_;
}


bool UsecaseTree::appendCommand(const std::string& param_identifier)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = available_usecases_map_.find(param_identifier);
    if (it == available_usecases_map_.end())
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::appendCommand: Usecase with identifier '" + param_identifier + "' not found.");
        return false;
    }

    const structs::AvailableUsecase& available_usecase = it->second;

    uint64_t command_id = next_command_id_++;
    existing_commands_list_.push_back(std::move(structs::ExistingCommand(
        param_identifier,
        command_id,
        &available_usecases_map_,
        std::move(available_usecase.getAutoParameters().buildParameterValues()),
        std::move(available_usecase.getRequiredParameters().buildParameterValues()),
        std::move(available_usecase.getAdvancedParameters().buildParameterValues())
    )));
    command_id_to_index_map_[command_id] = existing_commands_list_.size() - 1;

    return true;
}


bool UsecaseTree::insertCommand(size_t list_index, structs::ExistingCommand command)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (list_index > existing_commands_list_.size())
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::insertCommand: list_index out of range.");
        return false;
    }

    uint64_t command_id = next_command_id_++;
    existing_commands_list_.insert(existing_commands_list_.begin() + list_index, std::move(structs::ExistingCommand(
        std::move(command), command_id
    )));
    // update command_id_to_index_map_
    for (size_t i = list_index; i < existing_commands_list_.size(); ++i)
    {
        command_id_to_index_map_[existing_commands_list_[i].getCommandId()] = i;
    }

    return true;
}


bool UsecaseTree::removeCommand(size_t list_index)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (list_index >= existing_commands_list_.size())
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::removeCommand: list_index out of range.");
        return false;
    }

    uint64_t command_id = existing_commands_list_[list_index].getCommandId();
    existing_commands_list_.erase(existing_commands_list_.begin() + list_index);
    command_id_to_index_map_.erase(command_id);
    // update command_id_to_index_map_
    for (size_t i = list_index; i < existing_commands_list_.size(); ++i)
    {
        command_id_to_index_map_[existing_commands_list_[i].getCommandId()] = i;
    }

    return true;
}


bool UsecaseTree::generateCommandDataJson(size_t list_index, std::optional<std::function<void(bool, uw::helper::ErrorInfo)>> on_finish)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (list_index >= existing_commands_list_.size())
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::generateCommandDataJson: list_index out of range.");
        return false;
    }


    const structs::ExistingCommand& command = existing_commands_list_[list_index];
    const structs::AvailableUsecase* usecase_ref = command.getUsecaseReference();
    if (!usecase_ref)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::generateCommandDataJson: Usecase reference invalid for command at index " + std::to_string(list_index) + ".");
        return false;
    }


    constexpr std::array types = {
        structs::ExistingCommand::ParamType::AUTO,
        structs::ExistingCommand::ParamType::REQUIRED,
        structs::ExistingCommand::ParamType::ADVANCED
    };

    bool all_filled = std::ranges::all_of(types, [&](auto type) {
        return std::ranges::all_of(
            command.getParameterValues(type) | std::views::join,
            [](auto const& val){ return val.has_value(); }
        );
    });

    if (!all_filled)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::generateCommandDataJson: Not all parameter values are filled for command at index " + std::to_string(list_index) + ".");
        return false;
    }


    auto transform_to_value = [&command](structs::ExistingCommand::ParamType param_type) {
        std::vector<std::vector<uw::helper::ParameterTypeValue>> param_values;

        const auto& param_values_opt = command.getParameterValues(param_type);
        const auto& params = command.getParameters(param_type)->getParameters();

        param_values.resize(param_values_opt.size());
        for (size_t i = 0; i < param_values_opt.size(); ++i)
        {
            p_desc::ParameterType param_type = params[i].type_;

            for (const p_desc::ParameterValueOpt& p_opt : param_values_opt[i])
            {
                uw::helper::ParameterTypeValue type_value {
                    .type_ = param_type,
                    .value_ = *p_opt
                };
                param_values[i].push_back(std::move(type_value));
            }
        }
        
        return param_values;
    };


    uw::message_types::Request request { .req_type_ = uw::message_types::ReqType::CREATE_COMMAND };

    std::vector<std::byte> serialized_parameters;
    uw::serialize::pushParameterValues(serialized_parameters, transform_to_value(structs::ExistingCommand::ParamType::AUTO));
    uw::serialize::pushParameterValues(serialized_parameters, transform_to_value(structs::ExistingCommand::ParamType::REQUIRED));
    uw::serialize::pushParameterValues(serialized_parameters, transform_to_value(structs::ExistingCommand::ParamType::ADVANCED));

    auto blob = dynamic_allocator_->allocateFromData(std::span{serialized_parameters});

    uint64_t request_id = base_module_ref_->sendRequest(
        usecase_request_channel_id_, 
        usecase_ref->getCommunicationChannel(),
        aergo::module::message::MessageHeader::Message(&request, &blob)
    );

    std::string usecase_identifier = command.getUsecaseIdentifier();
    uint64_t command_id = command.getCommandId();
    response_handlers_[request_id] = [this, usecase_identifier, command_id, on_finish](ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& response_message)
    {
        if (!response_message.success_)
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::generateCommandDataJson: Received failure response for command with ID: " + std::to_string(command_id) + ", usecase_identifier: " + usecase_identifier + ".");
            if (on_finish) (*on_finish)(false, uw::helper::ErrorInfo::WithDetails(0, "Request failed to get processed/reach module."));
            return;
        }

        uw::message_types::Response response;
        if (!response_message.readAs(response))
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::generateCommandDataJson: Failed to read response for command with ID: " + std::to_string(command_id) + ", usecase_identifier: " + usecase_identifier + ".");    
            if (on_finish) (*on_finish)(false, uw::helper::ErrorInfo::WithDetails(0, "Module responded with an invalid response."));
            return;
        }

        if (response.result_ != uw::message_types::Result::SUCCESS)
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::generateCommandDataJson: Module reported failure in response for command with ID: " + std::to_string(command_id) + ", usecase_identifier: " + usecase_identifier + ".");
            if (response_message.blobs_ != nullptr && response_message.blob_count_ == 1)
            {
                aergo::module::message::SharedDataBlob error_blob = response_message.blobs_[0];
                uw::deserialize::des::BufferReader reader(error_blob.data(), error_blob.size());

                uw::helper::ErrorInfo error_info;
                if (uw::deserialize::readErrorInfo(reader, error_info))
                {
                    if (on_finish) (*on_finish)(false, error_info);
                    return;
                }
            }
            if (on_finish) (*on_finish)(false, uw::helper::ErrorInfo::WithDetails(0, "Module responded with FAILURE result, no specific error info available."));
            return;
        }

        if (response_message.data_ == nullptr || response_message.data_len_ != 1)
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::generateCommandDataJson: Empty response data for command with ID: " + std::to_string(command_id) + ", usecase_identifier: " + usecase_identifier + ".");
            if (on_finish) (*on_finish)(false, uw::helper::ErrorInfo::WithDetails(0, "Module responded with SUCCESS state, but no data."));
            return;
        }

        auto command_data_blob = response_message.blobs_[0];
        uw::deserialize::des::BufferReader command_data_reader(command_data_blob.data(), command_data_blob.size());
        std::string command_data_json;
        if (!uw::deserialize::readString(command_data_reader, command_data_json))
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::generateCommandDataJson: Failed to read command data JSON for command with ID: " + std::to_string(command_id) + ", usecase_identifier: " + usecase_identifier + ".");
            if (on_finish) (*on_finish)(false, uw::helper::ErrorInfo::WithDetails(0, "Failed to read command data JSON from module response."));
            return;
        }

        if (!command_id_to_index_map_.contains(command_id))
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::generateCommandDataJson: Command ID to index map does not contain command ID: " + std::to_string(command_id) + ", usecase_identifier: " + usecase_identifier + ".");
            if (on_finish) (*on_finish)(false, uw::helper::ErrorInfo::WithDetails(0, "Internal error: Command ID to index map does not contain command ID."));
            return;
        }

        existing_commands_list_[command_id_to_index_map_[command_id]].setCommandDataJson(std::move(command_data_json));
        if (on_finish) (*on_finish)(true, uw::helper::ErrorInfo::WithoutDetails());
    };

    return true;
}


bool UsecaseTree::readCustomValue(size_t list_index, size_t param_index, size_t list_index_in_param, std::atomic<bool>& cancel_read, std::optional<std::function<void()>> on_value_ready_callback)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (list_index >= existing_commands_list_.size())
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::readCustomValue: list_index out of range.");
        return false;
    }

    const structs::ExistingCommand& command = existing_commands_list_[list_index];
    const structs::AvailableUsecase* usecase_ref = command.getUsecaseReference();
    if (!usecase_ref)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::readCustomValue: Usecase reference invalid for command at index " + std::to_string(list_index) + ".");
        return false;
    }

    auto& param_values = command.getParameterValues(structs::ExistingCommand::ParamType::AUTO);
    if (param_index >= param_values.size() || list_index_in_param >= param_values[param_index].size())
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::readCustomValue: param_index or list_index_in_param out of range.");
        return false;
    }

    uw::message_types::Request request { 
        .req_type_ = uw::message_types::ReqType::READ_CUSTOM_PARAMETER_START, 
        .param_id_ = static_cast<uint32_t>(param_index) 
    };

    uint64_t request_id = base_module_ref_->sendRequest(
        usecase_request_channel_id_, 
        usecase_ref->getCommunicationChannel(),
        aergo::module::message::MessageHeader::Message(&request)
    );

    uint64_t command_id = command.getCommandId();
    std::string usecase_identifier = command.getUsecaseIdentifier();
    response_handlers_[request_id] = [this, command_id, usecase_identifier, param_index, list_index_in_param, &cancel_read, on_value_ready_callback](ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& response_message)
    {
        if (!response_message.success_)
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::readCustomValue: Received failure response for custom value read, param_index: " + std::to_string(param_index) + ".");
            if (on_value_ready_callback) (*on_value_ready_callback)();
            return;
        }

        uw::message_types::Response response;
        if (!response_message.readAs(response))
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::readCustomValue: Failed to read response for custom value read, param_index: " + std::to_string(param_index) + ".");    
            if (on_value_ready_callback) (*on_value_ready_callback)();
            return;
        }

        if (response.result_ != uw::message_types::Result::SUCCESS)
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::readCustomValue: Module reported failure in response for custom value read, param_index: " + std::to_string(param_index) + ".");
            if (on_value_ready_callback) (*on_value_ready_callback)();
            return;
        }

        uint64_t task_id = response.task_id_;


        uw::message_types::Request check_request { 
            .req_type_ = uw::message_types::ReqType::READ_CUSTOM_PARAMETER_CHECK, 
            .task_id_ = task_id,
            .cancel_ = cancel_read.load()
        };

        auto it = available_usecases_map_.find(usecase_identifier);
        if (it == available_usecases_map_.end())
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::readCustomValue: Usecase with identifier '" + usecase_identifier + "' not found.");
            if (on_value_ready_callback) (*on_value_ready_callback)();
            return;
        }
        const structs::AvailableUsecase& usecase_ref = it->second;

        uint64_t check_request_id = base_module_ref_->sendRequest(
            usecase_request_channel_id_, 
            usecase_ref.getCommunicationChannel(),
            aergo::module::message::MessageHeader::Message(&check_request)
        );
        
        response_handlers_[check_request_id] = [this, command_id, task_id, param_index, list_index_in_param, &cancel_read, on_value_ready_callback](ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& response_message)
        {
            processCustomValueResponse(command_id, task_id, param_index, list_index_in_param, cancel_read, on_value_ready_callback, source_channel, response_message);
        };
    };

    return true;
}


void UsecaseTree::processCustomValueResponse(uint64_t command_id, uint64_t task_id, size_t param_index, size_t list_index_in_param, std::atomic<bool>& cancel_read, std::optional<std::function<void()>> on_value_ready_callback, ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& message)
{
    if (!message.success_)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Received failure response for custom value read, param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)();
        return;
    }

    uw::message_types::Response response;
    if (!message.readAs(response))
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Failed to read response for custom value read, param_index: " + std::to_string(param_index) + ".");    
        if (on_value_ready_callback) (*on_value_ready_callback)();
        return;
    }

    if (response.result_ == uw::message_types::Result::ID_INVALID)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Invalid task ID in response for custom value read, param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)();
        return;
    }

    if (response.result_ == uw::message_types::Result::FAIL)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Module reported failure in response for custom value read, param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)();
        return;
    }

    if (response.result_ == uw::message_types::Result::IN_PROGRESS) // still in progress, check again in a moment (call request that will "recursively" call this function on finish)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // wait before checking again

        uw::message_types::Request check_request { 
            .req_type_ = uw::message_types::ReqType::READ_CUSTOM_PARAMETER_CHECK, 
            .task_id_ = task_id,
            .cancel_ = cancel_read.load()
        };

        uint64_t check_request_id = base_module_ref_->sendRequest(
            usecase_request_channel_id_, 
            source_channel,
            aergo::module::message::MessageHeader::Message(&check_request)
        );

        response_handlers_[check_request_id] = [this, command_id, task_id, param_index, list_index_in_param, &cancel_read, on_value_ready_callback](ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& response_message)
        {
            processCustomValueResponse(command_id, task_id, param_index, list_index_in_param, cancel_read, on_value_ready_callback, source_channel, response_message);
        };
    }

    if (response.result_ != uw::message_types::Result::SUCCESS) // should be SUCCESS here
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Unexpected result in response for custom value read, param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)();
        return;
    }

    if (message.blob_count_ == 0) // means cancelled request
    {
        base_module_ref_->log(logging::LogType::INFO, "UsecaseTree::processCustomValueResponse: Custom value read cancelled for param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)();
        return;
    }

    if (message.blobs_ == nullptr || message.blob_count_ != 1)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Invalid blob data in response for custom value read, param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)();
        return;
    }

    aergo::module::message::SharedDataBlob value_blob = message.blobs_[0];

    if (!command_id_to_index_map_.contains(command_id))
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Command ID to index map does not contain command ID: " + std::to_string(command_id) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)();
        return;
    }

    structs::ExistingCommand& command = existing_commands_list_[command_id_to_index_map_[command_id]];
    if (!command.setValue(structs::ExistingCommand::ParamType::AUTO, param_index, list_index_in_param, std::vector<uint8_t>(value_blob.data(), value_blob.data() + value_blob.size())))
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Failed to set custom value for command ID: " + std::to_string(command_id) + ", param_index: " + std::to_string(param_index) + ".");
    }

    if (on_value_ready_callback) (*on_value_ready_callback)();
}


bool UsecaseTree::start(bool simulate)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_program_instance_)
    {
        if (running_program_instance_->state() == ProgramInstance::ProgramState::STOPPED)
        {
            running_program_instance_ = nullptr; // clear stopped instance
        }
        else
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::start: Another program instance is already running.");
            return false;
        }
    }

    running_program_instance_ = std::make_unique<ProgramInstance>(simulate, existing_commands_list_, [this](
            ChannelIdentifier target_channel, 
            uw::message_types::Request request, 
            std::optional<std::span<const std::byte>> send_data, 
            uw::message_types::Response& out_response, 
            aergo::module::message::SharedDataBlob* out_response_blob
        )
        {
            return sendRequestSynchronized(target_channel, request, send_data, out_response, out_response_blob);
        }
    );
    
    if (!running_program_instance_->valid())
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::start: Failed to create ProgramInstance.");
        running_program_instance_ = nullptr;
        return false;
    }

    return true; // if valid, already started
}


bool UsecaseTree::sendRequestSynchronized(ChannelIdentifier target_channel, uw::message_types::Request request, std::optional<std::span<const std::byte>> send_data, uw::message_types::Response& out_response, aergo::module::message::SharedDataBlob* out_response_blob)
{
    std::unique_lock<std::mutex> lock(mutex_);

    std::condition_variable response_condition;

    message::SharedDataBlob send_blob;
    if (send_data)
    {
        send_blob = dynamic_allocator_->allocateFromData(*send_data);
    }

    uint64_t request_id = base_module_ref_->sendRequest(
        usecase_request_channel_id_, 
        target_channel,
        send_blob.valid() ? aergo::module::message::MessageHeader::Message(&request, &send_blob) : aergo::module::message::MessageHeader::Message(&request)
    );

    bool finished = false;
    bool success = false;

    response_handlers_[request_id] = [this, &finished, &success, &out_response, out_response_blob, &response_condition](ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& response_message)
    {
        if (!response_message.success_)
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::sendRequestSynchronized: Received failure response.");
            finished = true;
            response_condition.notify_one();
            return;
        }

        if (!response_message.readAs(out_response))
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::sendRequestSynchronized: Failed to read response.");
            finished = true;
            response_condition.notify_one();
            return;
        }

        if (out_response_blob != nullptr && response_message.blob_count_ == 1 && response_message.blobs_ != nullptr)
        {
            *out_response_blob = response_message.blobs_[0];
        }

        success = true;
        finished = true;
        response_condition.notify_one();
    };
    
    response_condition.wait(lock, [&finished]() { return finished; });

    return success;
}


void UsecaseTree::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_program_instance_)
    {
        running_program_instance_->stop();
    }
}


void UsecaseTree::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_program_instance_)
    {
        running_program_instance_->pause();
    }
}


void UsecaseTree::resume()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_program_instance_)
    {
        running_program_instance_->resume();
    }
}


std::optional<ProgramInstance::ProgramState> UsecaseTree::getProgramState() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_program_instance_)
    {
        return running_program_instance_->state();
    }

    return std::nullopt;
}


std::optional<ProgramInstance::ProgramResult> UsecaseTree::getProgramResult() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_program_instance_)
    {
        return running_program_instance_->getResult();
    }

    return std::nullopt;
}


void UsecaseTree::clearCommands()
{
    std::lock_guard<std::mutex> lock(mutex_);

    existing_commands_list_.clear();
    command_id_to_index_map_.clear();
}