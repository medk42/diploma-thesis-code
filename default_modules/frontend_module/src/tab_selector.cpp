#include "webapp/ui/helper/tab_selector.h"

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

TabSelector::TabSelector(std::vector<std::string> tabs)
{
    if (tabs.size() <= 1)
    {
        throw std::invalid_argument("TabSelector requires at least two tabs");
    }

    setStyleClass("tab-selector");

    for (size_t i = 0; i < tabs.size(); ++i)
    {
        auto tab = addWidget(std::make_unique<Wt::WText>(tabs[i]));
        tab->setStyleClass("tab-selector-tab");

        size_t tab_index = i;
        tab->clicked().connect([this, tab_index]() {
            setSelectedTab(tab_index);
            onTabSelected_.emit(tab_index);
        });

        tab_list_.push_back(tab);
    }

    setSelectedTab(0);
}


void TabSelector::setSelectedTab(size_t index)
{
    if (index >= tab_list_.size())
    {
        throw std::out_of_range("TabSelector::setSelectedTab: index out of range");
    }

    for (size_t i = 0; i < tab_list_.size(); ++i)
    {
        if (i == index)
        {
            tab_list_[i]->addStyleClass("tab-selector-tab-selected", true);
        }
        else
        {
            tab_list_[i]->removeStyleClass("tab-selector-tab-selected", true);
        }
    }
}