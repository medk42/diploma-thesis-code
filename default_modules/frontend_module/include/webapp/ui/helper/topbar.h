#pragma once

#include "button.h"

#include <Wt/WContainerWidget.h>
#include <Wt/WText.h>
#include <Wt/WSignal.h>

#include <vector>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    

    class TopBar : public Wt::WContainerWidget
    {
    public:
        TopBar(const std::string& title, std::vector<std::vector<ButtonDescription>> left_buttons, std::vector<std::vector<ButtonDescription>> right_buttons);

        Wt::Signal<size_t>& onButtonClicked() { return onButtonClicked_; } // index in order of left buttons, then right buttons

        void setEnabled(size_t button_index, bool enabled) { buttons_.at(button_index)->setEnabled(enabled); }
        bool isEnabled(size_t button_index) const { return buttons_.at(button_index)->isEnabled(); }

    private:
        Wt::Signal<size_t> onButtonClicked_;
        std::vector<Button*> buttons_;
        
    };
}