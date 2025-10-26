#include "webapp/ui/helper/program_tree_parameters.h"

#include <Wt/WText.h>
#include <Wt/WStackedWidget.h>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

ProgramTreeParameters::ProgramTreeParameters()
{
    setStyleClass("program-tree-parameters");

    auto parameter_section = addWidget(std::make_unique<Wt::WStackedWidget>());
    parameter_section->setStyleClass("program-tree-parameter-section");
    parameter_section->addWidget(std::make_unique<Wt::WText>("Required Parameters"));
    parameter_section->addWidget(std::make_unique<Wt::WText>("Advanced Parameters"));
    parameter_section->setCurrentIndex(0);

    tab_selector_ = addWidget(std::make_unique<TabSelector>(std::vector<std::string>{"Required", "Advanced"}));
    tab_selector_->setSelectedTab(0);

    tab_selector_->onTabSelected().connect([parameter_section](size_t index) {
        parameter_section->setCurrentIndex(index);
    });
}