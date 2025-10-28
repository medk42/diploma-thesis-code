#pragma once

#include "right_module_parameter.h"
#include "button.h"
#include "input_fields.h"
#include "parameter_value.h"

#include "module_helpers/parameter_description/parameter_description.h"
#include "module_helpers/usecase_tree/structs.h"

#include <Wt/WContainerWidget.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    namespace p_desc = aergo::module::helpers::parameter_description;
    namespace u_tree = aergo::module::helpers::usecase_tree;

    using command_param_type = u_tree::structs::ExistingCommand::ParamType;

    class ProgramTreeParameters : public Wt::WContainerWidget
    {
    public:
        ProgramTreeParameters(
            const std::vector<p_desc::ParameterDescription>& auto_parameters,
            const std::vector<p_desc::ParameterDescription>& required_parameters,
            const std::vector<p_desc::ParameterDescription>& advanced_parameters
        );

        void setConfirmEnable(bool enabled) { confirm_button_->setEnabled(enabled); }
        bool isConfirmEnabled() const { return confirm_button_->isEnabled(); }
        Wt::Signal<>& confirmClicked() { return confirm_button_->clicked(); }

        bool setAllValues(const std::vector<std::vector<value_opt_t>>& auto_values,
                          const std::vector<std::vector<value_opt_t>>& required_values,
                          const std::vector<std::vector<value_opt_t>>& advanced_values);

        // returns true if set (successful), false if not (invalid index or value not valid for parameter)
        // mainly for use with ExistingCommand - if ExistingCommand does not accept the value from the signal, 
        // this is called to reset the parameter widget value as well
        bool setValue(command_param_type param_type, size_t param_index, size_t list_index, const value_opt_t& value);

        Wt::Signal<const std::string&, const std::string&>& onShowDescription() { return onShowDescriptionClicked_; } // parameter title and description
        Wt::Signal<command_param_type, size_t, size_t, value_opt_t>& onValueAdded() { return onValueAdded_; } // parameter list type, param index, list index, optionally value (if exists)
        Wt::Signal<command_param_type, size_t, size_t>& onValueRemoved() { return onValueRemoved_; } // parameter list type, param index, list index
        Wt::Signal<command_param_type, size_t, size_t, value_t>& onValueChanged() { return onValueChanged_; } // parameter list type, param index, list index, value
    

    private:
        Wt::WContainerWidget* setupSection(command_param_type section_type, Wt::WContainerWidget* section_container, const std::vector<p_desc::ParameterDescription>& parameters, const std::string& section_title, bool top_padding);

        Button* confirm_button_ = nullptr;

        std::vector<RightModuleParameter*> auto_parameters_;
        std::vector<RightModuleParameter*> required_parameters_;
        std::vector<RightModuleParameter*> advanced_parameters_;

        Wt::Signal<const std::string&, const std::string&> onShowDescriptionClicked_; // parameter title and description
        Wt::Signal<command_param_type, size_t, size_t, value_opt_t> onValueAdded_; // parameter list type, param index, optionally value (if exists)
        Wt::Signal<command_param_type, size_t, size_t> onValueRemoved_; // parameter list type, param index, list index
        Wt::Signal<command_param_type, size_t, size_t, value_t> onValueChanged_; // parameter list type, param index, list index, value
    };
}