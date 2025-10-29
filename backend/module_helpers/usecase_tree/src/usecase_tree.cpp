#include "module_helpers/usecase_tree/usecase_tree.h"

#include "module_helpers/usecase_wrapper/message_types.h"
#include "module_helpers/usecase_wrapper/serialization_helper.h"
#include "module_common/serialization_helper.h"

#include <ranges>
#include <algorithm>
#include <thread>
#include <chrono>
#include <condition_variable>

#include <nlohmann/json.hpp>

using namespace aergo::module::helpers::usecase_tree;
using json = nlohmann::json;


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


bool UsecaseTree::updateAvailableUsecases(std::optional<std::function<void(bool, const std::map<std::string, structs::AvailableUsecase>&)>> on_finish)
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

    available_usecases_map_.clear();

    if (existing_channels.size() == 0)
    {
        // no usecase response channels found
        if (on_finish.has_value())
        {
            (*on_finish)(true, available_usecases_map_);
        }
        return true;
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

            if (!response_message.success_)
            {
                pending_modules_for_update_.clear();
                auto on_finish_callback = pending_update_on_finish_.value();
                pending_update_on_finish_ = std::nullopt;
                on_finish_callback(false, available_usecases_map_);

                base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::updateAvailableUsecases: Module " + std::to_string(source_channel.producer_module_id_) + " reported failure in response.");
                return;
            }

            uw::message_types::Response response;
            if (!response_message.readAs(response))
            {
                pending_modules_for_update_.clear();
                auto on_finish_callback = pending_update_on_finish_.value();
                pending_update_on_finish_ = std::nullopt;
                on_finish_callback(false, available_usecases_map_);

                base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::updateAvailableUsecases: Failed to read response from module " + std::to_string(source_channel.producer_module_id_));    
                return;
            }

            if (response.result_ != uw::message_types::Result::SUCCESS)
            {
                pending_modules_for_update_.clear();
                auto on_finish_callback = pending_update_on_finish_.value();
                pending_update_on_finish_ = std::nullopt;
                on_finish_callback(false, available_usecases_map_);

                base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::updateAvailableUsecases: Module " + std::to_string(source_channel.producer_module_id_) + " reported failure in response.");
                return;
            }

            if (response_message.blob_count_ != 1 || response_message.blobs_ == nullptr)
            {
                pending_modules_for_update_.clear();
                auto on_finish_callback = pending_update_on_finish_.value();
                pending_update_on_finish_ = std::nullopt;
                on_finish_callback(false, available_usecases_map_);

                base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::updateAvailableUsecases: Invalid blobs in response from module " + std::to_string(source_channel.producer_module_id_));
                return;
            }

            aergo::module::helpers::usecase_wrapper::deserialize::des::BufferReader reader(response_message.blobs_[0].data(), response_message.blobs_[0].size());

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
                pending_modules_for_update_.clear();
                auto on_finish_callback = pending_update_on_finish_.value();
                pending_update_on_finish_ = std::nullopt;
                on_finish_callback(false, available_usecases_map_);

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
                on_finish_callback(true, available_usecases_map_);
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
        available_usecase.getUsecaseName(),
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


bool UsecaseTree::readCustomValue(size_t list_index, size_t param_index, size_t list_index_in_param, std::atomic<bool>& cancel_read, std::optional<std::function<void(bool)>> on_value_ready_callback)
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

    if (param_values[param_index][list_index_in_param].has_value())
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::readCustomValue: Parameter value at specified indices is already set.");
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
            if (on_value_ready_callback) (*on_value_ready_callback)(false);
            return;
        }

        uw::message_types::Response response;
        if (!response_message.readAs(response))
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::readCustomValue: Failed to read response for custom value read, param_index: " + std::to_string(param_index) + ".");    
            if (on_value_ready_callback) (*on_value_ready_callback)(false);
            return;
        }

        if (response.result_ != uw::message_types::Result::SUCCESS)
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::readCustomValue: Module reported failure in response for custom value read, param_index: " + std::to_string(param_index) + ".");
            if (on_value_ready_callback) (*on_value_ready_callback)(false);
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
            if (on_value_ready_callback) (*on_value_ready_callback)(false);
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


