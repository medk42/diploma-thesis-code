#pragma once

#include "button.h"

#include <Wt/WContainerWidget.h>
#include <Wt/WText.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    

    class TopBar : public Wt::WContainerWidget
    {
    public:
        TopBar(const std::string& title, std::vector<ButtonDescription> left_buttons, std::vector<ButtonDescription> right_buttons);

        Wt::Signal<size_t>& onButtonClicked() { return onButtonClicked_; } // index in order of left buttons, then right buttons

    private:
        Wt::Signal<size_t> onButtonClicked_;
        
    };
}