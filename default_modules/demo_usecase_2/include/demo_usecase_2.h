#pragma once

#include "module_helpers/base_usecase/base_usecase.h"
#include "module_helpers/usecase_wrapper/helper_types.h"
#include "module_common/module_interface_.h"

#include <expected>

namespace aergo::default_modules::demo_usecase_2
{
    namespace uw = aergo::module::helpers::usecase_wrapper;

    class DemoUsecase2 : public aergo::module::helpers::base_usecase::BaseUsecase
    {
    public:
        DemoUsecase2(
            const char* data_path, 
            aergo::module::ICore* core, 
            aergo::module::InputChannelMapInfo channel_map_info, 
            const aergo::module::logging::ILogger* logger, 
            uint64_t module_id, 
            const aergo::module::ModuleInfo* module_info,
            bool supports_multi_program,
            bool supports_pause,
            bool supports_stop
        )  : BaseUsecase(
            data_path, core, channel_map_info, 
            logger, module_id, module_info,
            supports_multi_program, supports_pause, supports_stop
        ) {}

        ~DemoUsecase2() noexcept override = default;
        
        virtual std::expected<void, uw::helper::ErrorInfo> createCommandFromParameters(
            const uw::p_desc::ParameterList& auto_parameters,
            const uw::p_desc::ParameterList& required_parameters,
            const uw::p_desc::ParameterList& advanced_parameters,
            std::vector<std::vector<uw::helper::ParameterTypeValue>>& auto_parameter_values,
            std::vector<std::vector<uw::helper::ParameterTypeValue>>& required_parameter_values,
            std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
            nlohmann::json& out_command_json,
            uw::IUsecaseModule::UsecaseVisualization& out_visualization
        ) override;

        virtual std::expected<void, uw::helper::ErrorInfo> validateParameters(
            const nlohmann::json& command_json
        ) override;

        virtual std::expected<void, uw::helper::ErrorInfo> runProgram(
            const nlohmann::json& command_json, 
            bool simulated
        ) override;
        
    };
}