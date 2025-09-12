#include "webapp/ui/helper/right_module_parameter.h"

#include <Wt/WAnchor.h>
#include <Wt/WSpinBox.h>
#include <Wt/WPushButton.h>

#include <iostream>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;
using namespace aergo::module::helpers::activation_wrapper::params;


RightModuleParameter::RightModuleParameter(ParameterDescription parameter_description)
: parameter_description_(parameter_description), can_remove_(false), can_add_(true)
{
    setStyleClass("parameter-item");

    auto name_anchor = addWidget(std::make_unique<Wt::WAnchor>(Wt::WLink(), parameter_description_.param_name_));
    name_anchor->setStyleClass("parameter-name");
    name_anchor->clicked().connect([this]() {
        onShowDescriptionClicked_.emit(parameter_description_.param_name_, parameter_description_.param_desc_);
    });
    name_anchor->setTabIndex(-1); // prevent tabbing to this anchor

    auto value_wrap = addWidget(std::make_unique<Wt::WContainerWidget>());
    value_wrap->setStyleClass("parameter-widget");

    if (parameter_description_.as_list_)
    {
        // list of values
        auto list_wrap = value_wrap->addWidget(std::make_unique<Wt::WContainerWidget>());
        list_wrap->setStyleClass("list-wrapper");
        list_container_ = list_wrap->addWidget(std::make_unique<Wt::WContainerWidget>());

        auto button_wrap = list_wrap->addWidget(std::make_unique<Wt::WContainerWidget>());
        button_wrap->setStyleClass("list-item-button");
        button_wrap->addWidget(std::make_unique<Wt::WContainerWidget>())->setStyleClass("grow"); // spacer
        list_add_button_ = button_wrap->addWidget(std::make_unique<Button>("+", ButtonStyle::Secondary, true));
        
        if (parameter_description_.list_size_min_ >= parameter_description_.list_size_max_ && parameter_description_.list_size_max_ > 0)
        {
            list_add_button_->setEnabled(false);
            can_add_ = false;
        }
        list_add_button_->clicked().connect([this]() {
            addListItem();
        });

        for (size_t i = 0; i < parameter_description_.list_size_min_; ++i)
        {
            addListItemWidget(list_container_);
        }

        for (auto& remove_text : list_item_remove_texts_)
        {
            remove_text->addStyleClass("list-item-remove-disabled");
        }
    }
    else
    {
        // single value
        addParameterWidget(value_wrap);
    }
}



bool RightModuleParameter::addListItem()
{
    if (!parameter_description_.as_list_)
        return false;

    if (parameter_description_.list_size_max_ != 0 && list_container_->count() >= parameter_description_.list_size_max_)
        return false;

    addListItemWidget(list_container_);
    updateListButtonsVisibility();
    return true;
}



bool RightModuleParameter::removeListItem(size_t index)
{
    if (!parameter_description_.as_list_)
        return false;

    size_t count = list_container_->count();
    if (index >= count)
        return false;

    if (count <= parameter_description_.list_size_min_)
        return false;

    list_item_remove_texts_.erase(list_item_remove_texts_.begin() + index);
    parameter_widgets_.erase(parameter_widgets_.begin() + index);
    list_container_->removeWidget(list_container_->children()[index]);
    
    updateListButtonsVisibility();

    return true;
}



bool RightModuleParameter::setValue(const value_t& value, size_t index)
{
    if (index >= parameter_widgets_.size())
        return false;

    parameter_widgets_[index]->setValue(value);

    return true;
}



void RightModuleParameter::updateListButtonsVisibility()
{
    if (!parameter_description_.as_list_)
        return;

    size_t count = list_container_->count();
    bool can_remove = count > parameter_description_.list_size_min_;
    bool can_add = (parameter_description_.list_size_max_ == 0 || count < parameter_description_.list_size_max_);

    if (can_add != can_add_)
    {
        list_add_button_->setEnabled(can_add);
    }

    if (can_remove != can_remove_)
    {
        for (auto& remove_text : list_item_remove_texts_)
        {
            if (can_remove)
                remove_text->removeStyleClass("list-item-remove-disabled");
            else
                remove_text->addStyleClass("list-item-remove-disabled");
        }
    }
    
    can_add_ = can_add;
    can_remove_ = can_remove;
}



void RightModuleParameter::addListItemWidget(Wt::WContainerWidget* parent)
{
    auto param_wrap = list_container_->addWidget(std::make_unique<Wt::WContainerWidget>());
    param_wrap->setStyleClass("list-item");
    addParameterWidget(param_wrap);

    auto remove_text = param_wrap->addNew<Wt::WText>("✕");
    remove_text->setStyleClass("list-item-remove");
    remove_text->clicked().connect([this, param_wrap, remove_text]{
        if (can_remove_)
        {
            auto it = std::find(list_item_remove_texts_.begin(), list_item_remove_texts_.end(), remove_text);
            if (it != list_item_remove_texts_.end())
            {
                size_t index = std::distance(list_item_remove_texts_.begin(), it);
                if (removeListItem(index))
                {
                    onValueRemoved_.emit(index);
                }
            }
        }
    });
    list_item_remove_texts_.push_back(remove_text);
}



