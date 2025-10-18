#pragma once

#include "module_common/module_interface_.h"
#include "module_helpers/parameter_description/parameter_description.h"
#include "helper_types.h"
#include "module_helpers/serialization_helper/serialization_helper.h"

#include <cstdint>
#include <vector>

namespace aergo::module::helpers::usecase_wrapper
{
    namespace p_desc = aergo::module::helpers::parameter_description;

    namespace serialize
    {
        namespace ser = aergo::module::helpers::serialization_helper::serialization;


        /// @brief Push a string to buffer [u64 str_len][str_len * u8 str]
        inline void pushString(std::vector<uint8_t>& buf, const char* str, size_t str_len)
        {
            ser::push<uint64_t>(buf, static_cast<uint64_t>(str_len)); // [u64 str_len]
            if (str_len > 0)
            {
                ser::pushBytes(buf, str, str_len); // [str_len * u8 str]
            }
        }


        /// @brief Push a message to buffer:
        /// u64 data_len_
        /// [data_len_ * u8 data_]
        /// u64 blob_count_
        /// repeat blob_count_ times:
        ///     u64 blob_size
        ///     [blob_size * u8 blob_data]
        inline bool pushMessage(std::vector<uint8_t>& buf, const aergo::module::message::MessageHeader& header)
        {
            if (!header.success_ || (header.data_len_ > 0 && header.data_ == nullptr) || (header.blob_count_ > 0 && header.blobs_ == nullptr))
            {
                return false; // invalid message
            }

            for (uint64_t blob_id = 0; blob_id < header.blob_count_; ++blob_id)
            {
                if (!header.blobs_[blob_id].valid() || header.blobs_[blob_id].data() == nullptr)
                {
                    return false; // invalid blob
                }
            }

            ser::push<uint64_t>(buf, header.data_len_); // [u64 data_len_]
            if (header.data_len_ > 0)
            {
                ser::pushBytes(buf, header.data_, header.data_len_); // [data_len_ * u8 data_]
            }

            ser::push<uint64_t>(buf, header.blob_count_); // [u64 blob_count_]
            for (uint64_t blob_id = 0; blob_id < header.blob_count_; ++blob_id)
            {
                uint64_t blob_size = header.blobs_[blob_id].size();
                uint8_t* blob_data = header.blobs_[blob_id].data();

                ser::push<uint64_t>(buf, blob_size); // [u64 blob_size]
                if (blob_size > 0)
                {
                    ser::pushBytes(buf, blob_data, blob_size); // [blob_size * u8 blob_data]
                }
            }

            return true;
        }


        /// @brief Push module identifier and usecase parameters to buffer: [4 * string]
        /// @param module_type_identifier unique identifier of the module type to identify this usecase module
        /// @param module_type_identifier_len length of the module_type_identifier string
        /// @param auto_parameters parameters that are set from the input Consumer channels (subscribe/request) - only CUSTOM type allowed (value or list of values)
        /// @param required_parameters parameters that are required to be set by the user before activation - any non-CUSTOM type allowed (value or list of values)
        /// @param advanced_parameters parameters that are optional to set by the user before activation - any non-CUSTOM type allowed, need to have default value (value or list of values)
        inline void pushParameters(
            std::vector<uint8_t>& buf,
            const char* module_type_identifier,
            size_t module_type_identifier_len,
            p_desc::ParameterList& auto_parameters,
            p_desc::ParameterList& required_parameters,
            p_desc::ParameterList& advanced_parameters
        )
        {
            pushString(buf, module_type_identifier, module_type_identifier_len); // [u64 module_type_identifier_len, module_type_identifier_len * u8 module_type_identifier]

            std::string auto_parameters_str = auto_parameters.toString();
            pushString(buf, auto_parameters_str.c_str(), auto_parameters_str.size()); // [u64 auto_parameters_str_len, auto_parameters_str_len * u8 auto_parameters_str]

            std::string required_parameters_str = required_parameters.toString();
            pushString(buf, required_parameters_str.c_str(), required_parameters_str.size()); // [u64 required_parameters_str_len, required_parameters_str_len * u8 required_parameters_str]

            std::string advanced_parameters_str = advanced_parameters.toString();
            pushString(buf, advanced_parameters_str.c_str(), advanced_parameters_str.size()); // [u64 advanced_parameters_str_len, advanced_parameters_str_len * u8 advanced_parameters_str]
        }


