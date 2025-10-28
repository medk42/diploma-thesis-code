#include "webapp/ui/helper/program_command.h"

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

ProgramCommand::ProgramCommand(const std::string& command_name, Status status)
{
    setStatus(status);
    setText(command_name);

    clicked().connect([this]() {
        onClick_.emit();
    });
}


void ProgramCommand::setStatus(Status status)
{
    status_ = status;

    if (status == Status::Normal)
    {
        setStyleClass("program-command");
    }
    else if (status == Status::Warning)
    {
        setStyleClass("program-command program-command-warning");
    }
    else if (status == Status::Invalid)
    {
        setStyleClass("program-command program-command-invalid");
    }
}