void RightModuleParameter::addParameterWidget(Wt::WContainerWidget* parent)
{
    IParamInput* widget = nullptr;

    switch (parameter_description_.type_)
    {
    case ParameterType::LONG:
        {
            bool parse_ok = false; 
            int64_t long_value;
            try {
                long_value = std::stoll(parameter_description_.default_value_);
                parse_ok = true;
            } catch (...) {
                long_value = 0;
            }
            parent->addWidget(std::make_unique<Wt::WContainerWidget>())->setStyleClass("grow");  // make sure input is aligned right
            widget = parent->addWidget(
                std::make_unique<NumericLineEdit<int64_t>>(
                    parse_ok && !parameter_description_.default_value_.empty(), long_value,
                    parameter_description_.limit_min_, parameter_description_.min_value_long_,
                    parameter_description_.limit_max_, parameter_description_.max_value_long_
                )
            );
        }
        break;
    case ParameterType::DOUBLE:
        {
            bool parse_ok = false;
            double double_value;
            try {
                double_value = std::stod(parameter_description_.default_value_);
                parse_ok = true;
            } catch (...) {
                double_value = 0.0;
            }
            if (parameter_description_.as_slider_)
            {
                double min = parameter_description_.limit_min_ ? parameter_description_.min_value_double_ : 0.0;
                double max = parameter_description_.limit_max_ ? parameter_description_.max_value_double_ : 1.0;

                if (min >= max) { min = 0.0; max = 1.0; } // fallback range
                if (double_value < min) double_value = min;
                if (double_value > max) double_value = max;

                widget = parent->addWidget(std::make_unique<SliderLineEdit>(
                    min, max, double_value
                ));
            }
            else
            {
                parent->addWidget(std::make_unique<Wt::WContainerWidget>())->setStyleClass("grow");  // make sure input is aligned right
                widget = parent->addWidget(
                    std::make_unique<NumericLineEdit<double>>(
                        parse_ok && !parameter_description_.default_value_.empty(), double_value,
                        parameter_description_.limit_min_, parameter_description_.min_value_double_,
                        parameter_description_.limit_max_, parameter_description_.max_value_double_
                    )
                );
            }
        }
        break;
    case ParameterType::BOOL:
        {
            bool bool_value = false;
            if (!parameter_description_.default_value_.empty())
            {
                if (parameter_description_.default_value_ == "1" || parameter_description_.default_value_ == "true" || parameter_description_.default_value_ == "TRUE")
                    bool_value = true;
            }
        
            parent->addWidget(std::make_unique<Wt::WContainerWidget>())->setStyleClass("grow");  // make sure input is aligned right
            widget = parent->addWidget(std::make_unique<ToggleCheckbox>(bool_value));
        }
        break;

    case ParameterType::STRING:
        {
            parent->addWidget(std::make_unique<Wt::WContainerWidget>())->setStyleClass("grow");  // make sure input is aligned right
            widget = parent->addWidget(std::make_unique<TextLineEdit>(parameter_description_.default_value_));
        }
        break;
    
    case ParameterType::ENUM:
        {
            parent->addWidget(std::make_unique<Wt::WContainerWidget>())->setStyleClass("grow");  // make sure input is aligned right
            widget = parent->addWidget(std::make_unique<EnumSelect>(
                parameter_description_.enum_values_,
                parameter_description_.default_value_.empty() ? std::nullopt : std::optional<std::string>(parameter_description_.default_value_),
                "Select..."
            ));
        }
        break;
    
    case ParameterType::CUSTOM:
        {
            parent->addWidget(std::make_unique<Wt::WContainerWidget>())->setStyleClass("grow");  // make sure input is aligned right
            widget = parent->addWidget(std::make_unique<CustomValue>("Click to set", "Value " + std::to_string(custom_channel_id_counter_++)));
        }
        break;
    }

    if (widget)
    {
        parameter_widgets_.push_back(widget);

        widget->changed().connect([this, widget](value_t new_value) {
            auto it = std::find(parameter_widgets_.begin(), parameter_widgets_.end(), widget);
            if (it != parameter_widgets_.end())
            {
                size_t index = std::distance(parameter_widgets_.begin(), it);
                onValueChanged_.emit(index, new_value);
            }
        });

        onValueAdded_.emit(parameter_widgets_.size() - 1, widget->value());
    }
}