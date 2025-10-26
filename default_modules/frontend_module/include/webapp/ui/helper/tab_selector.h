#pragma once

#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>
#include <Wt/WText.h>

#include <vector>
#include <string>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class TabSelector : public Wt::WContainerWidget
    {
    public:
        TabSelector(std::vector<std::string> tabs);

        void setSelectedTab(size_t index); // select tab programmatically, does not emit signal

        Wt::Signal<size_t>& onTabSelected() { return onTabSelected_; } // tab selected signal

    private:
        Wt::Signal<size_t> onTabSelected_;

        std::vector<Wt::WText*> tab_list_;
    };
}