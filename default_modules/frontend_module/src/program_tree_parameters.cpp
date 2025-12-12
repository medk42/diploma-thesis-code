#include "webapp/ui/helper/program_tree_parameters.h"

#include <Wt/WText.h>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

ProgramTreeParameters::ProgramTreeParameters(
    const std::vector<p_desc::ParameterDescription>& auto_parameters,
    const std::vector<p_desc::ParameterDescription>& required_parameters,
    const std::vector<p_desc::ParameterDescription>& advanced_parameters
)
{
    setStyleClass("program-tree-parameters");

    auto parameter_section = addWidget(std::make_unique<Wt::WContainerWidget>());
    parameter_section->setStyleClass("program-tree-parameter-section");


    auto auto_parameters_section = setupSection(command_param_type::AUTO, parameter_section, auto_parameters, "Automatic Parameters", false);
    auto required_parameters_section = setupSection(command_param_type::REQUIRED, parameter_section, required_parameters, "Required Parameters", true);
    auto advanced_parameters_section = setupSection(command_param_type::ADVANCED, parameter_section, advanced_parameters, "Advanced Parameters", true);


    auto action_bar = addWidget(std::make_unique<Wt::WContainerWidget>());
    action_bar->setStyleClass("action-bar");
    
    if (advanced_parameters_section)
    {
        auto checkbox_container = action_bar->addWidget(std::make_unique<Wt::WContainerWidget>());
        auto checkbox_label = checkbox_container->addWidget(std::make_unique<Wt::WText>("ADVANCED"));
        checkbox_label->setStyleClass("light-gray-text checkbox-label");
        auto checkbox = checkbox_container->addWidget(std::make_unique<ToggleCheckbox>());
        checkbox->addStyleClass("input-checkbox-inline");
        advanced_parameters_section->setHidden(true);
        checkbox->clicked().connect([advanced_parameters_section, checkbox]() {
            bool is_checked = checkbox->isChecked();
            advanced_parameters_section->setHidden(!is_checked);
        });
    }
    else
    {
        action_bar->addWidget(std::make_unique<Wt::WContainerWidget>()); // empty spacer to align confirm button to right
    }

    confirm_button_ = action_bar->addWidget(std::make_unique<Button>("Confirm", ButtonStyle::Primary));
}


Wt::WContainerWidget* ProgramTreeParameters::setupSection(command_param_type section_type, Wt::WContainerWidget* section_container, const std::vector<p_desc::ParameterDescription>& parameters, const std::string& section_title, bool top_padding)
{
    if (parameters.empty()) // do not create section if no parameters
    {
        return nullptr;
    }

    auto section = section_container->addWidget(std::make_unique<Wt::WContainerWidget>());
    section->setStyleClass("parameter-list");

    if (top_padding)
    {
        auto spacer = section->addWidget(std::make_unique<Wt::WContainerWidget>());
        spacer->setStyleClass("parameter-section-spacer");
    }

    auto title = section->addWidget(std::make_unique<Wt::WText>(section_title));
    title->setStyleClass("light-gray-text parameter-section-title");

    for (size_t param_id = 0; param_id < parameters.size(); ++param_id)
    {
        auto single_param = section->addWidget(std::make_unique<RightModuleParameter>(parameters[param_id]));
        bool is_custom = parameters[param_id].type_ == p_desc::ParameterType::CUSTOM;
        single_param->onShowDescription().connect([this](const std::string& title, const std::string& description) {
            onShowDescriptionClicked_.emit(title, description);
        });
        single_param->onValueAdded().connect([this, section_type, param_id, is_custom](size_t list_index, const value_opt_t& value) {
            onValueAdded_.emit(ParameterIndex {section_type, param_id, list_index}, value, is_custom);
        });
        single_param->onValueRemoved().connect([this, section_type, param_id](size_t list_index) {
            onValueRemoved_.emit(ParameterIndex {section_type, param_id, list_index});
        });
        single_param->onValueChanged().connect([this, section_type, param_id, is_custom](size_t list_index, const value_t& value) {
            onValueChanged_.emit(ParameterIndex {section_type, param_id, list_index}, value, is_custom);
        });

        switch (section_type)
        {
            case command_param_type::AUTO:
                auto_parameters_.push_back(single_param);
                break;
            case command_param_type::REQUIRED:
                required_parameters_.push_back(single_param);
                break;
            case command_param_type::ADVANCED:
                advanced_parameters_.push_back(single_param);
                break;
        }
    }

    return section;
}



