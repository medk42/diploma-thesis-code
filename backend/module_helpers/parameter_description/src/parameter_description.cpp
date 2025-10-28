#include "module_helpers/parameter_description/parameter_description.h"

#include <limits>
#include <string_view>

#include "websocketpp/base64.hpp"

#define PARAM_DESC_VERSION 1

#define CHECKED_READ(read_call) if (!(read_call)) throw std::runtime_error("Invalid parameter description.");


using namespace aergo::module::helpers::parameter_description;



ParameterValueOpt string_conversions::stringToParameterValue(const std::string& str, ParameterType type)
{
    if (str.empty())
    {
        return std::nullopt;
    }

    try
    {
        switch (type)
        {
            case ParameterType::BOOL:
            {
                if (str == "1")
                {
                    return ParameterValue(true);
                }
                else if (str == "0")
                {
                    return ParameterValue(false);
                }
                break;
            }
            case ParameterType::LONG:
            {
                int64_t value = std::stoll(str);
                return ParameterValue(value);
            }
            case ParameterType::DOUBLE:
            {
                double value = std::stod(str);
                return ParameterValue(value);
            }
            case ParameterType::STRING:
            {
                return ParameterValue(str);
            }
            case ParameterType::ENUM:
            {
                int32_t index = std::stoi(str);
                return ParameterValue(index);
            }
            case ParameterType::CUSTOM:
            {
                std::string decoded = websocketpp::base64_decode(str);
                std::vector<uint8_t> data(decoded.begin(), decoded.end());
                return ParameterValue(data);
            }
        }
    }
    catch (...)
    {
        // parsing error
    }

    return std::nullopt;
}


ParameterValueOpt string_conversions::parseDefaultValue(const ParameterDescription& param_desc)
{
    auto value = string_conversions::stringToParameterValue(param_desc.default_value_, param_desc.type_);
    if (param_desc.checkValid(value))
    {
        return value;
    }
    else
    {
        return std::nullopt;
    }
}


std::optional<std::string> string_conversions::parameterValueToString(const ParameterValue& value)
{
    if (std::holds_alternative<bool>(value))
    {
        return std::get<bool>(value) ? "1" : "0";
    }
    else if (std::holds_alternative<int64_t>(value))
    {
        return std::to_string(std::get<int64_t>(value));
    }
    else if (std::holds_alternative<double>(value))
    {
        return std::to_string(std::get<double>(value));
    }
    else if (std::holds_alternative<std::string>(value))
    {
        return std::get<std::string>(value);
    }
    else if (std::holds_alternative<int32_t>(value))
    {
        return std::to_string(std::get<int32_t>(value));
    }
    else if (std::holds_alternative<std::vector<uint8_t>>(value))
    {
        const auto& data = std::get<std::vector<uint8_t>>(value);
        return websocketpp::base64_encode(reinterpret_cast<unsigned char const *>(data.data()), data.size());
    }
    
    return std::nullopt;
}


bool ParameterDescription::checkValid(const ParameterValueOpt& value) const
{
    if (!value)
    {
        return false;
    }
    
    switch (type_)
    {
        case ParameterType::BOOL:
        {
            if (!std::holds_alternative<bool>(*value))
            {
                return false;
            }
            break;
        }
        case ParameterType::LONG:
        {
            if (!std::holds_alternative<int64_t>(*value))
            {
                return false;
            }
            int64_t val = std::get<int64_t>(*value);
            if (limit_min_ && val < min_value_long_)
            {
                return false;
            }
            if (limit_max_ && val > max_value_long_)
            {
                return false;
            }
            break;
        }
        case ParameterType::DOUBLE:
        {
            if (!std::holds_alternative<double>(*value))
            {
                return false;
            }
            double val =  std::get<double>(*value);
            if (limit_min_ && val < min_value_double_)
            {
                return false;
            }
            if (limit_max_ && val > max_value_double_)
            {
                return false;
            }
            break;
        }
        case ParameterType::STRING:
        {
            if (!std::holds_alternative<std::string>(*value))
            {
                return false;
            }
            break;
        }
        case ParameterType::ENUM:
        {
            if (!std::holds_alternative<int32_t>(*value))
            {
                return false;
            }
            int32_t index = std::get<int32_t>(*value);
            if (index < 0 || static_cast<size_t>(index) >= enum_values_.size())
            {
                return false;
            }
            break;
        }
        case ParameterType::CUSTOM:
        {
            if (!std::holds_alternative<std::vector<uint8_t>>(*value))
            {
                return false;
            }
            break;
        }
    }

    return true;
}



