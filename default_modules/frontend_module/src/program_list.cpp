#include "webapp/ui/helper/program_list.h"

#include <Wt/WText.h>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

ProgramList::ProgramList(std::string title, std::string style_class, bool selectable)
    : selectable_(selectable)
{
    setStyleClass(style_class);

    auto title_widget = addWidget(std::make_unique<Wt::WText>(title));
    title_widget->setStyleClass("light-gray-text program-list-title");

    command_container_ = addWidget(std::make_unique<Wt::WContainerWidget>());
    command_container_->setStyleClass("program-list-commands");
}


void ProgramList::addCommand(const std::string& command_name, ProgramCommand::Status command_status)
{
    insertCommand(command_list_.size(), command_name, command_status);
}


void ProgramList::insertCommand(size_t index, const std::string& command_name, ProgramCommand::Status command_status)
{
    index = std::min(index, command_list_.size());
    auto program_command = command_container_->insertWidget(index, std::make_unique<ProgramCommand>(command_name, command_status));
    
    command_list_.insert(command_list_.begin() + index, program_command);
    program_command->onClick().connect([this, program_command]() {
        size_t index = 0;
        for (; index < command_list_.size(); ++index)
        {
            if (command_list_[index] == program_command) break;
        }

        if (selectable_)
        {
            selected_command_index_ = index;

            for (size_t i = 0; i < command_list_.size(); ++i)
            {
                command_list_[i]->setSelected(i == index);
            }
        }

        onCommandClicked_.emit(index);
    });
}



void ProgramList::clearCommands()
{
    command_container_->clear();
    command_list_.clear();
}



void ProgramList::setCommandSelected(size_t index)
{
    if (!selectable_ || index >= command_list_.size()) return;

    selected_command_index_ = index;

    for (size_t i = 0; i < command_list_.size(); ++i)
    {
        command_list_[i]->setSelected(i == index);
    }
}


void ProgramList::setCommandStatus(size_t index, ProgramCommand::Status status)
{
    if (index >= command_list_.size()) return;

    command_list_[index]->setStatus(status);
}


void ProgramList::removeCommand(size_t index)
{
    if (index >= command_list_.size()) return;

    if (selected_command_index_ && *selected_command_index_ == index)
    {
        selected_command_index_ = std::nullopt;
    }
    else if (selected_command_index_ && *selected_command_index_ > index)
    {
        --(*selected_command_index_);
    }

    command_container_->removeWidget(command_list_[index]);
    command_list_.erase(command_list_.begin() + index);
}