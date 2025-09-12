#include "webapp/ui/helper/reusable_dialog.h"


#include <Wt/WEvent.h>


using namespace aergo::default_modules::frontend_module::webapp::ui::helper;



ReusableDialog::ReusableDialog(std::string title, std::string content, std::vector<ButtonDescription> buttons)
{
    auto overlay = addWidget(std::make_unique<Wt::WContainerWidget>());
    overlay->setStyleClass("dialog-overlay-blocker");
    overlay->mouseWentDown().connect([this](const Wt::WMouseEvent&) {
        onBackgroundClicked_.emit();
    });

    auto dialog_window = addWidget(std::make_unique<Wt::WContainerWidget>());
    dialog_window->setStyleClass("dialog-window");
    
    auto dialog_window_inner = dialog_window->addWidget(std::make_unique<Wt::WContainerWidget>());
    dialog_window_inner->setStyleClass("dialog-window-inner");

    title_widget_ = dialog_window_inner->addWidget(std::make_unique<Wt::WText>(title));
    title_widget_->setStyleClass("dialog-title");
    content_widget_ = dialog_window_inner->addWidget(std::make_unique<Wt::WText>(content));
    content_widget_->setStyleClass("dialog-content");

    auto buttons_container = dialog_window_inner->addWidget(std::make_unique<Wt::WContainerWidget>());
    buttons_container->setStyleClass("dialog-buttons");
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        const auto& desc = buttons[i];
        auto button = buttons_container->addWidget(std::make_unique<Button>(desc.text_, desc.style_, desc.enabled_));
        buttons_.emplace_back(button);
        button->clicked().connect([this, i]() {
            onButtonClicked_.emit(i);
        });
    }
}



void ReusableDialog::setEnabled(size_t button_id, bool enabled)
{
    if (button_id >= buttons_.size() || !buttons_[button_id])
        return;

    buttons_[button_id]->setEnabled(enabled);
}