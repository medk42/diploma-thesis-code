#include "webapp/ui/helper/right_module_view.h"


#include <Wt/WBreak.h>


using namespace aergo::default_modules::frontend_module::webapp::ui::helper;


RightModuleView::RightModuleView(std::string title, std::string description, std::vector<ButtonDescriptionValid> left_buttons, std::vector<ButtonDescriptionValid> right_buttons, const std::vector<ParameterSection>& parameter_sections)
{
    setStyleClass("detail-pane");

    // Body
    auto body = addWidget(std::make_unique<WContainerWidget>());
    body->setStyleClass("detail-body");

    auto title_widget = body->addWidget(std::make_unique<Wt::WText>(title));
    title_widget->setStyleClass("detail-title");

    auto desc_container = body->addWidget(std::make_unique<Wt::WContainerWidget>());
    desc_container->setStyleClass("desc");
    desc_container->addWidget(std::make_unique<Wt::WText>(description));

    section_container_ = body->addWidget(std::make_unique<Wt::WContainerWidget>());
    setParameters(parameter_sections);



    // Buttons
    auto actions = addWidget(std::make_unique<Wt::WContainerWidget>());
    actions->setStyleClass("action-bar");
    
    auto left_button_container = actions->addWidget(std::make_unique<Wt::WContainerWidget>());
    left_button_container->setStyleClass("button-container");
    for (size_t i = 0; i < left_buttons.size(); ++i)
    {
        const auto& desc = left_buttons[i];
        auto button = left_button_container->addWidget(std::make_unique<Button>(
            desc.button_desc_.text_, 
            desc.button_desc_.style_, 
            desc.button_desc_.enabled_ && (!desc.requires_valid_parameters_ || parameters_valid_)
        ));
        setupButton(button, desc, i);
    }

    auto right_button_container = actions->addWidget(std::make_unique<Wt::WContainerWidget>());
    right_button_container->setStyleClass("button-container");
    for (size_t i = 0; i < right_buttons.size(); ++i)
    {
        const auto& desc = right_buttons[i];
        auto button = right_button_container->addWidget(std::make_unique<Button>(
            desc.button_desc_.text_, 
            desc.button_desc_.style_, 
            desc.button_desc_.enabled_ && (!desc.requires_valid_parameters_ || parameters_valid_)
        ));
        setupButton(button, desc, left_buttons.size() + i);
    }



    updateParameterValidity();
}



void RightModuleView::setupButton(Button* button, const ButtonDescriptionValid& desc, size_t index)
{
    button->clicked().connect([this, index]() {
        onButtonClicked_.emit(index);
    });

    buttons_.push_back(button);
    button_descriptions_.push_back(desc);
}



void RightModuleView::setParameters(const std::vector<ParameterSection>& parameter_sections)
{
    parameters_.clear();
    section_container_->clear();

    for (size_t i = 0; i < parameter_sections.size(); ++i)
    {
        const auto& section = parameter_sections[i];

        auto parameter_section_container = section_container_->addWidget(std::make_unique<Wt::WContainerWidget>());
        parameter_section_container->setStyleClass("parameter-section");

        auto section_header = parameter_section_container->addWidget(std::make_unique<Wt::WText>(section.section_header_));
        section_header->setStyleClass("section-header");

        auto& last_vector = parameters_.emplace_back();
        for (size_t j = 0; j < section.parameters_.size(); ++j)
        {
            const auto& param = section.parameters_[j];

            auto param_widget = parameter_section_container->addWidget(std::make_unique<RightModuleParameter>(param));
            last_vector.emplace_back(param_widget);

            setupParameterSignals(i, j, param_widget);
        }
    }
}



void RightModuleView::setButtonEnabled(size_t index, bool enabled)
{
    if (index >= button_descriptions_.size())
        return;

    button_descriptions_[index].button_desc_.enabled_ = enabled;
    buttons_[index]->setEnabled(enabled && (!button_descriptions_[index].requires_valid_parameters_ || parameters_valid_));
}



size_t RightModuleView::listSize(size_t section_id, size_t param_id) const
{
    if (section_id >= parameters_.size())
        return 0;
    if (param_id >= parameters_[section_id].size())
        return 0;

    return parameters_[section_id][param_id]->listSize();
}



