#pragma once


#include "webapp/ui/helper/camera_container.h"
#include "webapp/ui/helper/scene_container.h"
#include "webapp/ui/helper/program_tree.h"
#include "webapp/ui/helper/reusable_dialog.h"


#include "module_common/base_module.h"
#include "module_helpers/robot_interface/features/robot_control/messages.h"


#include <vector>
#include <mutex>


#include <Wt/WContainerWidget.h>
#include <Wt/WSignal.h>


namespace aergo::default_modules::frontend_module::webapp::ui
{
    namespace ri = aergo::module::helpers::robot_interface;
    namespace rc = ri::robot_control;
    struct MainVisualizationState
    {
        std::mutex main_visualization_state_mutex_;
        bool robot_status_message_valid_{ false };
        rc::status_messages::deserialization::StatusMessage robot_status_message_{};
    };


    class MainVisualizationUi : public Wt::WContainerWidget
    {
    public:
        MainVisualizationUi(aergo::module::BaseModule* base_module, helper::ProgramTreeState& program_state_unsafe, MainVisualizationState& main_visualization_state_unsafe, std::function<void(std::function<void()>)> with_frontend_state_lock);

        void reloadAvailableUsecases() { program_tree_->reloadAvailableUsecases(); } // reload available usecases in program tree

        void updateFrame(std::vector<uint8_t>&& jpeg_data) { camera_container_->updateFrame(std::move(jpeg_data)); } // update camera frame
        
        Wt::Signal<>& onSetupClicked() { return onSetupClicked_; } // setup button clicked
        Wt::Signal<>& onReloadVisualizationClicked() { return onReloadVisualizationClicked_; } // reload visualization button clicked

        helper::SceneContainer* getSceneContainer() { return scene_container_; }

    private:
        void programTreeButtonClicked(size_t index);

        void setPositionPressed();
        void moveToPositionPressed();

        void scanSceneRequested();
        void moveToPositionRequested(const std::vector<double>& joint_positions);
        void setPositionRequested(const std::vector<double>& joint_positions);

        bool loadDefaultJointPositions(std::vector<double>& out_joint_positions, std::string& out_err_msg);
        void saveDefaultJointPositions(const std::vector<double>& joint_positions);

        void showMoveToPositionDialog(std::vector<double> joint_positions);
        void dismissMoveToPositionDialog();
        void showSetPositionDialog(std::vector<double> joint_positions);
        void dismissSetPositionDialog();
        void showInfoDialog(std::string title, std::string content);
        void dismissInfoDialog();

        helper::CameraContainer* camera_container_{ nullptr };
        helper::SceneContainer* scene_container_{ nullptr };
        helper::ProgramTree* program_tree_{ nullptr };

        Wt::Signal<> onSetupClicked_; // setup button clicked
        Wt::Signal<> onReloadVisualizationClicked_; // reload visualization button clicked

        aergo::module::BaseModule* base_module_{ nullptr };
        MainVisualizationState& main_visualization_state_unsafe_;
        std::function<void(std::function<void()>)> with_frontend_state_lock_;

        aergo::module::BaseModule::AllocatorPtr allocator_;

        helper::ReusableDialog* move_to_position_dialog_{ nullptr };
        helper::ReusableDialog* set_position_dialog_{ nullptr };
        helper::ReusableDialog* info_dialog_{ nullptr };
    };
}