void ParameterDescription::toStringStream(std::stringstream& stream)
{
    stream << (size_t)type_ << " ";
    stream << param_name_.size() << " ";
    stream << param_name_;
    stream << param_desc_.size() << " ";
    stream << param_desc_;
    stream << limit_min_ << " ";
    stream << limit_max_ << " ";
    stream << min_value_double_ << " ";
    stream << max_value_double_ << " ";
    stream << min_value_long_ << " ";
    stream << max_value_long_ << " ";
    stream << as_slider_ << " ";
    stream << enum_values_.size() << " ";
    for (auto& enum_value : enum_values_)
    {
        stream << enum_value.size() << " ";
        stream << enum_value;
    }
    stream << (size_t)custom_channel_type_ << " ";
    stream << custom_channel_id_ << " ";
    stream << as_list_ << " ";
    stream << list_size_min_ << " ";
    stream << list_size_max_ << " ";
    stream << default_value_.size() << " ";
    stream << default_value_;

}



ParameterDescription ParameterDescription::fromStringStream(std::stringstream& stream)
{
    ParameterDescription description;

    size_t param_type, param_name_length, param_desc_length, enum_values_size, custom_channel_type, default_value_length;

    CHECKED_READ(stream >> param_type)
    description.type_ = (ParameterType)param_type;
    CHECKED_READ(stream >> param_name_length)
    description.param_name_.resize(param_name_length);
    stream >> std::ws; // eat whitespace
    CHECKED_READ(stream.read(&description.param_name_[0], param_name_length))
    CHECKED_READ(stream >> param_desc_length)
    description.param_desc_.resize(param_desc_length);
    stream >> std::ws; // eat whitespace
    CHECKED_READ(stream.read(&description.param_desc_[0], param_desc_length))
    CHECKED_READ(stream >> description.limit_min_)
    CHECKED_READ(stream >> description.limit_max_)
    CHECKED_READ(stream >> description.min_value_double_)
    CHECKED_READ(stream >> description.max_value_double_)
    CHECKED_READ(stream >> description.min_value_long_)
    CHECKED_READ(stream >> description.max_value_long_)
    CHECKED_READ(stream >> description.as_slider_)
    CHECKED_READ(stream >> enum_values_size)
    for (size_t i = 0; i < enum_values_size; ++i)
    {
        std::string enum_value;
        size_t enum_value_size;
        CHECKED_READ(stream >> enum_value_size)
        enum_value.resize(enum_value_size);
        stream >> std::ws; // eat whitespace
        CHECKED_READ(stream.read(&enum_value[0], enum_value_size))
        description.enum_values_.push_back(std::move(enum_value));
    }

    CHECKED_READ(stream >> custom_channel_type)
    description.custom_channel_type_ = (CustomChannelType)custom_channel_type;
    CHECKED_READ(stream >> description.custom_channel_id_)
    CHECKED_READ(stream >> description.as_list_)
    CHECKED_READ(stream >> description.list_size_min_)
    CHECKED_READ(stream >> description.list_size_max_)
    CHECKED_READ(stream >> default_value_length)
    description.default_value_.resize(default_value_length);
    stream >> std::ws; // eat whitespace
    CHECKED_READ(stream.read(&description.default_value_[0], default_value_length))

    return description;
}



ParameterList::ParameterList(std::vector<ParameterDescription>&& parameters)
: parameters_(std::move(parameters)) {}



std::string ParameterList::toString()
{
    if (cached_string_.empty())
    {
        std::stringstream stream;
        stream.precision(std::numeric_limits<double>::max_digits10);

        stream << PARAM_DESC_VERSION << " ";
        stream << parameters_.size() << " ";

        for (auto& param : parameters_)
        {
            param.toStringStream(stream);
        }

        cached_string_ = stream.str();
    }

    return cached_string_;
}



ParameterList ParameterList::fromString(std::string& parameters_str)
{
    std::stringstream input_stream(parameters_str);

    std::vector<ParameterDescription> parameters;
    size_t reported_param_desc_version, parameter_count;

    if (!(input_stream >> reported_param_desc_version))
    {
        throw std::runtime_error("Invalid parameter description version");
    }

    if (reported_param_desc_version != PARAM_DESC_VERSION)
    {
        throw std::runtime_error("Mismatched parameter description version");
    }

    if (!(input_stream >> parameter_count))
    {
        throw std::runtime_error("Invalid parameter count");
    }

    for (size_t i = 0; i < parameter_count; ++i)
    {
        parameters.push_back(ParameterDescription::fromStringStream(input_stream));
    }

    return ParameterList(std::move(parameters));
}



const std::vector<ParameterDescription>& ParameterList::getParameters() const
{
    return parameters_;
}



ParameterValueOptListList ParameterList::buildParameterValues() const
{
    ParameterValueOptListList parameter_values;
    parameter_values.resize(parameters_.size());

    for (size_t param_index = 0; param_index < parameters_.size(); ++param_index)
    {
        const ParameterDescription& param_desc = parameters_[param_index];
        size_t list_size = 1;
        if (param_desc.as_list_)
        {
            list_size = param_desc.list_size_min_;
        }

        parameter_values[param_index].resize(list_size);

        std::optional<ParameterValue> default_value = string_conversions::parseDefaultValue(param_desc);
        if (default_value.has_value())
        {
            for (size_t list_index = 0; list_index < list_size; ++list_index)
            {
                parameter_values[param_index][list_index] = default_value.value();
            }
        }
    }

    return parameter_values;
}