        /// @brief Push parameter values to buffer:
        /// u64 parameter_count
        /// repeat parameter_count times:
        ///     u64 list_size
        ///     repeat list_size times:
        ///         u64 ParameterType
        ///         value (based on ParameterType)
        ///              ParameterType::BOOL:   u8 bool_value
        ///              ParameterType::LONG:   i64 long_value
        ///              ParameterType::DOUBLE: double double_value
        ///              ParameterType::STRING: u64 str_len, str_len * u8 str
        ///              ParameterType::ENUM:   i32 enum_index
        ///              ParameterType::CUSTOM: u64 custom_data_len, custom_data_len * u8 custom_data
        inline void pushParameterValues(
            std::vector<uint8_t>& buf,
            const std::vector<std::vector<helper::ParameterTypeValue>>& parameter_values
        )
        {
            ser::push<uint64_t>(buf, static_cast<uint64_t>(parameter_values.size())); // [u64 parameter_count]
            for (const auto& param_value : parameter_values)
            {
                ser::push<uint64_t>(buf, static_cast<uint64_t>(param_value.size())); // [u64 list_size]
                for (const auto& val : param_value)
                {
                    ser::push<uint64_t>(buf, static_cast<uint64_t>(val.type_)); // [u64 ParameterType]
                    switch (val.type_)
                    {
                        case p_desc::ParameterType::BOOL:
                            ser::push<bool>(buf, std::get<bool>(val.value_)); // [u8 bool_value]
                            break;
                        case p_desc::ParameterType::LONG:
                            ser::push<int64_t>(buf, std::get<int64_t>(val.value_)); // [i64 long_value]
                            break;
                        case p_desc::ParameterType::DOUBLE:
                            ser::push<double>(buf, std::get<double>(val.value_)); // [double double_value]
                            break;
                        case p_desc::ParameterType::STRING:
                            {
                                const std::string& str = std::get<std::string>(val.value_);
                                pushString(buf, str.c_str(), str.size()); // [u64 str_len, str_len * u8 str]
                            }
                            break;
                        case p_desc::ParameterType::ENUM:
                            ser::push<int32_t>(buf, std::get<int32_t>(val.value_)); // [i32 enum_index]
                            break;
                        case p_desc::ParameterType::CUSTOM:
                            {
                                const std::vector<uint8_t>& custom_data = std::get<std::vector<uint8_t>>(val.value_);
                                ser::push<uint64_t>(buf, static_cast<uint64_t>(custom_data.size())); // [u64 custom_data_len]
                                if (!custom_data.empty())
                                {
                                    ser::pushBytes(buf, custom_data.data(), custom_data.size()); // [custom_data_len * u8 custom_data]
                                }
                            }
                            break;
                    }
                }
            }
        }
    }

    namespace deserialize
    {
        namespace des = aergo::module::helpers::serialization_helper::deserialization;


        struct MessageData
        {
            std::vector<uint8_t> data_;
            std::vector<std::vector<uint8_t>> blobs_;
        };
        
        
        /// @brief Read a string from buffer [u64 str_len][str_len * u8 str]
        inline bool readString(des::BufferReader& reader, std::string& out_str)
        {
            uint64_t str_len = 0;
            if (!reader.read<uint64_t>(str_len))
            {
                return false; // failed to read str_len
            }
            if (str_len > 0)
            {
                out_str.resize(str_len);
                if (!reader.readBytes(out_str.data(), str_len))
                {
                    return false; // failed to read str data
                }
            }
            else
            {
                out_str.clear();
            }
            return true;
        }


