#include "webapp/ui/helper/topbar.h"

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

TopBar::TopBar(const std::string& title, std::vector<ButtonDescription> left_buttons, std::vector<ButtonDescription> right_buttons)
{
    setStyleClass("top-bar");

    auto title_widget = addWidget(std::make_unique<Wt::WText>(title));
    title_widget->setStyleClass("top-bar-title");

    auto button_container = addWidget(std::make_unique<Wt::WContainerWidget>());
    button_container->setStyleClass("top-bar-buttons");

    auto left_button_container = button_container->addWidget(std::make_unique<Wt::WContainerWidget>());
    left_button_container->setStyleClass("top-bar-buttons-left");

    for (size_t i = 0; i < left_buttons.size(); ++i)
    {
        const auto& desc = left_buttons[i];
        auto button = left_button_container->addWidget(std::make_unique<Button>(
            desc.text_, 
            desc.style_, 
            desc.enabled_
        ));

        size_t callback_index = i;
        button->clicked().connect([this, callback_index]() {
            onButtonClicked_.emit(callback_index);
        });
    }

    auto right_button_container = button_container->addWidget(std::make_unique<Wt::WContainerWidget>());
    right_button_container->setStyleClass("top-bar-buttons-right");

    for (size_t i = 0; i < right_buttons.size(); ++i)
    {
        const auto& desc = right_buttons[i];
        auto button = right_button_container->addWidget(std::make_unique<Button>(
            desc.text_, 
            desc.style_, 
            desc.enabled_
        ));

        size_t callback_index = left_buttons.size() + i;
        button->clicked().connect([this, callback_index]() {
            onButtonClicked_.emit(callback_index);
        });
    }
}