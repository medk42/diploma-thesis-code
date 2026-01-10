#include "webapp/ui/main_visualization_ui.h"

#include "webapp/ui/helper/topbar.h"

#include "module_helpers/scene_detection_helper/message_types.h"
#include "module_common/module_interface_.h"
#include "module_common/serialization_helper.h"

#undef ERROR // Gotta love Windows.h

using namespace aergo::default_modules::frontend_module::webapp::ui;
namespace sdh = aergo::module::helpers::scene_detection_helper;
using namespace aergo::module;

MainVisualizationUi::MainVisualizationUi(
    aergo::module::BaseModule* base_module, 
    helper::ProgramTreeState& program_state_unsafe, 
    std::function<void(std::function<void()>)> with_frontend_state_lock
)
: base_module_(base_module), 
  with_frontend_state_lock_(with_frontend_state_lock)
{
    setStyleClass("main-visualization-ui");

    auto top_bar = addWidget(std::make_unique<helper::TopBar>(
        "Aergo",
        std::vector<std::vector<helper::ButtonDescription>>{
            std::vector<helper::ButtonDescription> {
                {"Setup", helper::ButtonStyle::Secondary, true}
            }
        },
        std::vector<std::vector<helper::ButtonDescription>>{
            std::vector<helper::ButtonDescription> {
                {"New", helper::ButtonStyle::Secondary, true},
                {"Save", helper::ButtonStyle::Secondary, true},
                {"Load", helper::ButtonStyle::Secondary, true}
            },
            std::vector<helper::ButtonDescription> {
                {"Cut", helper::ButtonStyle::Secondary, true},
                {"Copy", helper::ButtonStyle::Secondary, true},
                {"Paste", helper::ButtonStyle::Secondary, true}
            },
            std::vector<helper::ButtonDescription> {
                {"Start", helper::ButtonStyle::Primary, false},
                {"Simulate", helper::ButtonStyle::Secondary, false},
                {"Stop", helper::ButtonStyle::Danger, false},
                {"Pause", helper::ButtonStyle::Secondary, false},
                {"Resume", helper::ButtonStyle::Secondary, false}
            }
        }
    ));

    top_bar->onButtonClicked().connect([this](size_t index){
        if (index == 0) // Setup
        {
            onSetupClicked_.emit();
        }
        programTreeButtonClicked(index);
    });

    auto content_container = addWidget(std::make_unique<Wt::WContainerWidget>());
    content_container->setStyleClass("main-content-container");

    auto left_container = content_container->addWidget(std::make_unique<Wt::WContainerWidget>());
    left_container->setStyleClass("left-container");
    scene_container_ = left_container->addWidget(std::make_unique<helper::SceneContainer>(base_module, 16 /* ~60fps */));
    auto bottom_bar = left_container->addWidget(std::make_unique<helper::TopBar>(
        "",
        std::vector<std::vector<helper::ButtonDescription>>{
            std::vector<helper::ButtonDescription> {
                {"Move To Position", helper::ButtonStyle::Secondary, true},
                {"Set Position", helper::ButtonStyle::Secondary, true},
            }
        },
        std::vector<std::vector<helper::ButtonDescription>>{
            std::vector<helper::ButtonDescription> {
                {"Scan Scene", helper::ButtonStyle::Secondary, true}
            }
        }
    ));
    bottom_bar->onButtonClicked().connect([this](size_t index){
        if (index == 0) // Move To Position
        {
            showMoveToPositionDialog();
        }
        else if (index == 1) // Set Position
        {
            showSetPositionDialog();
        }
        else if (index == 2) // Scan Scene
        {
            scanSceneRequested();
        }
    });

    program_tree_ = content_container->addWidget(std::make_unique<helper::ProgramTree>(
        base_module_,
        program_state_unsafe,
        with_frontend_state_lock
    ));
    camera_container_ = content_container->addWidget(std::make_unique<helper::CameraContainer>());

    program_tree_->onButtonStateChanged().connect([top_bar](helper::ProgramTreeButtonState state){
        top_bar->setEnabled(7, state.start_program_enabled);     // Start
        top_bar->setEnabled(8, state.simulate_program_enabled);  // Simulate
        top_bar->setEnabled(9, state.stop_program_enabled);      // Stop
        top_bar->setEnabled(10, state.pause_program_enabled);    // Pause
        top_bar->setEnabled(11, state.resume_program_enabled);   // Resume
    });
}



void MainVisualizationUi::programTreeButtonClicked(size_t index)
{
    with_frontend_state_lock_([this, index]() {
        switch (index)
        {
            case 1:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::NewProgram);
                return;
            case 2:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::SaveProgram);
                return;
            case 3:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::LoadProgram);
                return;
            case 4:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::CutCommand);
                return;
            case 5:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::CopyCommand);
                return;
            case 6:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::PasteCommand);
                return;
            case 7:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::StartProgram);
                return;
            case 8:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::SimulateProgram);
                return;
            case 9:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::StopProgram);
                return;
            case 10:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::PauseProgram);
                return;
            case 11:
                program_tree_->onButtonClicked(helper::ProgramTreeButtons::ResumeProgram);
                return;
            default:
                base_module_->log(aergo::module::logging::LogType::ERROR, "Unknown program tree button index: " + std::to_string(index));
                break;
        }
    });
}