        /// @brief Read a message from buffer:
        /// u64 data_len_
        /// [data_len_ * u8 data_]
        /// u64 blob_count_
        /// repeat blob_count_ times:
        ///     u64 blob_size
        ///     [blob_size * u8 blob_data]
        inline bool readMessage(des::BufferReader& reader, MessageData& out_message_data)
        {
            uint64_t data_len = 0;
            if (!reader.read<uint64_t>(data_len))
            {
                return false; // failed to read data_len
            }
            out_message_data.data_.clear();
            if (data_len > 0)
            {
                out_message_data.data_.resize(data_len);
                if (!reader.readBytes(out_message_data.data_.data(), data_len))
                {
                    return false; // failed to read data
                }
            }

            uint64_t blob_count = 0;
            if (!reader.read<uint64_t>(blob_count))
            {
                return false; // failed to read blob_count
            }
            out_message_data.blobs_.clear();
            out_message_data.blobs_.resize(blob_count);
            for (uint64_t blob_id = 0; blob_id < blob_count; ++blob_id)
            {
                uint64_t blob_size = 0;
                if (!reader.read<uint64_t>(blob_size))
                {
                    return false; // failed to read blob_size
                }

                out_message_data.blobs_[blob_id].clear();
                if (blob_size > 0)
                {
                    out_message_data.blobs_[blob_id].resize(blob_size);
                    if (!reader.readBytes(out_message_data.blobs_[blob_id].data(), blob_size))
                    {
                        return false; // failed to read blob_data
                    }
                }
            }

            return true;
        }


