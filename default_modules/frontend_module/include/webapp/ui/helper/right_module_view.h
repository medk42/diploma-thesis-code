#pragma once

#include "module_helpers/parameter_description/parameter_description.h"
#include "right_module_parameter.h"
#include "button.h"

#include <string>
#include <vector>

#include <Wt/WContainerWidget.h>
#include <Wt/WPushButton.h>
#include <Wt/WText.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class RightModuleView : public Wt::WContainerWidget
    {
    public:
        struct ButtonDescriptionValid;
        struct ParameterSection;
        struct ParameterId;

        RightModuleView(std::string title, std::string description, std::vector<ButtonDescriptionValid> left_buttons, std::vector<ButtonDescriptionValid> right_buttons, const std::vector<ParameterSection>& parameter_sections);

        void setParameters(const std::vector<ParameterSection>& parameter_sections); // resets all set values to defaults
        void setButtonEnabled(size_t index, bool enabled);

        size_t listSize(size_t section_id, size_t param_id) const;              // returns size of parameter list or 0 if invalid section_id or param_id
        bool addListItem(size_t section_id, size_t param_id);                   // returns true if added (successful), false if not (max size reached or invalid section_id or param_id)
        bool removeListItem(size_t section_id, size_t param_id, size_t index);  // returns true if removed (successful), false if not (min size reached or invalid index or invalid section_id or param_id)
        
        bool hasValue(size_t section_id, size_t param_id, size_t index = 0);     // returns true if parameter has value, false if not (or invalid section_id or param_id or index)
        value_opt_t value(size_t section_id, size_t param_id, size_t index = 0); // returns parameter value or nullopt if not set (or invalid section_id or param_id or index)
        void resetValue(size_t section_id, size_t param_id, size_t index = 0);   // resets parameter value to invalid or empty
        bool setValue(size_t section_id, size_t param_id, const value_t& value, size_t index = 0); // returns true if set (successful), false if not (invalid section_id or param_id or index or type mismatch)
        
        Wt::Signal<size_t>& onButtonClicked() { return onButtonClicked_; }   // id of the button in the order of construction (left to right)
        Wt::Signal<ParameterId, const std::string&, const std::string&>& onShowDescription() { return onShowDescriptionClicked_; } // section id, param id, parameter name, parameter description
        Wt::Signal<ParameterId, size_t, value_opt_t>& onValueAdded() { return onValueAdded_; }           // section id, param id, list index, optionally value (if exists)
        Wt::Signal<ParameterId, size_t>& onValueRemoved() { return onValueRemoved_; }                    // section id, param id, list index
        Wt::Signal<ParameterId, size_t, value_t>& onValueChanged() { return onValueChanged_; }           // section id, param id, list index, value

        IParamInput* getRawParameterWidget(size_t section_id, size_t param_id, size_t index = 0); // for direct access to the widget (e.g. for setting focus), may return nullptr if invalid section_id or param_id or index

    private:
        void setupButton(Button* button, const ButtonDescriptionValid& desc, size_t index);
        void updateParameterValidity();
        void updateButtonStates();
        void setupParameterSignals(size_t section_id, size_t param_id, RightModuleParameter* param);

        Wt::WContainerWidget* section_container_ { nullptr };

        Wt::Signal<size_t> onButtonClicked_;
        bool parameters_valid_ { false };
        std::vector<ButtonDescriptionValid> button_descriptions_;
        std::vector<Button*> buttons_;

        std::vector<std::vector<RightModuleParameter*>> parameters_;

        Wt::Signal<ParameterId, const std::string&, const std::string&> onShowDescriptionClicked_; // section id, param id, parameter name, parameter description
        Wt::Signal<ParameterId, size_t, value_opt_t> onValueAdded_; // section id, param id, list index, optionally value (if exists)
        Wt::Signal<ParameterId, size_t> onValueRemoved_; // section id, param id, list index
        Wt::Signal<ParameterId, size_t, value_t> onValueChanged_; // section id, param id, list index, value
    };

    struct RightModuleView::ButtonDescriptionValid
    {
        ButtonDescription button_desc_;
        bool requires_valid_parameters_;
    };

    struct RightModuleView::ParameterSection
    {
        std::string section_header_;
        std::vector<aergo::module::helpers::parameter_description::ParameterDescription> parameters_;
    };

    struct RightModuleView::ParameterId
    {
        size_t section_id_;
        size_t param_id_;
    };
}