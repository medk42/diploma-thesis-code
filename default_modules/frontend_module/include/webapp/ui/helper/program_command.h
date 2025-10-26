#pragma once

#include <Wt/WText.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class ProgramCommand : public Wt::WText
    {   
    public:
        ProgramCommand(const std::string& command_name);

        Wt::Signal<>& onClick() { return onClick_; }
        void setSelected(bool selected)
        {
            if (selected)
                addStyleClass("selected");
            else
                removeStyleClass("selected");
        }

    private:
        Wt::Signal<> onClick_;
    };
}