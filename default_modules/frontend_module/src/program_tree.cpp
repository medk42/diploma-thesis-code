#include "webapp/ui/helper/program_tree.h"
#include "webapp/ui/helper/program_tree_parameters.h"

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

    program_list->addCommand("WELD");
    program_list->addCommand("P & P");
    program_list->addCommand("MOVE L");
    program_list->addCommand("MOVE J");
    program_list->addCommand("MOVE J");

    available_command_list->addCommand("CUT");
    available_command_list->addCommand("DRILL");
    available_command_list->addCommand("GRIND");
    available_command_list->addCommand("PAINT");
}  