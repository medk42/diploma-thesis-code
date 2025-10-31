#pragma once

#include <string>
#include <map>

#include "module_helpers/parameter_description/parameter_description.h"
#include "module_common/module_interface_.h"

namespace aergo::module::helpers::usecase_tree::structs
{
    namespace p_desc = aergo::module::helpers::parameter_description;


    
    class AvailableUsecase
    {
    public:
        AvailableUsecase(std::string usecase_identifier,
                         std::string usecase_name,
                         std::string usecase_desc,
                         aergo::module::helpers::parameter_description::ParameterList&& auto_parameters,
                         aergo::module::helpers::parameter_description::ParameterList&& required_parameters,
                         aergo::module::helpers::parameter_description::ParameterList&& advanced_parameters,
                         aergo::module::ChannelIdentifier communication_channel)
            : usecase_identifier_(std::move(usecase_identifier)),
              usecase_name_(std::move(usecase_name)),
              usecase_desc_(std::move(usecase_desc)),
              auto_parameters_(std::move(auto_parameters)),
              required_parameters_(std::move(required_parameters)),
              advanced_parameters_(std::move(advanced_parameters)),
              communication_channel_(communication_channel)
              {}

        const std::string& getUsecaseIdentifier() const { return usecase_identifier_; }
        const std::string& getUsecaseName() const { return usecase_name_; }
        const std::string& getUsecaseDesc() const { return usecase_desc_; }
        const aergo::module::helpers::parameter_description::ParameterList& getAutoParameters() const { return auto_parameters_; }
        const aergo::module::helpers::parameter_description::ParameterList& getRequiredParameters() const { return required_parameters_; }
        const aergo::module::helpers::parameter_description::ParameterList& getAdvancedParameters() const { return advanced_parameters_; }
        const aergo::module::ChannelIdentifier& getCommunicationChannel() const { return communication_channel_; }

    private:
        std::string usecase_identifier_;
        std::string usecase_name_;
        std::string usecase_desc_;
        aergo::module::helpers::parameter_description::ParameterList auto_parameters_;   
        aergo::module::helpers::parameter_description::ParameterList required_parameters_;
        aergo::module::helpers::parameter_description::ParameterList advanced_parameters_;
        aergo::module::ChannelIdentifier communication_channel_;
    };



    class ExistingCommand
    {
    public:
        enum class ParamType { AUTO, REQUIRED, ADVANCED };

        ExistingCommand(
            std::string usecase_identifier, 
            std::string usecase_name,
            uint64_t command_id, 
            const std::map<std::string, AvailableUsecase>* available_usecases_map,
            p_desc::ParameterValueOptListList&& auto_parameter_values, 
            p_desc::ParameterValueOptListList&& required_parameter_values, 
            p_desc::ParameterValueOptListList&& advanced_parameter_values,
            std::optional<std::string> command_data_json = std::nullopt,
            bool command_data_json_in_sync = false
        );

        ExistingCommand(ExistingCommand&& other, uint64_t new_command_id);

        const std::string& getUsecaseIdentifier() const { return usecase_identifier_; }
        const std::string& getUsecaseName() const { return usecase_name_; }
        uint64_t getCommandId() const { return command_id_; }
        const p_desc::ParameterValueOptListList& getParameterValues(ParamType type) const;
        const p_desc::ParameterList* getParameters(ParamType type) const; // returns nullptr if usecase_identifier_ not found in available_usecases_map_
        const std::string& getCommandDataJson() const { return command_data_json_; }
        const bool hasCommandDataJson() const { return has_command_data_json_; }
        const bool isCommandDataJsonInSync() const { return command_data_json_in_sync_; }
        std::optional<p_desc::ParameterValueOpt> getValue(ParamType type, size_t param_index, size_t list_index) const; // returns std::nullopt if value out of range

        const AvailableUsecase* getUsecaseReference() const // returns pointer to AvailableUsecase if usecase_identifier_ exists in available_usecases_map_, nullptr otherwise
        {
            auto it = available_usecases_map_->find(usecase_identifier_);
            if (it == available_usecases_map_->end())
            {
                return nullptr;
            }
            return &it->second;
        }

        bool setValue(ParamType type, size_t param_index, size_t list_index, const p_desc::ParameterValue& value);
        bool addValue(ParamType type, size_t param_index, const p_desc::ParameterValueOpt& value);
        bool resetValue(ParamType type, size_t param_index, size_t list_index); // only for AUTO parameters (CUSTOM parameter type)
        bool removeValue(ParamType type, size_t param_index, size_t list_index);

        void setCommandDataJson(std::string&& json);

    private:
        p_desc::ParameterValueOptListList& getParameterValues(ParamType type);

        friend class UsecaseTree;

        const std::map<std::string, AvailableUsecase>* available_usecases_map_;

        std::string usecase_identifier_;
        std::string usecase_name_;
        uint64_t command_id_;
        p_desc::ParameterValueOptListList auto_parameter_values_;
        p_desc::ParameterValueOptListList required_parameter_values_;
        p_desc::ParameterValueOptListList advanced_parameter_values_;

        bool command_data_json_in_sync_ = false; // indicates whether command_data_json_ matches the parameter values
        bool has_command_data_json_ = false; // indicates whether command_data_json_ has been generated
        std::string command_data_json_;
    };
}