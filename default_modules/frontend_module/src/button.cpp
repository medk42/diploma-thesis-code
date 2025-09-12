#include "webapp/ui/helper/button.h"

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

Button::Button(const std::string& text, ButtonStyle style, bool enabled)
: enabled_(enabled)
{
    setText(text);
    setEnabled(enabled);

    switch (style)
    {
        case ButtonStyle::Primary:
            addStyleClass("btn btn-primary");
            break;
        case ButtonStyle::Secondary:
            addStyleClass("btn btn-secondary");
            break;
        case ButtonStyle::Danger:
            addStyleClass("btn btn-danger");
            break;
        default:
            addStyleClass("btn");
            break;
    }

    if (enabled)
    {
        addStyleClass("btn-enabled");
    }
    else
    {
        addStyleClass("btn-disabled");
    }

    Wt::WPushButton::clicked().connect([this]() {
        if (enabled_)
        {
            clicked_.emit();
        }
    });
}



void Button::setEnabled(bool enabled)
{
    enabled_ = enabled;

    Wt::WPushButton::setEnabled(enabled);
    if (enabled)
    {
        removeStyleClass("btn-disabled");
        addStyleClass("btn-enabled");
    }
    else
    {
        removeStyleClass("btn-enabled");
        addStyleClass("btn-disabled");
    }
}



bool Button::isEnabled() const
{
    return Wt::WPushButton::isEnabled();
}