#pragma once

#include "tab_selector.h"
#include "right_module_parameter.h"

#include "module_helpers/parameter_description/parameter_description.h"

#include <Wt/WContainerWidget.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    namespace p_desc = aergo::module::helpers::parameter_description;

    class ProgramTreeParameters : public Wt::WContainerWidget
    {
    public:
        ProgramTreeParameters(
            const std::vector<p_desc::ParameterDescription>& auto_parameters,
            const std::vector<p_desc::ParameterDescription>& required_parameters,
            const std::vector<p_desc::ParameterDescription>& advanced_parameters
        );

    private:
        TabSelector* tab_selector_{ nullptr };
    };
}