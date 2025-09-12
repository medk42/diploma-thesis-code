#pragma once

#include "left_module_button.h"

#include <vector>

#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class LeftModuleList : public Wt::WContainerWidget
    {
    public:
        LeftModuleList(bool show_add_new, bool show_ready);

        void addModule(std::string name, bool ready = false);
        void removeModule(size_t index);
        void setSelected(size_t index);
        void setReady(size_t index, bool ready);

        Wt::Signal<size_t>& moduleSelected() { return onModuleSelected_; }
        Wt::Signal<>& addNewSelected() { return onAddNew_; }

    private:

        bool show_ready_;
        
        Wt::WContainerWidget* list_inner_{ nullptr };
        std::vector<LeftModuleButton*> list_buttons_;
        size_t selected_index_ = 0;

        Wt::Signal<size_t> onModuleSelected_;
        Wt::Signal<> onAddNew_;
    };
}