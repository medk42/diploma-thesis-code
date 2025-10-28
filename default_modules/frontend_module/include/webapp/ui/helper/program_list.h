#pragma once

#include "program_command.h"

#include <vector>

#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class ProgramList : public Wt::WContainerWidget
    {
    public:
        ProgramList(std::string title, std::string style_class, bool selectable);

        void addCommand(const std::string& command_name, ProgramCommand::Status command_status, bool selected = false);
        void insertCommand(size_t index, const std::string& command_name, ProgramCommand::Status command_status, bool selected = false);
        void clearCommands();
        void setCommandSelected(size_t index);
        void removeCommand(size_t index);

        Wt::Signal<size_t>& onCommandClicked() { return onCommandClicked_; }

    private:
        bool selectable_;
        std::vector<ProgramCommand*> command_list_;
        Wt::WContainerWidget* command_container_;

        Wt::Signal<size_t> onCommandClicked_;
    };
}