        /// @brief Read module identifier and usecase parameters from buffer: [4 * string]
        /// @param out_module_type_identifier unique identifier of the module type to identify this usecase module
        /// @param out_auto_parameters parameters that are set from the input Consumer channels (subscribe/request) - only CUSTOM type allowed (value or list of values)
        /// @param out_required_parameters parameters that are required to be set by the user before activation - any non-CUSTOM type allowed (value or list of values)
        /// @param out_advanced_parameters parameters that are optional to set by the user before activation - any non-CUSTOM type allowed, need to have default value (value or list of values)
        inline bool readParameters(
            des::BufferReader& reader,
            std::string& out_module_type_identifier,
            p_desc::ParameterList& out_auto_parameters,
            p_desc::ParameterList& out_required_parameters,
            p_desc::ParameterList& out_advanced_parameters
        )
        {
            if (!readString(reader, out_module_type_identifier))
            {
                return false; // failed to read module_type_identifier
            }

            std::string auto_parameters_str, required_parameters_str, advanced_parameters_str;
            if (!readString(reader, auto_parameters_str) ||
                !readString(reader, required_parameters_str) ||
                !readString(reader, advanced_parameters_str))
            {
                return false; // failed to read parameters strings
            }

            try
            {
                // throws runtime_error on parsing error
                auto auto_params = p_desc::ParameterList::fromString(auto_parameters_str);  
                auto req_params  = p_desc::ParameterList::fromString(required_parameters_str);
                auto adv_params  = p_desc::ParameterList::fromString(advanced_parameters_str);

                out_auto_parameters = std::move(auto_params);
                out_required_parameters = std::move(req_params);
                out_advanced_parameters = std::move(adv_params);
            }
            catch (const std::runtime_error&)
            {
                return false; // failed to parse parameters
            }

            return true;
        }
     
        
        /// @brief Read parameter values from buffer:
        /// u64 parameter_count
        /// repeat parameter_count times:
        ///     u64 list_size
        ///     repeat list_size times:
        ///         u64 ParameterType
        ///         value (based on ParameterType)
        ///              ParameterType::BOOL:   u8 bool_value
        ///              ParameterType::LONG:   i64 long_value
        ///              ParameterType::DOUBLE: double double_value
        ///              ParameterType::STRING: u64 str_len, str_len * u8 str
        ///              ParameterType::ENUM:   i32 enum_index
        ///              ParameterType::CUSTOM: u64 custom_data_len, custom_data_len * u8 custom_data
        inline bool readParameterValues(
            des::BufferReader& reader,
            std::vector<std::vector<helper::ParameterTypeValue>>& out_parameter_values
        )
        {
            uint64_t parameter_count = 0;
            if (!reader.read<uint64_t>(parameter_count))
            {
                return false; // failed to read parameter_count
            }

            out_parameter_values.clear();
            out_parameter_values.resize(parameter_count);
            for (uint64_t param_id = 0; param_id < parameter_count; ++param_id)
            {
                uint64_t list_size = 0;
                if (!reader.read<uint64_t>(list_size))
                {
                    return false; // failed to read list_size
                }

                out_parameter_values[param_id].clear();
                out_parameter_values[param_id].resize(list_size);
                for (uint64_t list_id = 0; list_id < list_size; ++list_id)
                {
                    uint64_t param_type_int = 0;
                    if (!reader.read<uint64_t>(param_type_int))
                    {
                        return false; // failed to read ParameterType
                    }
                    if (
                        param_type_int != static_cast<uint64_t>(p_desc::ParameterType::BOOL) 
                        && param_type_int != static_cast<uint64_t>(p_desc::ParameterType::LONG)
                        && param_type_int != static_cast<uint64_t>(p_desc::ParameterType::DOUBLE)
                        && param_type_int != static_cast<uint64_t>(p_desc::ParameterType::STRING)
                        && param_type_int != static_cast<uint64_t>(p_desc::ParameterType::ENUM)
                        && param_type_int != static_cast<uint64_t>(p_desc::ParameterType::CUSTOM)
                    )
                    {
                        return false; // invalid ParameterType
                    }

                    helper::ParameterTypeValue& param_value = out_parameter_values[param_id][list_id];
                    param_value.type_ = static_cast<p_desc::ParameterType>(param_type_int);

                    switch (param_value.type_)
                    {
                        case p_desc::ParameterType::BOOL:
                        {
                            bool bool_value = false;
                            if (!reader.read<bool>(bool_value)) return false; // failed to read bool_value
                            param_value.value_ = bool_value;
                            break;
                        }
                        case p_desc::ParameterType::LONG:
                        {
                            int64_t long_value = 0;
                            if (!reader.read<int64_t>(long_value)) return false; // failed to read long_value
                            param_value.value_ = long_value;
                            break;
                        }
                        case p_desc::ParameterType::DOUBLE:
                        {
                            double double_value = 0.0;
                            if (!reader.read<double>(double_value)) return false; // failed to read double_value
                            param_value.value_ = double_value;
                            break;
                        }
                        case p_desc::ParameterType::STRING:
                        {
                            std::string str_value;
                            if (!readString(reader, str_value)) return false; // failed to read str_value
                            param_value.value_ = std::move(str_value);
                            break;
                        }
                        case p_desc::ParameterType::ENUM:
                        {
                            int32_t enum_index = 0;
                            if (!reader.read<int32_t>(enum_index)) return false; // failed to read enum_index
                            param_value.value_ = enum_index;
                            break;
                        }
                        case p_desc::ParameterType::CUSTOM:
                        {
                            uint64_t custom_data_len = 0;
                            if (!reader.read<uint64_t>(custom_data_len))
                            {
                                return false; // failed to read custom_data_len
                            }

                            std::vector<uint8_t> custom_data;
                            if (custom_data_len > 0)
                            {
                                custom_data.resize(custom_data_len);
                                if (!reader.readBytes(custom_data.data(), custom_data_len))
                                {
                                    return false; // failed to read custom_data
                                }
                            }
                            param_value.value_ = std::move(custom_data);
                            break;
                        }
                        default:
                            return false; // should not happen
                    }
                }
            }

            return true;
        }
    }
}