bool RightModuleView::addListItem(size_t section_id, size_t param_id)
{
    if (section_id >= parameters_.size())
        return false;
    if (param_id >= parameters_[section_id].size())
        return false;

    auto& param = parameters_[section_id][param_id];
    bool result = param->addListItem();
    if (result && parameters_valid_ && !param->hasValue(param->listSize() - 1))
    {
        parameters_valid_ = false; // definitely invalid now
        updateButtonStates();
    }

    return result;
}



bool RightModuleView::removeListItem(size_t section_id, size_t param_id, size_t index)
{
    if (section_id >= parameters_.size())
        return false;
    if (param_id >= parameters_[section_id].size())
        return false;

    bool result = parameters_[section_id][param_id]->removeListItem(index);

    if (result && !parameters_valid_)
        updateParameterValidity(); // might be valid now

    return result;
}



bool RightModuleView::hasValue(size_t section_id, size_t param_id, size_t index)
{
    if (section_id >= parameters_.size())
        return false;
    if (param_id >= parameters_[section_id].size())
        return false;

    return parameters_[section_id][param_id]->hasValue(index);
}



value_opt_t RightModuleView::value(size_t section_id, size_t param_id, size_t index)
{
    if (section_id >= parameters_.size())
        return std::nullopt;
    if (param_id >= parameters_[section_id].size())
        return std::nullopt;

    return parameters_[section_id][param_id]->value(index);
}



void RightModuleView::resetValue(size_t section_id, size_t param_id, size_t index)
{
    if (section_id >= parameters_.size())
        return;
    if (param_id >= parameters_[section_id].size())
        return;

    parameters_[section_id][param_id]->resetValue(index);

    if (parameters_valid_)
    {
        updateParameterValidity(); // might be invalid now
    }
}



bool RightModuleView::setValue(size_t section_id, size_t param_id, const value_t& value, size_t index)
{
    if (section_id >= parameters_.size())
        return false;
    if (param_id >= parameters_[section_id].size())
        return false;

    bool result = parameters_[section_id][param_id]->setValue(value, index);

    if (result && !parameters_valid_)
        updateParameterValidity(); // might be valid now

    return result;
}



void RightModuleView::updateParameterValidity()
{
    bool parameters_valid = true;
    for (size_t section_id = 0; section_id < parameters_.size() && parameters_valid; ++section_id)
    {
        for (size_t param_id = 0; param_id < parameters_[section_id].size() && parameters_valid; ++param_id)
        {
            auto& param = parameters_[section_id][param_id];
            for (size_t index = 0; index < param->listSize() && parameters_valid; ++index)
            {
                if (!param->hasValue(index))
                {
                    parameters_valid = false;
                    return;
                }
            }
        }
    }

    if (parameters_valid != parameters_valid_)
    {
        parameters_valid_ = parameters_valid;
        updateButtonStates();
    }
}

void RightModuleView::updateButtonStates()
{
    for (size_t i = 0; i < buttons_.size(); ++i)
    {
        buttons_[i]->setEnabled(button_descriptions_[i].button_desc_.enabled_ && (!button_descriptions_[i].requires_valid_parameters_ || parameters_valid_));
    }
}



void RightModuleView::setupParameterSignals(size_t section_id, size_t param_id, RightModuleParameter* param)
{
    param->onShowDescription().connect([this, section_id, param_id](const std::string& name, const std::string& desc) {
        onShowDescriptionClicked_.emit({section_id, param_id}, name, desc);
    });

    param->onValueAdded().connect([this, section_id, param_id](size_t index, value_opt_t value) {
        if (!value.has_value() && parameters_valid_)
        {
            parameters_valid_ = false; // definitely invalid now
            updateButtonStates();
        }

        onValueAdded_.emit({section_id, param_id}, index, value);
    });

    param->onValueRemoved().connect([this, section_id, param_id](size_t index) {
        if (!parameters_valid_)
            updateParameterValidity(); // might be valid now

        onValueRemoved_.emit({section_id, param_id}, index);
    });

    param->onValueChanged().connect([this, section_id, param_id](size_t index, value_t value) {
        if (!parameters_valid_)
            updateParameterValidity(); // might be valid now

        onValueChanged_.emit({section_id, param_id}, index, value);
    });
}



IParamInput* RightModuleView::getRawParameterWidget(size_t section_id, size_t param_id, size_t index)
{
    if (section_id >= parameters_.size())
        return nullptr;
    if (param_id >= parameters_[section_id].size())
        return nullptr;

    return parameters_[section_id][param_id]->getRawParameterWidget(index);
}