#pragma once

#include "program_command.h"

#include <vector>
#include <optional>

#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class ProgramList : public Wt::WContainerWidget
    {
    public:
        ProgramList(std::string title, std::string style_class, bool selectable);

        void addCommand(const std::string& command_name, ProgramCommand::Status command_status);
        void insertCommand(size_t index, const std::string& command_name, ProgramCommand::Status command_status);
        void clearCommands();
        size_t commandCount() const { return command_list_.size(); }
        void setCommandSelected(size_t index);
        void setCommandStatus(size_t index, ProgramCommand::Status status);
        void removeCommand(size_t index);
        std::optional<size_t> selectedCommandIndex() const { return selected_command_index_; } // returns std::nullopt if no command is selected or selection is disabled
        bool allCommandsHaveStatus(ProgramCommand::Status status) const;

        Wt::Signal<size_t>& onCommandClicked() { return onCommandClicked_; }

    private:
        bool selectable_;
        std::vector<ProgramCommand*> command_list_;
        Wt::WContainerWidget* command_container_;

        Wt::Signal<size_t> onCommandClicked_;
        std::optional<size_t> selected_command_index_{ std::nullopt };
    };
}