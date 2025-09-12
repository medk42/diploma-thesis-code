#include "webapp/ui/activation_ui.h"

#include "webapp/ui/helper/topbar.h"

using namespace aergo::default_modules::frontend_module::webapp::ui;
using namespace aergo::module::helpers::activation_wrapper::params;

ActivationUi::ActivationUi()
{
    setStyleClass("activation-ui");

    auto top_bar = addWidget(std::make_unique<helper::TopBar>(
        "Setup Modules",
        std::vector<helper::ButtonDescription> {
            {"Save", helper::ButtonStyle::Secondary, true},
            {"Load", helper::ButtonStyle::Secondary, true}
        },
        std::vector<helper::ButtonDescription> {
            {"Close", helper::ButtonStyle::Danger, true}
        }
    ));

    top_bar->onButtonClicked().connect([this](size_t index){
        if (index == 0) // Save
        {
            onSave_.emit();
        }
        else if (index == 1) // Load
        {
            onLoad_.emit();
        }
        else if (index == 2) // Close
        {
            onClose_.emit();
        }
    });

    auto content_container = addWidget(std::make_unique<Wt::WContainerWidget>());
    content_container->setStyleClass("content");

    module_list_ = content_container->addWidget(std::make_unique<helper::LeftModuleList>(true, true));

    details_view_container_ = content_container->addWidget(std::make_unique<Wt::WStackedWidget>());    // widget showing details of the selected module, contains all detail views
    details_view_container_->setStyleClass("content-right");

    module_list_->moduleSelected().connect([this](size_t index){
        details_view_container_->setCurrentIndex(static_cast<int>(index));
    });

    module_list_->addNewSelected().connect([this](){
        onAddNew_.emit();
    });
}



void ActivationUi::addModule(uint64_t running_module_id, const char* module_name, const char* module_description, bool supports_activation, bool parameters_delayed, const std::vector<ParameterDescription>& parameters)
{
    // get inner index
    size_t inner_index = detail_views_.size();


    // add new module to UI
    module_list_->addModule(std::string(module_name) + " (ID: " + std::to_string(running_module_id) + ")", !supports_activation); // if module does not support activation, it is always ready

    helper::RightModuleView* detail_view = nullptr;
    if (supports_activation)
    {
        std::vector<helper::RightModuleView::ParameterSection> selection;
        if (!parameters_delayed)
        {
            selection.emplace_back(std::move(helper::RightModuleView::ParameterSection {"Parameters", parameters}));
        }

        detail_view = details_view_container_->addWidget(std::make_unique<helper::RightModuleView>(
            module_name,
            module_description,
            std::vector<helper::RightModuleView::ButtonDescriptionValid> {
                {{"Remove Module", helper::ButtonStyle::Danger, true}, false}
            },
            std::vector<helper::RightModuleView::ButtonDescriptionValid> {
                {{"Deactivate", helper::ButtonStyle::Primary, false}, false},
                {{"Activate", helper::ButtonStyle::Primary, !parameters_delayed}, true}
            },
            std::move(selection)
        ));
    }
    else
    {
        detail_view = details_view_container_->addWidget(std::make_unique<helper::RightModuleView>(
            module_name,
            module_description,
            std::vector<helper::RightModuleView::ButtonDescriptionValid> {
                {{"Remove Module", helper::ButtonStyle::Danger, true}, false}
            },
            std::vector<helper::RightModuleView::ButtonDescriptionValid> {}, // no activate/deactivate button, because module does not support activation (always active)
            std::vector<helper::RightModuleView::ParameterSection> {} // no parameters, because module does not support activation (always active)
        ));
    }


    // update UI to select the new module
    module_list_->setSelected(inner_index);
    details_view_container_->setCurrentIndex(static_cast<int>(inner_index));


    // update inner data structures
    detail_views_.push_back(detail_view);
    supports_activation_[running_module_id] = supports_activation;
    running_module_id_to_inner_index_[running_module_id] = inner_index;


    // connect signals
    detail_view->onButtonClicked().connect([this, running_module_id, supports_activation](size_t index){
        if (index == 0) // Remove Module
        {
            onRemoveModule_.emit(running_module_id);
        }
        else if (supports_activation)
        {
            if (index == 1) // Deactivate
            {
                onDeactivate_.emit(running_module_id);
            }
            else if (index == 2) // Activate
            {
                onActivate_.emit(running_module_id);
            }
        }
    });

    detail_view->onShowDescription().connect([this](helper::RightModuleView::ParameterId param_id, const std::string& param_name, const std::string& param_description){
        if (!parameter_dialog_)
        {
            showDialog(std::make_unique<helper::ReusableDialog>(
                param_name,
                param_description,
                std::vector<helper::ButtonDescription> { {"Close", helper::ButtonStyle::Secondary, true} }
            ));
        }
    });

    detail_view->onValueAdded().connect([this, running_module_id](helper::RightModuleView::ParameterId param_id, size_t list_index, helper::value_opt_t value){
        ModuleSingleParameter param{ running_module_id, param_id.param_id_, list_index };
        addListEntry_.emit(param);
        if (value.has_value())
        {
            onParameterChanged_.emit(param, value.value());
        }
    });

    detail_view->onValueRemoved().connect([this, running_module_id](helper::RightModuleView::ParameterId param_id, size_t list_index){
        ModuleSingleParameter param{ running_module_id, param_id.param_id_, list_index };
        removeListEntry_.emit(param);
    });

    detail_view->onValueChanged().connect([this, running_module_id](helper::RightModuleView::ParameterId param_id, size_t list_index, helper::value_t value){
        ModuleSingleParameter param{ running_module_id, param_id.param_id_, list_index };
        onParameterChanged_.emit(param, value);
    });
}



