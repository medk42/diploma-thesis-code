#include "webapp/ui/helper/program_command.h"

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

ProgramCommand::ProgramCommand(const std::string& command_name)
{
    setStyleClass("program-command");
    setText(command_name);

    clicked().connect([this]() {
        onClick_.emit();
    });
}