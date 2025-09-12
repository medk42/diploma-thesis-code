#include "webapp/ui/helper/left_module_button.h"


using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

#include <iostream>

LeftModuleButton::LeftModuleButton(std::string label_str, bool show_ready, bool is_add_new)
: show_ready_(show_ready)
{
    setStyleClass("module-item");
    if (is_add_new) addStyleClass("add-new");
    setSelected(false);

    label_ = addWidget(std::make_unique<Wt::WText>(label_str));
    label_->setStyleClass("module-name");

    if (show_ready_)
    {
        tag_ = addWidget(std::make_unique<Wt::WText>(""));
        tag_->setStyleClass("tag");
        setReady(false);
    }

    clicked().connect([this]{
        on_pressed_.emit();
    });
}



void LeftModuleButton::setReady(bool ready)
{
    if (show_ready_)
    {
        if (ready)
        {
            tag_->setText("ready");
            tag_->addStyleClass("tag-ready");
            tag_->removeStyleClass("tag-unready");
        }
        else
        {
            tag_->setText("unready");
            tag_->addStyleClass("tag-unready");
            tag_->removeStyleClass("tag-ready");
        }
    }
}



void LeftModuleButton::setSelected(bool selected)
{
    if (selected)
    {
        addStyleClass("selected");
    }
    else
    {
        removeStyleClass("selected");
    }
}