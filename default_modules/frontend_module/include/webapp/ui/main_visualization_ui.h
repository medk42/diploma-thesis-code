#pragma once


#include "webapp/ui/helper/camera_container.h"
#include "webapp/ui/helper/scene_container.h"


#include "module_common/base_module.h"


#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>


namespace aergo::default_modules::frontend_module::webapp::ui
{
    class MainVisualizationUi : public Wt::WContainerWidget
    {
    public:
        MainVisualizationUi(aergo::module::BaseModule* base_module);

        void updateFrame(std::vector<uint8_t>&& jpeg_data) { camera_container_->updateFrame(std::move(jpeg_data)); } // update camera frame
        
        Wt::Signal<>& onSetupClicked() { return onSetupClicked_; } // setup button clicked

        helper::SceneContainer* getSceneContainer() { return scene_container_; }

    private:
        helper::CameraContainer* camera_container_{ nullptr };
        helper::SceneContainer* scene_container_{ nullptr };

        Wt::Signal<> onSetupClicked_; // setup button clicked

        aergo::module::BaseModule* base_module_{ nullptr };
    };
}