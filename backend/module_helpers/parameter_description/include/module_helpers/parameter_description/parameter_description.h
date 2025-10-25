#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
#include <variant>
#include <optional>



namespace aergo::module::helpers::parameter_description
{
    using ParameterValue = std::variant<
        bool,                // boolean
        int64_t,             // long
        double,              // double
        std::string,         // string
        int32_t,             // enum index (or -1 for no selection)
        std::vector<uint8_t> // custom channel data
    >;

    using ParameterValueOpt = std::optional<ParameterValue>;
    using ParameterValueOptList = std::vector<ParameterValueOpt>;
    using ParameterValueOptListList = std::vector<ParameterValueOptList>;

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
        ParameterType type_ = ParameterType::BOOL;
        
        std::string param_name_;
        std::string param_desc_;

        bool limit_min_ = false, limit_max_ = false;    // with limit checking
        double min_value_double_ = 0, max_value_double_ = 1;  // min/max double value
        int64_t min_value_long_ = 0, max_value_long_ = 100; // min/max long value
        bool as_slider_ = false;                // display value as a slider, for DOUBLE only, requires min_value_double_ and max_value_double_

        std::vector<std::string> enum_values_;   // has to be non-empty for ENUM type

        CustomChannelType custom_channel_type_ = CustomChannelType::SUBSCRIBE;
        uint32_t custom_channel_id_ = 0;

        bool as_list_ = false;
        uint16_t list_size_min_ = 0; // only used if as_list_ is true, no limits if 0
        uint16_t list_size_max_ = 0; // only used if as_list_ is true, no limits if 0

        std::string default_value_; // default value as a string (for all types, except CUSTOM; for lists, value is used for all entries), for enum it is the enum string value (if empty, first enum value is used)
        

        /// @brief Parse the default value string into ParameterValue. If no default value is set or parsing fails, returns std::nullopt.
        std::optional<ParameterValue> parseDefaultValue() const;
        void toStringStream(std::stringstream& stream);
        static ParameterDescription fromStringStream(std::stringstream& stream);
    };

    class ParameterList
    {
    public:
        ParameterList() = default;
        ParameterList(std::vector<ParameterDescription>&& parameters);

        std::string toString();
        static ParameterList fromString(std::string& parameters_str); // can throw runtime_error on parsing error

        const std::vector<ParameterDescription>& getParameters() const;
        ParameterValueOptListList buildParameterValues() const; // builds empty parameter values based on the parameter descriptions, with default values set where applicable and list sizes allocated to minimum size

    private:
        std::vector<ParameterDescription> parameters_;
        std::string cached_string_;
    };
}