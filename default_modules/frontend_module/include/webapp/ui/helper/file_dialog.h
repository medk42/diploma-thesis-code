#pragma once

#include "webapp/ui/helper/input_fields.h"

#include <string>
#include <vector>

#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class FileDialog : public Wt::WContainerWidget
    {
    public:
        FileDialog(std::string title, std::vector<std::string> existing_files, std::string selected_file, std::string accept_button_title);

        Wt::Signal<std::string>& onAcceptClicked() { return onAcceptClicked_; }  // accept button clicked, emits selected file
        Wt::Signal<>& onCancelClicked() { return onCancelClicked_; }  // cancel button clicked

    private:
        Wt::Signal<std::string> onAcceptClicked_;  // accept button clicked, emits selected file
        Wt::Signal<> onCancelClicked_;  // cancel button clicked

        Wt::WText* current_selected_file_ { nullptr };
    };
}