#include "webapp/ui/helper/program_tree.h"
#include "webapp/ui/helper/program_tree_parameters.h"
#include "webapp/ui/helper/program_command.h"

#include "webapp/ui/helper/program_tree_dummy_params.h"

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

ProgramTree::ProgramTree()
{
    setStyleClass("program-tree");

    auto program_container = addWidget(std::make_unique<Wt::WContainerWidget>());
    program_container->setStyleClass("program-tree-container");

    auto program_list = program_container->addWidget(std::make_unique<ProgramList>("PROGRAM", "program-tree-list", true));
    auto available_command_list = program_container->addWidget(std::make_unique<ProgramList>("AVAILABLE", "program-tree-available-commands", false));

    auto parameters_container = addWidget(std::make_unique<Wt::WContainerWidget>());
    parameters_container->setStyleClass("program-tree-parameters-container");

    auto [dummy_auto, dummy_required, dummy_advanced] = generateParams();

    auto parameters = parameters_container->addWidget(
        std::make_unique<ProgramTreeParameters>(
            dummy_auto, dummy_required, dummy_advanced
        )
    );

    program_list->addCommand("WELD", ProgramCommand::Status::Normal);
    program_list->addCommand("P & P", ProgramCommand::Status::Warning);
    program_list->addCommand("MOVE L", ProgramCommand::Status::Normal);
    program_list->addCommand("MOVE J", ProgramCommand::Status::Invalid);
    program_list->addCommand("MOVE J", ProgramCommand::Status::Normal);

    available_command_list->addCommand("CUT", ProgramCommand::Status::Normal);
    available_command_list->addCommand("DRILL", ProgramCommand::Status::Normal);
    available_command_list->addCommand("GRIND", ProgramCommand::Status::Normal);
    available_command_list->addCommand("PAINT", ProgramCommand::Status::Normal);
}  