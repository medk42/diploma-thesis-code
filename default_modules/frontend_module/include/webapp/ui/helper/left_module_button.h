#pragma once

#include <Wt/WContainerWidget.h>
#include <Wt/WText.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class LeftModuleButton : public Wt::WContainerWidget
    {
    public:
        LeftModuleButton(std::string label_str, bool show_ready, bool is_add_new);

        Wt::Signal<>& onPressed() { return on_pressed_; }

        void setReady(bool ready);
        void setSelected(bool selected);
        void setLabel(std::string label_str) { label_->setText(label_str); }

    private:
        bool show_ready_;
        Wt::Signal<> on_pressed_;

        Wt::WText* label_{ nullptr };
        Wt::WText* tag_{ nullptr };
    };
}