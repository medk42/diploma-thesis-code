#pragma once

#include <string>

#include <Wt/WPushButton.h>
#include <Wt/WSignal.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    enum class ButtonStyle
    {
        Primary,   // Prominent design
        Secondary, // Less prominent design
        Danger     // Red button
    };

    struct ButtonDescription
    {
        std::string text_;
        ButtonStyle style_;
        bool enabled_ = true;
    };

    class Button : public Wt::WPushButton
    {
    public:
        Button(const std::string& text, ButtonStyle style, bool enabled = true);

        void setEnabled(bool enabled);
        bool isEnabled() const;

        Wt::Signal<>& clicked() { return clicked_; }

    private:
        bool enabled_;
        Wt::Signal<> clicked_;
    };
}