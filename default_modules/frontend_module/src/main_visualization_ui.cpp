#include "webapp/ui/main_visualization_ui.h"

#include "webapp/ui/helper/topbar.h"

using namespace aergo::default_modules::frontend_module::webapp::ui;

MainVisualizationUi::MainVisualizationUi()
{
    setStyleClass("main-visualization-ui");

    auto top_bar = addWidget(std::make_unique<helper::TopBar>(
        "Aergo",
        std::vector<helper::ButtonDescription> {
            {"Setup", helper::ButtonStyle::Secondary, true}
        },
        std::vector<helper::ButtonDescription> {}
    ));

    top_bar->onButtonClicked().connect([this](size_t index){
        if (index == 0) // Setup
        {
            onSetupClicked_.emit();
        }
    });

    camera_container_ = addWidget(std::make_unique<helper::CameraContainer>());
}