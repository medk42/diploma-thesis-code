#include "webapp/ui/helper/program_command.h"

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

ProgramCommand::ProgramCommand(const std::string& command_name, Status status)
{
    setStyleClass("program-command");
    setStatus(status);
    setText(command_name);

    clicked().connect([this]() {
        onClick_.emit();
    });
}


void ProgramCommand::setStatus(Status status)
{
    status_ = status;

    removeStyleClass("program-command-warning");
    removeStyleClass("program-command-invalid");

    if (status == Status::Warning)
    {
        addStyleClass("program-command-warning");
    }
    else if (status == Status::Invalid)
    {
        addStyleClass("program-command-invalid");
    }
}