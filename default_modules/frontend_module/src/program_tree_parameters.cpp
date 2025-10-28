#include "webapp/ui/helper/program_tree_parameters.h"

#include <Wt/WText.h>
#include <Wt/WStackedWidget.h>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

ProgramTreeParameters::ProgramTreeParameters(
    const std::vector<p_desc::ParameterDescription>& auto_parameters,
    const std::vector<p_desc::ParameterDescription>& required_parameters,
    const std::vector<p_desc::ParameterDescription>& advanced_parameters
)
{
    setStyleClass("program-tree-parameters");

    auto parameter_section = addWidget(std::make_unique<Wt::WStackedWidget>());
    parameter_section->setStyleClass("program-tree-parameter-section");
    auto required_parameters_section = parameter_section->addWidget(std::make_unique<Wt::WContainerWidget>());
    required_parameters_section->setStyleClass("parameter-list");
    auto advanced_parameters_section = parameter_section->addWidget(std::make_unique<Wt::WContainerWidget>());
    advanced_parameters_section->setStyleClass("parameter-list");
    parameter_section->setCurrentIndex(0);
    
    for (const auto& param : auto_parameters)
    {
        required_parameters_section->addWidget(std::make_unique<RightModuleParameter>(param));
    }
    for (const auto& param : required_parameters)
    {
        required_parameters_section->addWidget(std::make_unique<RightModuleParameter>(param));
    }

    for (const auto& param : advanced_parameters)
    {
        advanced_parameters_section->addWidget(std::make_unique<RightModuleParameter>(param));
    }

    tab_selector_ = addWidget(std::make_unique<TabSelector>(std::vector<std::string>{"Required", "Advanced"}));
    tab_selector_->setSelectedTab(0);

    tab_selector_->onTabSelected().connect([parameter_section](size_t index) {
        parameter_section->setCurrentIndex(index);
    });
}