void ActivationUi::removeModule(uint64_t running_module_id)
{
    // get inner index
    auto it = running_module_id_to_inner_index_.find(running_module_id);
    if (it == running_module_id_to_inner_index_.end())
    {
        return; // not found
    }

    size_t index = it->second;

    
    // remove from UI
    details_view_container_->removeWidget(detail_views_[index]);
    module_list_->removeModule(index);
    if (detail_views_.size() > 1) // set another module selected if there are any left
    {
        size_t new_index = (index == detail_views_.size() - 1) ? index - 1 : index; // select previous if last, otherwise next

        module_list_->setSelected(new_index);
        details_view_container_->setCurrentIndex(static_cast<int>(new_index));
    }
    

    // update inner data structures
    detail_views_.erase(detail_views_.begin() + index);
    supports_activation_.erase(running_module_id);

    running_module_id_to_inner_index_.erase(it);
    for (auto& pair : running_module_id_to_inner_index_) // update indices of modules after the removed one
    {
        if (pair.second > index)
            --pair.second;
    }
}



void ActivationUi::setParameters(uint64_t running_module_id, const std::vector<aergo::module::helpers::activation_wrapper::params::ParameterDescription>& parameters)
{
    // get inner index
    auto it = running_module_id_to_inner_index_.find(running_module_id);
    if (it == running_module_id_to_inner_index_.end())
    {
        return; // not found
    }

    size_t inner_index = it->second;


    detail_views_[inner_index]->setParameters(std::vector<helper::RightModuleView::ParameterSection> {
        helper::RightModuleView::ParameterSection {"Parameters", parameters }
    });
    
    detail_views_[inner_index]->setButtonEnabled(1, false); // Deactivate button
    detail_views_[inner_index]->setButtonEnabled(2, true); // Activate button
}



bool ActivationUi::setParameterValues(uint64_t running_module_id, const std::vector<std::vector<helper::value_t>>& values)
{
    auto it = running_module_id_to_inner_index_.find(running_module_id);
    if (it == running_module_id_to_inner_index_.end())
    {
        return false; // not found
    }

    size_t inner_index = it->second;
    auto right_module_view = detail_views_[inner_index];
    
    for (size_t param_id = 0; param_id < values.size(); ++param_id)
    {
        const auto& param_values = values[param_id];
        size_t expected_list_size = param_values.size();
        size_t current_list_size = right_module_view->listSize(0, param_id); // section id is always 0 because there is only one section

        // first adjust list size if necessary
        if (expected_list_size > current_list_size) // need to add entries
        {
            for (; current_list_size < expected_list_size; ++current_list_size)
            {
                if (!right_module_view->addListItem(0, param_id)) // section id is always 0 because there is only one section
                {
                    return false; // failed to add list item
                }
            }
        }
        else if (expected_list_size < current_list_size) // need to remove entries
        {
            for (; current_list_size > expected_list_size; --current_list_size)
            {
                if (!right_module_view->removeListItem(0, param_id, current_list_size - 1)) // section id is always 0 because there is only one section
                {
                    return false; // failed to remove list item
                }
            }
        }

        // now set values
        for (size_t list_index = 0; list_index < param_values.size(); ++list_index)
        {
            if (!right_module_view->setValue(0, param_id, param_values[list_index], list_index)) // section id is always 0 because there is only one section
            {
                return false; // failed to set value
            }
        }
    }

    return true;
}



void ActivationUi::setActive(uint64_t running_module_id, bool active)
{
    auto it = supports_activation_.find(running_module_id);
    if (it == supports_activation_.end() || !it->second)
    {
        return; // not found or does not support activation (= always active)
    }

    auto it2 = running_module_id_to_inner_index_.find(running_module_id);
    if (it2 == running_module_id_to_inner_index_.end())
    {
        return; // not found
    }

    size_t inner_index = it2->second;
    
    module_list_->setReady(inner_index, active);

    detail_views_[inner_index]->setButtonEnabled(1, active);   // Deactivate button
    detail_views_[inner_index]->setButtonEnabled(2, !active);  // Activate button
}



void ActivationUi::showDialog(std::unique_ptr<helper::ReusableDialog> dialog)
{
    if (parameter_dialog_)
    {
        removeWidget(parameter_dialog_);
        parameter_dialog_ = nullptr;
    }

    parameter_dialog_ = addWidget(std::move(dialog));

    parameter_dialog_->onButtonClicked().connect([this](size_t){
        removeWidget(parameter_dialog_);
        parameter_dialog_ = nullptr;
    });

    parameter_dialog_->onBackgroundClicked().connect([this]{
        removeWidget(parameter_dialog_);
        parameter_dialog_ = nullptr;
    });
}