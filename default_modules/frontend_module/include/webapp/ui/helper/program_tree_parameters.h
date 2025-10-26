#pragma once

#include "tab_selector.h"

#include <Wt/WContainerWidget.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class ProgramTreeParameters : public Wt::WContainerWidget
    {
    public:
        ProgramTreeParameters();

    private:
        TabSelector* tab_selector_{ nullptr };
    };
}