#include "webapp/ui/helper/file_dialog.h"


#include "webapp/ui/helper/button.h"
#include <Wt/WText.h>


using namespace aergo::default_modules::frontend_module::webapp::ui::helper;


FileDialog::FileDialog(std::string title, std::vector<std::string> existing_files, std::string selected_file, std::string accept_button_title)
{
    auto overlay = addWidget(std::make_unique<Wt::WContainerWidget>());
    overlay->setStyleClass("file-dialog-overlay-blocker");
    overlay->mouseWentDown().connect([this](const Wt::WMouseEvent&) { }); // prevent clicks to underlying elements

    auto dialog_window = addWidget(std::make_unique<Wt::WContainerWidget>());
    dialog_window->setStyleClass("file-dialog-window");
    
    auto dialog_window_inner = dialog_window->addWidget(std::make_unique<Wt::WContainerWidget>());
    dialog_window_inner->setStyleClass("dialog-window-inner");

    auto title_widget = dialog_window_inner->addWidget(std::make_unique<Wt::WText>(title));
    title_widget->setStyleClass("dialog-title");
    auto file_list_container = dialog_window_inner->addWidget(std::make_unique<Wt::WContainerWidget>());
    file_list_container->setStyleClass("file-dialog-file-list");
    auto file_list_container_scrollable = file_list_container->addWidget(std::make_unique<Wt::WContainerWidget>());
    file_list_container_scrollable->setStyleClass("file-dialog-file-list-scrollable");
    auto file_list_container_inner = file_list_container_scrollable->addWidget(std::make_unique<Wt::WContainerWidget>());
    file_list_container_inner->setStyleClass("file-dialog-file-list-inner");

    auto selected_file_widget = dialog_window_inner->addWidget(std::make_unique<TextLineEdit>(selected_file));
    selected_file_widget->addStyleClass("auto-width");

    for (const auto& file : existing_files)
    {
        auto file_item = file_list_container_inner->addWidget(std::make_unique<Wt::WText>(file));
        file_item->setStyleClass("file-dialog-file-item");
        if (file == selected_file)
        {
            current_selected_file_ = file_item;
            file_item->addStyleClass("selected");
        }

        file_item->clicked().connect([this, file, file_item, selected_file_widget]() {
            if (current_selected_file_ != nullptr)
            {
                current_selected_file_->removeStyleClass("selected");
            }
            current_selected_file_ = file_item;
            current_selected_file_->addStyleClass("selected");
            selected_file_widget->setValue(file);
        });
    }    

    auto buttons_container = dialog_window_inner->addWidget(std::make_unique<Wt::WContainerWidget>());
    buttons_container->setStyleClass("dialog-buttons");
    auto cancel_button = buttons_container->addWidget(std::make_unique<Button>("Cancel", ButtonStyle::Secondary, true));
    auto accept_button = buttons_container->addWidget(std::make_unique<Button>(accept_button_title, ButtonStyle::Primary, true));
    cancel_button->clicked().connect([this]() { onCancelClicked_.emit(); });
    accept_button->clicked().connect([this, selected_file_widget]() { 
        onAcceptClicked_.emit(std::get<std::string>(selected_file_widget->value().value_or(""))); 
    });
    
}