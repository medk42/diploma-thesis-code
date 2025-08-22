#include "module_helpers/activation_wrapper/parameter_description.h"

#define PARAM_DESC_VERSION 1

#define CHECKED_READ(read_call) if (!(read_call)) throw std::runtime_error("Invalid parameter description.");


using namespace aergo::module::helpers::activation_wrapper::params;



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
    CHECKED_READ(stream.read(&description.param_name_[0], param_name_length))
    CHECKED_READ(stream >> param_desc_length)
    description.param_desc_.resize(param_desc_length);
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
    CHECKED_READ(stream.read(&description.default_value_[0], default_value_length))

    return description;
}



ParameterList::ParameterList(std::vector<ParameterDescription>&& parameters)
: parameters_(std::move(parameters)) {}



std::string ParameterList::toString()
{
    if (cashed_string_.empty())
    {
        std::stringstream stream;
        stream.precision(std::numeric_limits<double>::max_digits10);

        stream << PARAM_DESC_VERSION << " ";
        stream << parameters_.size() << " ";

        for (auto& param : parameters_)
        {
            param.toStringStream(stream);
        }

        cashed_string_ = stream.str();
    }

    return cashed_string_;
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
