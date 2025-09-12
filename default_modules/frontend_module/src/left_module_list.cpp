#include "webapp/ui/helper/left_module_list.h"

#include <algorithm>

#include <Wt/WText.h>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;



LeftModuleList::LeftModuleList(bool show_add_new, bool show_ready)
: show_ready_(show_ready)
{
    setStyleClass("module-list");

    auto scrollable_widget = addWidget(std::make_unique<Wt::WContainerWidget>());
    scrollable_widget->setStyleClass("module-list-scrollable");

    list_inner_ = scrollable_widget->addWidget(std::make_unique<Wt::WContainerWidget>());
    list_inner_->setStyleClass("module-list-inner");

    if (show_add_new)
    {
        auto add_new = scrollable_widget->addWidget(std::make_unique<LeftModuleButton>("Add New", false, true));
        add_new->clicked().connect([this]{
            onAddNew_.emit();
        });
    }
}



void LeftModuleList::addModule(std::string name, bool ready)
{
    auto item = list_inner_->addWidget(std::make_unique<LeftModuleButton>(name, show_ready_, false));

    item->clicked().connect([this, item]{
        auto it = std::find(list_buttons_.begin(), list_buttons_.end(), item);
        if (it != list_buttons_.end()) {
            size_t idx = std::distance(list_buttons_.begin(), it);
            setSelected(idx);
            onModuleSelected_.emit(idx);
        }
    });

    if (show_ready_)
    {
        item->setReady(ready);
    }

    list_buttons_.push_back(item);
}



void LeftModuleList::removeModule(size_t index)
{
    if (index < list_buttons_.size())
    {
        list_inner_->removeWidget(list_buttons_[index]);
        list_buttons_.erase(list_buttons_.begin() + index);

        if (selected_index_ >= list_buttons_.size())
        {
            selected_index_ = list_buttons_.size() - 1;
        }
    }
}



void LeftModuleList::setSelected(size_t index)
{
    if (index < list_buttons_.size())
    {
        if (selected_index_ < list_buttons_.size())
            list_buttons_[selected_index_]->setSelected(false);
            
        list_buttons_[index]->setSelected(true);

        selected_index_ = index;
    }
}



void LeftModuleList::setReady(size_t index, bool ready)
{
    if (show_ready_ && index < list_buttons_.size())
    {
        list_buttons_[index]->setReady(ready);
    }
}