void MainVisualizationUi::showMoveToPositionDialog()
{
    dismissMoveToPositionDialog();
    move_to_position_dialog_ = addWidget(std::make_unique<helper::ReusableDialog>(
        "Move To Default Position",
        "Move the robot to the default position. Please watch the robot to ensure it does not collide with anything.",
        std::vector<helper::ButtonDescription> {
            helper::ButtonDescription("Cancel", helper::ButtonStyle::Secondary, true),
            helper::ButtonDescription("Move", helper::ButtonStyle::Danger, true)
        }
    ));
    move_to_position_dialog_->onBackgroundClicked().connect([this]() {
        dismissMoveToPositionDialog();
    });
    move_to_position_dialog_->onButtonClicked().connect([this](size_t button_index) {
        if (button_index == 0) // Cancel
        {
            dismissMoveToPositionDialog();
        }
        else if (button_index == 1) // Move
        {
            moveToPositionRequested();
            dismissMoveToPositionDialog();
        }
    });
}


void MainVisualizationUi::dismissMoveToPositionDialog()
{
    if (move_to_position_dialog_)
    {
        removeWidget(move_to_position_dialog_);
        move_to_position_dialog_ = nullptr;
    }
}


void MainVisualizationUi::showSetPositionDialog()
{
    dismissSetPositionDialog();
    set_position_dialog_ = addWidget(std::make_unique<helper::ReusableDialog>(
        "Set Position",
        "Update the default position of the robot. This position will be used as the default position for the robot on \"Move To Default Position\" button press and at the start and end of programs.",
        std::vector<helper::ButtonDescription> {
            helper::ButtonDescription("Cancel", helper::ButtonStyle::Secondary, true),
            helper::ButtonDescription("Set", helper::ButtonStyle::Danger, true)
        }
    ));
    set_position_dialog_->onBackgroundClicked().connect([this]() {
        dismissSetPositionDialog();
    });
    set_position_dialog_->onButtonClicked().connect([this](size_t button_index) {
        if (button_index == 0) // Cancel
        {
            dismissSetPositionDialog();
        }
        else if (button_index == 1) // Set
        {
            setPositionRequested();
            dismissSetPositionDialog();
        }
    });
}


void MainVisualizationUi::dismissSetPositionDialog()
{
    if (set_position_dialog_)
    {
        removeWidget(set_position_dialog_);
        set_position_dialog_ = nullptr;
    }
}


void MainVisualizationUi::showInfoDialog(std::string title, std::string content)
{
    dismissInfoDialog();
    info_dialog_ = addWidget(std::make_unique<helper::ReusableDialog>(title, content, std::vector<helper::ButtonDescription> { helper::ButtonDescription("OK", helper::ButtonStyle::Secondary, true) }));
    info_dialog_->onBackgroundClicked().connect([this]() { dismissInfoDialog(); });
    info_dialog_->onButtonClicked().connect([this](size_t button_index) { dismissInfoDialog(); });
}


void MainVisualizationUi::dismissInfoDialog()
{
    if (info_dialog_)
    {
        removeWidget(info_dialog_);
        info_dialog_ = nullptr;
    }
}


void MainVisualizationUi::scanSceneRequested()
{

    uint32_t scene_detection_request_channel_id;
    if (!base_module_->getRequestChannelByName(sdh::scene_detection_request_consumer.channel_type_identifier_, scene_detection_request_channel_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Scene scan request failed: Frontend module does not have scene detection request channel.");
        showInfoDialog("Scene Scan Failed", "Frontend module does not have scene detection request channel.");
        return;
    }

    ICoreControl* core_control = base_module_->getCoreControl();
    if (core_control == nullptr)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Scene scan request failed: Frontend module does not have core control.");
        showInfoDialog("Scene Scan Failed", "Frontend module does not have core control.");
        return;
    }

    message::SharedDataBlob existing_scene_detection_modules = core_control->getExistingResponseChannelsByName(sdh::scene_detection_request_consumer.channel_type_identifier_);
    if (!existing_scene_detection_modules.valid() || existing_scene_detection_modules.data() == nullptr)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Scene scan request failed: Response from core control is invalid.");
        showInfoDialog("Scene Scan Failed", "Response from core control is invalid.");
        return;
    }
    
    deserialize::des::BufferReader reader(existing_scene_detection_modules.data(), existing_scene_detection_modules.size());
    std::vector<aergo::module::ChannelIdentifier> existing_scene_detection_modules_list;
    if (!deserialize::readExistingChannels(reader, existing_scene_detection_modules_list))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Scene scan request failed: Failed to deserialize existing scene detection modules list.");
        showInfoDialog("Scene Scan Failed", "Failed to deserialize existing scene detection modules list.");
        return;
    }

    if (existing_scene_detection_modules_list.size() == 0)
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "Scene scan request failed: There are no scene detection modules running. Please add a scene detection module to the program.");
        showInfoDialog("Scene Scan Failed", "There are no scene detection modules running. Please add a scene detection module to the program.");
        return;
    }

    // send request to the first channel in the list
    aergo::module::ChannelIdentifier target_channel = existing_scene_detection_modules_list[0];

    sdh::Request request = sdh::Request::readScene();
    message::MessageHeader message = message::MessageHeader::Message(&request);
    uint64_t request_id = base_module_->sendRequest(scene_detection_request_channel_id, target_channel, message);
}


void MainVisualizationUi::moveToPositionRequested()
{
    base_module_->log(aergo::module::logging::LogType::INFO, "Frontend module: move to position requested");
}


void MainVisualizationUi::setPositionRequested()
{
    base_module_->log(aergo::module::logging::LogType::INFO, "Frontend module: set position requested");
}