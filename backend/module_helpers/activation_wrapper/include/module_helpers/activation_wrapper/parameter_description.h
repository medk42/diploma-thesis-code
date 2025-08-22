#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <sstream>



namespace aergo::module::helpers::activation_wrapper::params
{
    enum class ParameterType
    {
        BOOL,
        LONG,
        DOUBLE, 
        STRING,
        ENUM,
        CUSTOM
    };

    struct EnumValue
    {
        std::string value_name_;
    };

    enum class CustomChannelType
    {
        SUBSCRIBE, REQUEST
    };

    struct ParameterDescription
    {
        ParameterType type_;
        
        std::string param_name_;
        std::string param_desc_;

        bool limit_min_, limit_max_;    // with limit checking
        double min_value_, max_value_;  // min/max double value
        bool as_slider_;                // display value as a slider

        std::vector<std::string> enum_values_;

        CustomChannelType custom_channel_type_;
        std::string custom_channel_name_;

        bool as_list_;
        uint16_t list_size_min_, list_size_max_;

        void toStringStream(std::stringstream& stream);
        static ParameterDescription fromStringStream(std::stringstream& stream);
    };

    class ParameterList
    {
    public:
        ParameterList(std::vector<ParameterDescription>&& parameters);

        std::string toString();
        static ParameterList fromString(std::string& parameters_str); // can throw runtime_error on parsing error
        std::vector<ParameterDescription>& getParameters();

    private:
        std::vector<ParameterDescription> parameters_;
        std::string cashed_string_;
    };
}