void UsecaseTree::processCustomValueResponse(uint64_t command_id, uint64_t task_id, size_t param_index, size_t list_index_in_param, std::atomic<bool>& cancel_read, std::optional<std::function<void(bool)>> on_value_ready_callback, ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& message)
{
    if (!message.success_)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Received failure response for custom value read, param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)(false);
        return;
    }

    uw::message_types::Response response;
    if (!message.readAs(response))
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Failed to read response for custom value read, param_index: " + std::to_string(param_index) + ".");    
        if (on_value_ready_callback) (*on_value_ready_callback)(false);
        return;
    }

    if (response.result_ == uw::message_types::Result::ID_INVALID)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Invalid task ID in response for custom value read, param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)(false);
        return;
    }

    if (response.result_ == uw::message_types::Result::FAIL)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Module reported failure in response for custom value read, param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)(false);
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
        if (on_value_ready_callback) (*on_value_ready_callback)(false);
        return;
    }

    if (message.blob_count_ == 0) // means cancelled request
    {
        base_module_ref_->log(logging::LogType::INFO, "UsecaseTree::processCustomValueResponse: Custom value read cancelled for param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)(false);
        return;
    }

    if (message.blobs_ == nullptr || message.blob_count_ != 1)
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Invalid blob data in response for custom value read, param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)(false);
        return;
    }

    aergo::module::message::SharedDataBlob value_blob = message.blobs_[0];

    if (!command_id_to_index_map_.contains(command_id))
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Command ID to index map does not contain command ID: " + std::to_string(command_id) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)(false);
        return;
    }

    structs::ExistingCommand& command = existing_commands_list_[command_id_to_index_map_[command_id]];
    if (!command.setValue(structs::ExistingCommand::ParamType::AUTO, param_index, list_index_in_param, std::vector<uint8_t>(value_blob.data(), value_blob.data() + value_blob.size())))
    {
        base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::processCustomValueResponse: Failed to set custom value for command ID: " + std::to_string(command_id) + ", param_index: " + std::to_string(param_index) + ".");
        if (on_value_ready_callback) (*on_value_ready_callback)(false);
        return;
    }

    if (on_value_ready_callback) (*on_value_ready_callback)(true);
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
    next_command_id_ = 1; // we can start from 1 again, no conflicts
}


std::optional<std::string> UsecaseTree::toJson() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto encode_single_parameter_values = [](const p_desc::ParameterValueOptList& value_opt_list, p_desc::ParameterType param_type) -> std::optional<json> {
        json j_result;
        auto type_str_opt = p_desc::string_conversions::parameterValueToString(static_cast<int32_t>(param_type));
        if (!type_str_opt.has_value())
        {
            return std::nullopt;
        }

        j_result["type"] = *type_str_opt;
        json j_value_list = json::array();
        for (const auto& value_opt : value_opt_list)
        {
            if (value_opt.has_value())
            {
                auto single_value_str_opt = p_desc::string_conversions::parameterValueToString(*value_opt);
                if (!single_value_str_opt.has_value())
                {
                    return std::nullopt;
                }

                j_value_list.push_back(*single_value_str_opt); // push the string representation
            }
            else
            {
                j_value_list.push_back(false); // represent null value as false
            }
        }

        j_result["values"] = j_value_list;
        return j_result;
    };

    auto encode_value_opt_list_list = [&encode_single_parameter_values](
        const p_desc::ParameterValueOptListList& value_opt_list_list, 
        const p_desc::ParameterList& param_list
    ) -> std::optional<json> {
        json j_values = json::array();
        const auto& parameters = param_list.getParameters();
        if (value_opt_list_list.size() != parameters.size())
        {
            return std::nullopt;
        }

        for (size_t i = 0; i < value_opt_list_list.size(); ++i)
        {
            auto single_param_json_opt = encode_single_parameter_values(value_opt_list_list[i], parameters[i].type_);
            if (!single_param_json_opt.has_value())
            {
                return std::nullopt;
            }
            j_values.push_back(*single_param_json_opt);
        }
        return j_values;
    };

    json j;
    j["program"] = json::array();
    for (const auto& command : existing_commands_list_)
    {
        auto usecase_ref = command.getUsecaseReference();
        if (!usecase_ref)
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::toJson: Usecase reference invalid for command with ID " + std::to_string(command.getCommandId()) + ".");
            return std::nullopt;
        }

        json single_command_json;
        single_command_json["usecase_identifier"] = command.getUsecaseIdentifier();
        single_command_json["usecase_name"] = command.getUsecaseName();
        
        auto auto_values_json_opt = encode_value_opt_list_list(command.getParameterValues(structs::ExistingCommand::ParamType::AUTO), usecase_ref->getAutoParameters());
        if (!auto_values_json_opt.has_value())
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::toJson: Failed to encode AUTO parameter values for command with ID " + std::to_string(command.getCommandId()) + ".");
            return std::nullopt;
        }
        single_command_json["auto_parameters"] = *auto_values_json_opt;

        auto required_values_json_opt = encode_value_opt_list_list(command.getParameterValues(structs::ExistingCommand::ParamType::REQUIRED), usecase_ref->getRequiredParameters());
        if (!required_values_json_opt.has_value())
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::toJson: Failed to encode REQUIRED parameter values for command with ID " + std::to_string(command.getCommandId()) + ".");
            return std::nullopt;
        }
        single_command_json["required_parameters"] = *required_values_json_opt;

        auto advanced_values_json_opt = encode_value_opt_list_list(command.getParameterValues(structs::ExistingCommand::ParamType::ADVANCED), usecase_ref->getAdvancedParameters());
        if (!advanced_values_json_opt.has_value())
        {
            base_module_ref_->log(logging::LogType::ERROR, "UsecaseTree::toJson: Failed to encode ADVANCED parameter values for command with ID " + std::to_string(command.getCommandId()) + ".");
            return std::nullopt;
        }
        single_command_json["advanced_parameters"] = *advanced_values_json_opt;

        single_command_json["has_command_data_json"] = command.hasCommandDataJson();
        if (command.hasCommandDataJson())
        {
            single_command_json["command_data_json"] = command.getCommandDataJson();
        }
        single_command_json["command_data_json_in_sync"] = command.isCommandDataJsonInSync();

        j["program"].push_back(single_command_json);
    }

    try
    {
        return j.dump(-1, ' ', true, nlohmann::json::error_handler_t::strict);
    }
    catch (const std::exception& e)
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, std::string("UsecaseTree::toJson: Exception during JSON serialization: ") + e.what());
    }

    return std::nullopt;
}


