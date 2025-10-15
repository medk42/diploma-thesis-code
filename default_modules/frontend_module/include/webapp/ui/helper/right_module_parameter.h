#pragma once

#include "module_helpers/parameter_description/parameter_description.h"
#include "button.h"
#include "input_fields.h"

#include <optional>
#include <variant>

#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class RightModuleParameter : public Wt::WContainerWidget
    {
    public:
        using value_type_t = aergo::module::helpers::parameter_description::ParameterType;
        using desc_t = aergo::module::helpers::parameter_description::ParameterDescription;
    
        RightModuleParameter(desc_t parameter_description);

        value_type_t type() { return parameter_description_.type_; }
        bool isList() { return parameter_description_.as_list_; }

        size_t listSize() const { return parameter_widgets_.size(); }
        bool addListItem(); // returns true if added (successful), false if not (max size reached)
        bool removeListItem(size_t index); // returns true if removed (successful), false if not (min size reached or invalid index)

        bool hasValue(size_t index = 0) { return index < parameter_widgets_.size() ? parameter_widgets_[index]->hasValue() : false; }
        value_opt_t value(size_t index = 0) { return index < parameter_widgets_.size() ? parameter_widgets_[index]->value() : std::nullopt; }
        void resetValue(size_t index = 0) { if (index < parameter_widgets_.size()) parameter_widgets_[index]->resetValue(); }
        bool setValue(const value_t& value, size_t index = 0);

        Wt::Signal<const std::string&, const std::string&>& onShowDescription() { return onShowDescriptionClicked_; } // parameter title and description
        Wt::Signal<size_t, value_opt_t>& onValueAdded() { return onValueAdded_; } // list index, optionally value (if exists)
        Wt::Signal<size_t>& onValueRemoved() { return onValueRemoved_; } // list index
        Wt::Signal<size_t, value_t>& onValueChanged() { return onValueChanged_; } // list index, value

        IParamInput* getRawParameterWidget(size_t index = 0) { return index < parameter_widgets_.size() ? parameter_widgets_[index] : nullptr; } // for direct access to the widget (e.g. for setting focus), may return nullptr if index invalid

    private:
        void addListItemWidget(Wt::WContainerWidget* parent);
        void addParameterWidget(Wt::WContainerWidget* parent);
        void updateListButtonsVisibility();

        aergo::module::helpers::parameter_description::ParameterDescription parameter_description_;

        Wt::Signal<const std::string&, const std::string&> onShowDescriptionClicked_; // parameter title and description
        Wt::Signal<size_t, value_opt_t> onValueAdded_; // list index, optionally value (if exists)
        Wt::Signal<size_t> onValueRemoved_; // list index
        Wt::Signal<size_t, value_t> onValueChanged_; // list index, value

        Wt::WContainerWidget* list_container_ {nullptr}; // if as_list_ = true, container for list items
        std::vector<Wt::WText*> list_item_remove_texts_;
        Button* list_add_button_ {nullptr};

        std::vector<IParamInput*> parameter_widgets_; // if as_list_ = false, size is 1

        bool can_remove_;
        bool can_add_;

        uint32_t custom_channel_id_counter_ {1};
    };
}