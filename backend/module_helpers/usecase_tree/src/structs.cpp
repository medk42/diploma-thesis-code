#include "module_helpers/usecase_tree/structs.h"

using namespace aergo::module::helpers::usecase_tree::structs;


ExistingCommand::ExistingCommand(
    std::string usecase_identifier, 
    std::string usecase_name,
    uint64_t command_id, 
    const std::map<std::string, AvailableUsecase>* available_usecases_map,
    p_desc::ParameterValueOptListList&& auto_parameter_values, 
    p_desc::ParameterValueOptListList&& required_parameter_values, 
    p_desc::ParameterValueOptListList&& advanced_parameter_values,
    std::optional<std::string> command_data_json,
    bool command_data_json_in_sync)
    : usecase_identifier_(std::move(usecase_identifier)),
      usecase_name_(std::move(usecase_name)),
      command_id_(command_id),
      available_usecases_map_(available_usecases_map),
      auto_parameter_values_(std::move(auto_parameter_values)),
      required_parameter_values_(std::move(required_parameter_values)),
      advanced_parameter_values_(std::move(advanced_parameter_values)),
      command_data_json_in_sync_(command_data_json_in_sync)
{
    if (command_data_json.has_value())
    {
        command_data_json_ = std::move(command_data_json.value());
        has_command_data_json_ = true;
    }
}


ExistingCommand::ExistingCommand(ExistingCommand&& other, uint64_t new_command_id)
    : usecase_identifier_(std::move(other.usecase_identifier_)),
      command_id_(new_command_id),
      available_usecases_map_(other.available_usecases_map_),
      auto_parameter_values_(std::move(other.auto_parameter_values_)),
      required_parameter_values_(std::move(other.required_parameter_values_)),
      advanced_parameter_values_(std::move(other.advanced_parameter_values_)),
      command_data_json_(std::move(other.command_data_json_)),
      has_command_data_json_(other.has_command_data_json_),
      command_data_json_in_sync_(other.command_data_json_in_sync_)
{}


const p_desc::ParameterValueOptListList& ExistingCommand::getParameterValues(ParamType type) const
{
    switch (type)
    {
        case ParamType::AUTO:
            return auto_parameter_values_;
        case ParamType::REQUIRED:
            return required_parameter_values_;
        case ParamType::ADVANCED:
            return advanced_parameter_values_;
        default:
            throw std::runtime_error("Invalid ParamType.");
    }
}


p_desc::ParameterValueOptListList& ExistingCommand::getParameterValues(ParamType type)
{
    switch (type)
    {
        case ParamType::AUTO:
            return auto_parameter_values_;
        case ParamType::REQUIRED:
            return required_parameter_values_;
        case ParamType::ADVANCED:
            return advanced_parameter_values_;
        default:
            throw std::runtime_error("Invalid ParamType.");
    }
}


const p_desc::ParameterList* ExistingCommand::getParameters(ParamType type) const
{
    auto it = available_usecases_map_->find(usecase_identifier_);
    if (it == available_usecases_map_->end())
    {
        return nullptr;
    }

    const auto& available_usecase = it->second;
    switch (type)
    {
        case ParamType::AUTO:
            return &available_usecase.getAutoParameters();
        case ParamType::REQUIRED:
            return &available_usecase.getRequiredParameters();
        case ParamType::ADVANCED:
            return &available_usecase.getAdvancedParameters();
        default:
            throw std::runtime_error("Invalid ParamType.");
    }
}


std::optional<p_desc::ParameterValueOpt> ExistingCommand::getValue(ParamType type, size_t param_index, size_t list_index) const
{
    const auto& param_values = getParameterValues(type);
    if (param_index >= param_values.size() || list_index >= param_values[param_index].size())
    {
        return std::nullopt;
    }
    return param_values[param_index][list_index];
}


bool ExistingCommand::setValue(ParamType type, size_t param_index, size_t list_index, const p_desc::ParameterValue& value)
{
    auto& param_values = getParameterValues(type);
    if (param_index >= param_values.size() || list_index >= param_values[param_index].size())
    {
        return false;
    }
    param_values[param_index][list_index] = value;
    command_data_json_in_sync_ = false;

    return true;
}


bool ExistingCommand::addValue(ParamType type, size_t param_index, const p_desc::ParameterValueOpt& value)
{
    auto parameters = getParameters(type);
    if (parameters == nullptr)
    {
        return false; // param_identifier_ not found
    }
    const auto& param_list = parameters->getParameters();

    auto& param_values = getParameterValues(type);
    if (param_index >= param_values.size() || param_index >= param_list.size())
    {
        return false; // invalid param_index
    }

    if (!param_list[param_index].as_list_ || (param_list[param_index].list_size_max_ != 0 && param_values[param_index].size() >= param_list[param_index].list_size_max_))
    {
        return false; // not a list parameter or max size reached
    }

    param_values[param_index].push_back(value);
    command_data_json_in_sync_ = false;

    return true;
}


bool ExistingCommand::resetValue(ParamType type, size_t param_index, size_t list_index)
{
    if (type != ParamType::AUTO)
    {
        return false; // reset to nullopt only supported for CUSTOM type
    }

    auto& param_values = getParameterValues(type);
    if (param_index >= param_values.size() || list_index >= param_values[param_index].size())
    {
        return false;
    }
    param_values[param_index][list_index] = std::nullopt;
    command_data_json_in_sync_ = false;

    return true;
}


bool ExistingCommand::removeValue(ParamType type, size_t param_index, size_t list_index)
{
    auto parameters = getParameters(type);
    if (parameters == nullptr)
    {
        return false; // param_identifier_ not found
    }
    const auto& param_list = parameters->getParameters();

    auto& param_values = getParameterValues(type);
    if (param_index >= param_values.size() || param_index >= param_list.size() || list_index >= param_values[param_index].size())
    {
        return false; // invalid indices
    }

    if (!param_list[param_index].as_list_ || param_values[param_index].size() <= param_list[param_index].list_size_min_)
    {
        return false; // not a list parameter or min size reached
    }

    param_values[param_index].erase(param_values[param_index].begin() + list_index);
    command_data_json_in_sync_ = false;

    return true;
}


void ExistingCommand::setCommandDataJson(std::string&& json)
{
    command_data_json_ = std::move(json);
    has_command_data_json_ = true;
    command_data_json_in_sync_ = true;
}