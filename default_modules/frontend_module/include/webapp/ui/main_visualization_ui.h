#pragma once


#include "webapp/ui/helper/camera_container.h"


#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>


namespace aergo::default_modules::frontend_module::webapp::ui
{
    class MainVisualizationUi : public Wt::WContainerWidget
    {
    public:
        MainVisualizationUi();

        void updateFrame(const std::vector<uint8_t>& jpeg_data) { camera_container_->updateFrame(jpeg_data); } // update camera frame
        
        Wt::Signal<>& onSetupClicked() { return onSetupClicked_; } // setup button clicked

    private:
        helper::CameraContainer* camera_container_{ nullptr };

        Wt::Signal<> onSetupClicked_; // setup button clicked
    };
}