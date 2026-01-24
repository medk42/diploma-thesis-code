#pragma once

#include "module_common/module_interface_.h"
#include "module_helpers/parameter_description/parameter_description.h"
#include "helper_types.h"
#include "module_helpers/serialization_helper/serialization_helper.h"
#include "usecase_module_interface.h"

#include <cstdint>
#include <vector>

namespace aergo::module::helpers::usecase_wrapper
{
    namespace p_desc = aergo::module::helpers::parameter_description;

    namespace serialize
    {
        namespace ser = aergo::module::helpers::serialization_helper::serialization;


        /// @brief Push a string to buffer [u64 str_len][str_len * u8 str]
        template <typename ByteT>
        inline void pushString(std::vector<ByteT>& buf, const char* str, size_t str_len)
        {
            static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");

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
        template <typename ByteT>
        inline bool pushMessage(std::vector<ByteT>& buf, const aergo::module::message::MessageHeader& header)
        {
            static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");

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
        /// @param param_name name of the usecase parameter set
        /// @param param_name_len length of the param_name string
        /// @param param_desc description of the usecase parameter set
        /// @param param_desc_len length of the param_desc string
        /// @param auto_parameters parameters that are set from the input Consumer channels (subscribe/request) - only CUSTOM type allowed (value or list of values)
        /// @param required_parameters parameters that are required to be set by the user before activation - any non-CUSTOM type allowed (value or list of values)
        /// @param advanced_parameters parameters that are optional to set by the user before activation - any non-CUSTOM type allowed, need to have default value (value or list of values)
        template <typename ByteT>
        inline void pushParameters(
            std::vector<ByteT>& buf,
            const char* module_type_identifier,
            size_t module_type_identifier_len,
            const char* param_name,
            size_t param_name_len,
            const char* param_desc,
            size_t param_desc_len,
            p_desc::ParameterList& auto_parameters,
            p_desc::ParameterList& required_parameters,
            p_desc::ParameterList& advanced_parameters
        )
        {
            static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");

            pushString(buf, module_type_identifier, module_type_identifier_len); // [u64 module_type_identifier_len, module_type_identifier_len * u8 module_type_identifier]
            pushString(buf, param_name, param_name_len); // [u64 param_name_len, param_name_len * u8 param_name]
            pushString(buf, param_desc, param_desc_len); // [u64 param_desc_len, param_desc_len * u8 param_desc]

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
        template <typename ByteT>
        inline void pushParameterValues(
            std::vector<ByteT>& buf,
            const std::vector<std::vector<helper::ParameterTypeValue>>& parameter_values
        )
        {
            static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");

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



        /// @brief Push error info to buffer: [bool has_details_] if (has_details_) { [bool is_exception_][u32 error_code_][u64 error_message_len][error_message_len * u8 error_message] }
        template <typename ByteT>
        inline void pushErrorInfo(std::vector<ByteT>& buf, const helper::ErrorInfo& error_info)
        {
            static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");

            ser::push<bool>(buf, error_info.has_details_); // [bool has_details_]
            if (error_info.has_details_)
            {
                ser::push<bool>(buf, error_info.is_exception_); // [bool is_exception_]
                ser::push<uint32_t>(buf, error_info.error_code_); // [u32 error_code_]
                pushString(buf, error_info.error_message_.c_str(), error_info.error_message_.size()); // [u64 error_message_len, error_message_len * u8 error_message]
            }
        }

        /// @brief Push Vector3 to buffer: [double x][double y][double z]
        template<typename ByteT>
        inline void pushUsecaseVector3(
            std::vector<ByteT>& buf,
            const IUsecaseModule::Vector3& vec
        )
        {
            static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");

            ser::push<double>(buf, vec.x); // [double x]
            ser::push<double>(buf, vec.y); // [double y]
            ser::push<double>(buf, vec.z); // [double z]
        }

        /// @brief Push Quaternion to buffer: [double qx][double qy][double qz][double qw]
        template<typename ByteT>
        inline void pushUsecaseQuaternion(
            std::vector<ByteT>& buf,
            const IUsecaseModule::Quaternion& quat
        )
        {
            static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");

            ser::push<double>(buf, quat.qx); // [double qx]
            ser::push<double>(buf, quat.qy); // [double qy]
            ser::push<double>(buf, quat.qz); // [double qz]
            ser::push<double>(buf, quat.qw); // [double qw]
        }

        /// @brief Push Pose to buffer: [Vector3 position][Quaternion orientation]
        template<typename ByteT>
        inline void pushUsecasePose(
            std::vector<ByteT>& buf,
            const IUsecaseModule::Pose& pose
        )
        {
            static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");

            pushUsecaseVector3(buf, pose.position);    // [Vector3 position]
            pushUsecaseQuaternion(buf, pose.orientation); // [Quaternion orientation]
        }

        /// @brief Push UsecaseVisualization to buffer:
        /// [bool supports_visualization]
        /// if (supports_visualization) {
        ///     [u64 pose_count]
        ///     repeat pose_count times:
        ///         [Pose]
        ///     [u64 point_count]
        ///     repeat point_count times:
        ///         [Vector3]
        ///     [u64 trajectory_count]
        ///     repeat trajectory_count times:
        ///         [u64 trajectory_point_count]
        ///         repeat trajectory_point_count times:
        ///             [Vector3]
        template <typename ByteT>
        inline void pushUsecaseVisualization(
            std::vector<ByteT>& buf,
            const IUsecaseModule::UsecaseVisualization& visualization_info
        )
        {
            static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");

            ser::push<bool>(buf, visualization_info.supports_visualization); // [bool supports_visualization]
            if (visualization_info.supports_visualization)
            {
                ser::push<uint64_t>(buf, static_cast<uint64_t>(visualization_info.poses.size())); // [u64 pose_count]
                for (const auto& pose : visualization_info.poses)
                {
                    pushUsecasePose(buf, pose); // [Pose]
                }
                ser::push<uint64_t>(buf, static_cast<uint64_t>(visualization_info.points.size())); // [u64 point_count]
                for (const auto& point : visualization_info.points)
                {
                    pushUsecaseVector3(buf, point); // [Vector3]
                }
                ser::push<uint64_t>(buf, static_cast<uint64_t>(visualization_info.trajectories.size())); // [u64 trajectory_count]
                for (const auto& trajectory : visualization_info.trajectories)
                {
                    ser::push<uint64_t>(buf, static_cast<uint64_t>(trajectory.size())); // [u64 trajectory_point_count]
                    for (const auto& traj_point : trajectory)
                    {
                        pushUsecaseVector3(buf, traj_point); // [Vector3]
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
        inline bool readString(des::BufferReader& reader, std::string& out_str, size_t max_str_len = 16 * 1024 * 1024)
        {
            uint64_t str_len = 0;
            if (!reader.read<uint64_t>(str_len))
            {
                return false; // failed to read str_len
            }
            if (str_len > max_str_len)
            {
                return false; // str_len exceeds maximum allowed length
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
        /// @param out_param_name name of the usecase parameter set
        /// @param out_param_desc description of the usecase parameter set
        /// @param out_auto_parameters parameters that are set from the input Consumer channels (subscribe/request) - only CUSTOM type allowed (value or list of values)
        /// @param out_required_parameters parameters that are required to be set by the user before activation - any non-CUSTOM type allowed (value or list of values)
        /// @param out_advanced_parameters parameters that are optional to set by the user before activation - any non-CUSTOM type allowed, need to have default value (value or list of values)
        inline bool readParameters(
            des::BufferReader& reader,
            std::string& out_module_type_identifier,
            std::string& out_param_name,
            std::string& out_param_desc,
            p_desc::ParameterList& out_auto_parameters,
            p_desc::ParameterList& out_required_parameters,
            p_desc::ParameterList& out_advanced_parameters
        )
        {
            if (!readString(reader, out_module_type_identifier))
            {
                return false; // failed to read module_type_identifier
            }

            if (!readString(reader, out_param_name))
            {
                return false; // failed to read param_name
            }

            if (!readString(reader, out_param_desc))
            {
                return false; // failed to read param_desc
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


        /// @brief Read error info from buffer: [bool has_details_] if (has_details_) { [bool is_exception_][u32 error_code_][u64 error_message_len][error_message_len * u8 error_message] }
        inline bool readErrorInfo(des::BufferReader& reader, helper::ErrorInfo& out_error_info)
        {
            if (!reader.read<bool>(out_error_info.has_details_))
            {
                return false; // failed to read has_details_
            }
            if (out_error_info.has_details_)
            {
                if (!reader.read<bool>(out_error_info.is_exception_))
                {
                    return false; // failed to read is_exception_
                }
                if (!reader.read<uint32_t>(out_error_info.error_code_))
                {
                    return false; // failed to read error_code_
                }
                if (!readString(reader, out_error_info.error_message_))
                {
                    return false; // failed to read error_message_
                }
            }
            else
            {
                // no details, return
                out_error_info.is_exception_ = false;
                out_error_info.error_code_ = 0;
                out_error_info.error_message_.clear();
            }
            return true;
        }

        /// @brief Read Vector3 from buffer: [double x][double y][double z]
        inline bool readUsecaseVector3(
            des::BufferReader& reader,
            IUsecaseModule::Vector3& out_vec
        )
        {
            if (!reader.read<double>(out_vec.x)) return false; // failed to read x
            if (!reader.read<double>(out_vec.y)) return false; // failed to read y
            if (!reader.read<double>(out_vec.z)) return false; // failed to read z
            return true;
        }

        /// @brief Read Quaternion from buffer: [double qx][double qy][double qz][double qw]
        inline bool readUsecaseQuaternion(
            des::BufferReader& reader,
            IUsecaseModule::Quaternion& out_quat
        )
        {
            if (!reader.read<double>(out_quat.qx)) return false; // failed to read qx
            if (!reader.read<double>(out_quat.qy)) return false; // failed to read qy
            if (!reader.read<double>(out_quat.qz)) return false; // failed to read qz
            if (!reader.read<double>(out_quat.qw)) return false; // failed to read qw
            return true;
        }

        /// @brief Read Pose from buffer: [Vector3 position][Quaternion orientation]
        inline bool readUsecasePose(
            des::BufferReader& reader,
            IUsecaseModule::Pose& out_pose
        )
        {
            if (!readUsecaseVector3(reader, out_pose.position)) return false; // failed to read position
            if (!readUsecaseQuaternion(reader, out_pose.orientation)) return false; // failed to read orientation
            return true;
        }

        /// @brief Read UsecaseVisualization from buffer:
        /// [bool supports_visualization]
        /// if (supports_visualization) {
        ///     [u64 pose_count]
        ///     repeat pose_count times:
        ///         [Pose]
        ///     [u64 point_count]
        ///     repeat point_count times:
        ///         [Vector3]
        ///     [u64 trajectory_count]
        ///     repeat trajectory_count times:
        ///         [u64 trajectory_point_count]
        ///         repeat trajectory_point_count times:
        ///             [Vector3]
        inline bool readUsecaseVisualization(
            des::BufferReader& reader,
            IUsecaseModule::UsecaseVisualization& out_visualization_info
        )
        {
            if (!reader.read<bool>(out_visualization_info.supports_visualization))
            {
                return false; // failed to read supports_visualization
            }
            if (out_visualization_info.supports_visualization)
            {
                uint64_t pose_count = 0;
                if (!reader.read<uint64_t>(pose_count))
                {
                    return false; // failed to read pose_count
                }
                out_visualization_info.poses.clear();
                out_visualization_info.poses.resize(pose_count);
                for (uint64_t i = 0; i < pose_count; ++i)
                {
                    if (!readUsecasePose(reader, out_visualization_info.poses[i]))
                    {
                        return false; // failed to read Pose
                    }
                }

                uint64_t point_count = 0;
                if (!reader.read<uint64_t>(point_count))
                {
                    return false; // failed to read point_count
                }
                out_visualization_info.points.clear();
                out_visualization_info.points.resize(point_count);
                for (uint64_t i = 0; i < point_count; ++i)
                {
                    if (!readUsecaseVector3(reader, out_visualization_info.points[i]))
                    {
                        return false; // failed to read Vector3
                    }
                }

                uint64_t trajectory_count = 0;
                if (!reader.read<uint64_t>(trajectory_count))
                {
                    return false; // failed to read trajectory_count
                }
                out_visualization_info.trajectories.clear();
                out_visualization_info.trajectories.resize(trajectory_count);
                for (uint64_t i = 0; i < trajectory_count; ++i)
                {
                    uint64_t trajectory_point_count = 0;
                    if (!reader.read<uint64_t>(trajectory_point_count))
                    {
                        return false; // failed to read trajectory_point_count
                    }
                    out_visualization_info.trajectories[i].clear();
                    out_visualization_info.trajectories[i].resize(trajectory_point_count);
                    for (uint64_t j = 0; j < trajectory_point_count; ++j)
                    {
                        if (!readUsecaseVector3(reader, out_visualization_info.trajectories[i][j]))
                        {
                            return false; // failed to read Vector3
                        }
                    }
                }
            }
            else
            {
                // no visualization data
                out_visualization_info.poses.clear();
                out_visualization_info.points.clear();
                out_visualization_info.trajectories.clear();
            }
            return true;
        }
    }
}