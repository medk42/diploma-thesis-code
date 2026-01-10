#pragma once


#include "webapp/ui/helper/camera_container.h"
#include "webapp/ui/helper/scene_container.h"
#include "webapp/ui/helper/program_tree.h"
#include "webapp/ui/helper/reusable_dialog.h"


#include "module_common/base_module.h"


#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>


namespace aergo::default_modules::frontend_module::webapp::ui
{
    class MainVisualizationUi : public Wt::WContainerWidget
    {
    public:
        MainVisualizationUi(aergo::module::BaseModule* base_module, helper::ProgramTreeState& program_state_unsafe, std::function<void(std::function<void()>)> with_frontend_state_lock);

        void reloadAvailableUsecases() { program_tree_->reloadAvailableUsecases(); } // reload available usecases in program tree

        void updateFrame(std::vector<uint8_t>&& jpeg_data) { camera_container_->updateFrame(std::move(jpeg_data)); } // update camera frame
        
        Wt::Signal<>& onSetupClicked() { return onSetupClicked_; } // setup button clicked

        helper::SceneContainer* getSceneContainer() { return scene_container_; }

    private:
        void programTreeButtonClicked(size_t index);

        void scanSceneRequested();
        void moveToPositionRequested();
        void setPositionRequested();

        void showMoveToPositionDialog();
        void dismissMoveToPositionDialog();
        void showSetPositionDialog();
        void dismissSetPositionDialog();
        void showInfoDialog(std::string title, std::string content);
        void dismissInfoDialog();

        helper::CameraContainer* camera_container_{ nullptr };
        helper::SceneContainer* scene_container_{ nullptr };
        helper::ProgramTree* program_tree_{ nullptr };

        Wt::Signal<> onSetupClicked_; // setup button clicked

        aergo::module::BaseModule* base_module_{ nullptr };
        std::function<void(std::function<void()>)> with_frontend_state_lock_;

        helper::ReusableDialog* move_to_position_dialog_{ nullptr };
        helper::ReusableDialog* set_position_dialog_{ nullptr };
        helper::ReusableDialog* info_dialog_{ nullptr };
    };
}