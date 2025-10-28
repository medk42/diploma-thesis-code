#include "webapp/ui/helper/topbar.h"

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

TopBar::TopBar(const std::string& title, std::vector<std::vector<ButtonDescription>> left_buttons, std::vector<std::vector<ButtonDescription>> right_buttons)
{
    setStyleClass("top-bar");

    auto title_widget = addWidget(std::make_unique<Wt::WText>(title));
    title_widget->setStyleClass("top-bar-title");

    auto button_container = addWidget(std::make_unique<Wt::WContainerWidget>());
    button_container->setStyleClass("top-bar-buttons");

    auto left_button_container = button_container->addWidget(std::make_unique<Wt::WContainerWidget>());
    left_button_container->setStyleClass("top-bar-buttons-left");

    size_t button_index = 0;

    for (size_t outer = 0 ; outer < left_buttons.size(); ++outer)
    {
        if (outer > 0)
        {
            // add spacer between button groups
            auto spacer = left_button_container->addWidget(std::make_unique<Wt::WContainerWidget>());
            spacer->setStyleClass("top-bar-button-group-spacer");
        }

        for (size_t i = 0; i < left_buttons[outer].size(); ++i)
        {
            const auto& desc = left_buttons[outer][i];
            auto button = left_button_container->addWidget(std::make_unique<Button>(
                desc.text_, 
                desc.style_, 
                desc.enabled_
            ));

            button->clicked().connect([this, button_index]() {
                onButtonClicked_.emit(button_index);
            });
            ++button_index;
            buttons_.push_back(button);
        }
    }
    

    auto right_button_container = button_container->addWidget(std::make_unique<Wt::WContainerWidget>());
    right_button_container->setStyleClass("top-bar-buttons-right");

    for (size_t outer = 0 ; outer < right_buttons.size(); ++outer)
    {
        if (outer > 0)
        {
            // add spacer between button groups
            auto spacer = right_button_container->addWidget(std::make_unique<Wt::WContainerWidget>());
            spacer->setStyleClass("top-bar-button-group-spacer");
        }

        for (size_t i = 0; i < right_buttons[outer].size(); ++i)
        {
            const auto& desc = right_buttons[outer][i];
            auto button = right_button_container->addWidget(std::make_unique<Button>(
                desc.text_, 
                desc.style_, 
                desc.enabled_
            ));

            button->clicked().connect([this, button_index]() {
                onButtonClicked_.emit(button_index);
            });
            ++button_index;
            buttons_.push_back(button);
        }
    }
}