bool UsecaseTree::fromJson(const std::string& json_str, std::string& out_missing_usecase_identifier)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto decode_single_param_values = [this](const json& j_param, const p_desc::ParameterDescription& param_desc, p_desc::ParameterValueOptList& out_values) -> bool
    {
        if (!j_param.is_object())
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Parameter entry is not an object.");
            return false;
        }

        if (!j_param.contains("type") || !j_param["type"].is_string() || !j_param.contains("values") || !j_param["values"].is_array())
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Parameter entry missing or invalid 'type' or 'values' fields.");
            return false;
        }

        // Validate type matches; writer stored it as a stringified integer (enum underlying value).
        auto value = p_desc::string_conversions::stringToParameterValue(j_param["type"].get<std::string>(), p_desc::ParameterType::ENUM);
        if (!value.has_value() || !std::holds_alternative<int32_t>(*value))
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Parameter 'type' field is not a valid integer string.");
            return false;
        }
        int32_t type_int = std::get<int32_t>(*value);
        if (type_int != static_cast<int32_t>(param_desc.type_))
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Parameter type mismatch.");
            return false;
        }

        const auto& j_values = j_param["values"];

        // Validate list sizing constraints
        if (!param_desc.as_list_)
        {
            if (j_values.size() != 1)
            {
                base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Non-list parameter must have exactly one value.");
                return false;
            }
        }
        else
        {
            // list_size_min_ satisfied; list_size_max_ == 0 means unlimited
            if (j_values.size() < param_desc.list_size_min_)
            {
                base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: List parameter has fewer values than minimum.");
                return false;
            }
            if (param_desc.list_size_max_ != 0 && j_values.size() > param_desc.list_size_max_)
            {
                base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: List parameter has more values than maximum.");
                return false;
            }
        }

        out_values.clear();
        out_values.reserve(j_values.size());
        for (const auto& j_val : j_values)
        {
            if (j_val.is_boolean() && j_val.get<bool>() == false)
            {
                // Stored sentinel for null
                out_values.push_back(std::nullopt);
                continue;
            }
            if (!j_val.is_string())
            {
                base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Parameter value must be a string (or false for null).\n");
                return false;
            }

            auto parsed = p_desc::string_conversions::stringToParameterValue(j_val.get<std::string>(), param_desc.type_);
            if (!parsed.has_value())
            {
                base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Failed to load parameter value from string.");
                return false;
            }
            out_values.push_back(parsed);
        }

        return true;
    };

    auto decode_param_group = [&decode_single_param_values, this](const json& j_group, const p_desc::ParameterList& param_list, p_desc::ParameterValueOptListList& out_list_list) -> bool
    {
        if (!j_group.is_array())
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Parameter group is not an array.");
            return false;
        }

        const auto& params = param_list.getParameters();
        if (j_group.size() != params.size())
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Parameter group size does not match usecase description.");
            return false;
        }

        out_list_list.clear();
        out_list_list.resize(params.size());
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (!decode_single_param_values(j_group[i], params[i], out_list_list[i]))
            {
                return false;
            }
        }
        return true;
    };
    
    json j = json::parse(json_str.data(), json_str.data() + json_str.size(), nullptr, false); // false == non-throwing parse
    if (j.is_discarded())
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Failed to parse JSON string.");
        return false;
    }

    if (!j.contains("program") || !j["program"].is_array())
    {
        base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: JSON does not contain valid 'program' array.");
        return false;
    }

    clearCommands();

    for (const auto& cmd_json : j["program"])
    {
        if (!cmd_json.is_object())
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Each program entry must be an object.");
            return false;
        }

        if (!cmd_json.contains("usecase_identifier") || !cmd_json["usecase_identifier"].is_string())
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Program entry missing 'usecase_identifier'.");
            return false;
        }
        std::string usecase_identifier = cmd_json["usecase_identifier"].get<std::string>();

        if (!cmd_json.contains("usecase_name") || !cmd_json["usecase_name"].is_string())
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Program entry missing 'usecase_name'.");
            return false;
        }
        std::string usecase_name = cmd_json["usecase_name"].get<std::string>();

        auto usecase_it = available_usecases_map_.find(usecase_identifier);
        if (usecase_it == available_usecases_map_.end())
        {
            out_missing_usecase_identifier = usecase_identifier;
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Usecase '" + usecase_identifier + "' not found in available usecases. Update usecases first.");
            return false;
        }
        const auto& usecase_ref = usecase_it->second;

        if (!cmd_json.contains("auto_parameters") || !cmd_json.contains("required_parameters") || !cmd_json.contains("advanced_parameters"))
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: Program entry missing parameter groups.");
            return false;
        }

        p_desc::ParameterValueOptListList auto_values, required_values, advanced_values;
        if (!decode_param_group(cmd_json["auto_parameters"], usecase_ref.getAutoParameters(), auto_values))
        {
            return false;
        }
        if (!decode_param_group(cmd_json["required_parameters"], usecase_ref.getRequiredParameters(), required_values))
        {
            return false;
        }
        if (!decode_param_group(cmd_json["advanced_parameters"], usecase_ref.getAdvancedParameters(), advanced_values))
        {
            return false;
        }

        bool has_command_data_json = false;
        std::optional<std::string> command_data_json = std::nullopt;
        bool command_data_json_in_sync = false;

        if (!cmd_json.contains("has_command_data_json") || !cmd_json["has_command_data_json"].is_boolean())
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: 'has_command_data_json' missing or not a boolean.");
            return false;
        }
        has_command_data_json = cmd_json["has_command_data_json"].get<bool>();
        if (has_command_data_json)
        {
            if (!cmd_json.contains("command_data_json") || !cmd_json["command_data_json"].is_string())
            {
                base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: 'command_data_json' missing or not a string while 'has_command_data_json' is true.");
                return false;
            }
            command_data_json = cmd_json["command_data_json"].get<std::string>();
        }
        if (!cmd_json.contains("command_data_json_in_sync") || !cmd_json["command_data_json_in_sync"].is_boolean())
        {
            base_module_ref_->log(aergo::module::logging::LogType::ERROR, "UsecaseTree::fromJson: 'command_data_json_in_sync' missing or not a boolean.");
            return false;
        }
        command_data_json_in_sync = cmd_json["command_data_json_in_sync"].get<bool>();

        // Create command with a new ID and insert into list
        uint64_t command_id = next_command_id_++;
        existing_commands_list_.push_back(std::move(structs::ExistingCommand(
            std::move(usecase_identifier),
            std::move(usecase_name),
            command_id,
            &available_usecases_map_,
            std::move(auto_values),
            std::move(required_values),
            std::move(advanced_values),
            std::move(command_data_json),
            command_data_json_in_sync
        )));
        command_id_to_index_map_[command_id] = existing_commands_list_.size() - 1;
    }

    return true;
}