bool ProgramTreeParameters::setValue(command_param_type param_type, size_t param_index, size_t list_index, const p_desc::ParameterValueOpt& value)
{
    RightModuleParameter* parameter_widget = nullptr;

    switch (param_type)
    {
        case command_param_type::AUTO:
            if (param_index < auto_parameters_.size())
                parameter_widget = auto_parameters_[param_index];
            break;
        case command_param_type::REQUIRED:
            if (param_index < required_parameters_.size())
                parameter_widget = required_parameters_[param_index];
            break;
        case command_param_type::ADVANCED:
            if (param_index < advanced_parameters_.size())
                parameter_widget = advanced_parameters_[param_index];
            break;
    }

    if (parameter_widget)
    {
        if (value)
        {
            return parameter_widget->setValue(convertFromParameterValue(*value), list_index);
        }
        else
        {
            parameter_widget->resetValue(list_index);
            return true;
        }
    }

    return false;
}


value_t ProgramTreeParameters::convertFromParameterValue(const p_desc::ParameterValue& param_value_opt)
{
    // convert ParameterValue to value_t (note: ParameterValue has an extra type: std::vector<uint8_t> for custom channel data, which is not representable in value_t)
    return std::visit([](auto&& arg) -> value_t {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
        {
            return true; // custom channel data is filled -> represent as 'true' boolean flag in value_t
        }
        else
        {
            return arg;
        }
    }, param_value_opt);
}


bool ProgramTreeParameters::setAllValues(const std::vector<std::vector<p_desc::ParameterValueOpt>>& auto_values,
                                        const std::vector<std::vector<p_desc::ParameterValueOpt>>& required_values,
                                        const std::vector<std::vector<p_desc::ParameterValueOpt>>& advanced_values)
{
    auto set_values = [](RightModuleParameter* parameter_widget, const std::vector<p_desc::ParameterValueOpt>& values) -> bool {
        for (size_t i = parameter_widget->listSize(); i < values.size(); ++i) // grow list if needed
        {
            if (!parameter_widget->addListItem(true))
                return false; // could not add item
        }
        for (size_t i = parameter_widget->listSize(); i > values.size(); --i) // shrink list if needed
        {
            if (!parameter_widget->removeListItem(i - 1))
                return false; // could not remove item
        }
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (values[i])
            {
                if (!parameter_widget->setValue(convertFromParameterValue(*values[i]), i))
                    return false; // could not set value
            }
            else
            {
                parameter_widget->resetValue(i);
            }
        }
        return true;
    };

    if (auto_values.size() != auto_parameters_.size() ||
        required_values.size() != required_parameters_.size() ||
        advanced_values.size() != advanced_parameters_.size())
    {
        return false;
    }

    for (size_t i = 0; i < auto_parameters_.size(); ++i)
    {
        if (!set_values(auto_parameters_[i], auto_values[i]))
        {
            return false;
        }
    }

    for (size_t i = 0; i < required_parameters_.size(); ++i)
    {
        if (!set_values(required_parameters_[i], required_values[i]))
        {
            return false;
        }
    }

    for (size_t i = 0; i < advanced_parameters_.size(); ++i)
    {
        if (!set_values(advanced_parameters_[i], advanced_values[i]))
        {
            return false;
        }
    }

    return true;
}


bool ProgramTreeParameters::areAllValuesSet() const
{
    auto are_values_set = [](const std::vector<RightModuleParameter*>& parameter_widget_vector) -> bool {
        for (const auto& parameter_widget : parameter_widget_vector)
        {
            for (size_t i = 0; i < parameter_widget->listSize(); ++i)
            {
                if (parameter_widget->type() == p_desc::ParameterType::CUSTOM && 
                    (parameter_widget->value(i) == std::nullopt || std::get<bool>(*parameter_widget->value(i)) == false))
                {
                    return false; // custom values need to be true to be considered 'set'
                }
                if (!parameter_widget->hasValue(i))
                {
                    return false;
                }
            }
        }
        return true;
    };

    return are_values_set(auto_parameters_) &&
           are_values_set(required_parameters_) &&
           are_values_set(advanced_parameters_);
}