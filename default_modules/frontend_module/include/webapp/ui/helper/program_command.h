#pragma once

#include <Wt/WText.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class ProgramCommand : public Wt::WText
    {   
    public:
        enum class Status { Normal, Warning, Invalid };

        ProgramCommand(const std::string& command_name, Status status = Status::Normal);

        void setStatus(Status status);
        Status getStatus() const { return status_; }
        Wt::Signal<>& onClick() { return onClick_; }
        void setSelected(bool selected)
        {
            if (selected)
                addStyleClass("selected");
            else
                removeStyleClass("selected");
        }

    private:
        Status status_;

        Wt::Signal<> onClick_;
    };
}