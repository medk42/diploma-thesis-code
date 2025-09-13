#pragma once

#include "button.h"

#include <Wt/WContainerWidget.h>
#include <Wt/WText.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class ReusableDialog : public Wt::WContainerWidget
    {
    public:
        ReusableDialog(std::string title, std::string content, std::vector<ButtonDescription> buttons);

        void setEnabled(size_t button_id, bool enabled); // enable or disable button with given id (in order of buttons given at construction time)
        void setTitle(const std::string& title) { title_widget_->setText(title); }
        void setContent(const std::string& content) { content_widget_->setText(content); }

        Wt::Signal<size_t>& onButtonClicked() { return onButtonClicked_; } // index in order of buttons given at construction time
        Wt::Signal<>& onBackgroundClicked() { return onBackgroundClicked_; }

    private:
        Wt::WText* title_widget_{ nullptr };
        Wt::WText* content_widget_{ nullptr };
        std::vector<Button*> buttons_;

        Wt::Signal<size_t> onButtonClicked_; // index in order of buttons given at construction time
        Wt::Signal<> onBackgroundClicked_;
    };
}