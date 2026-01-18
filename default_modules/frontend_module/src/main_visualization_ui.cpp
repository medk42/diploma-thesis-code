#include "webapp/ui/main_visualization_ui.h"

#include "webapp/ui/helper/topbar.h"

#include "module_helpers/scene_detection_helper/message_types.h"
#include "module_common/module_interface_.h"
#include "module_common/serialization_helper.h"
#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/robot_interface/message_types.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

#undef ERROR // Gotta love Windows.h

using namespace aergo::default_modules::frontend_module::webapp::ui;
namespace sdh = aergo::module::helpers::scene_detection_helper;
using namespace aergo::module;

namespace
{
    constexpr const char* MAIN_VISUALIZATION_DATA_JSON = "main_visualization_data.json";
}

MainVisualizationUi::MainVisualizationUi(
    aergo::module::BaseModule* base_module, 
    helper::ProgramTreeState& program_state_unsafe, 
    MainVisualizationState& main_visualization_state_unsafe,
    std::function<void(std::function<void()>)> with_frontend_state_lock
)
: base_module_(base_module), 
  main_visualization_state_unsafe_(main_visualization_state_unsafe),
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
                {"Reload Visualization", helper::ButtonStyle::Secondary, true},
                {"Scan Scene", helper::ButtonStyle::Secondary, true}
            }
        }
    ));
    bottom_bar->onButtonClicked().connect([this](size_t index){
        if (index == 0) // Move To Position
        {
            moveToPositionPressed();
        }
        else if (index == 1) // Set Position
        {
            setPositionPressed();
        }
        else if (index == 2) // Reload Visualization
        {
            onReloadVisualizationClicked_.emit();
        }
        else if (index == 3) // Scan Scene
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

    allocator_ = base_module_->createDynamicAllocator();
    if (allocator_ == nullptr)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Frontend module: failed to create allocator");
        return;
    }
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


void MainVisualizationUi::moveToPositionPressed()
{
    std::vector<double> joint_positions;
    std::string err_msg;
    if (!loadDefaultJointPositions(joint_positions, err_msg))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Move to position failed: " + err_msg);
        showInfoDialog("Move To Position Failed", "No default joint positions have been set. Please use 'Set Position' to set a default position first." + (err_msg.empty() ? "" : ": " + err_msg));
        return;
    }
    
    if (joint_positions.empty())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Move to position failed: No default joint positions loaded.");
        showInfoDialog("Move To Position Failed", "No default joint positions have been set. Please use 'Set Position' to set a default position first.");
        return;
    }

    showMoveToPositionDialog(joint_positions);
}


void MainVisualizationUi::showMoveToPositionDialog(std::vector<double> joint_positions)
{
    dismissMoveToPositionDialog();
    
    // Format joint positions string
    std::string joint_positions_str = "[";
    for (size_t i = 0; i < joint_positions.size(); ++i)
    {
        joint_positions_str += std::to_string(joint_positions[i]) + (i < joint_positions.size() - 1 ? ", " : "");
    }
    joint_positions_str += "]";
    
    std::string dialog_content = "Move the robot to the default position (" + std::to_string(joint_positions.size()) + " joints): " + joint_positions_str + ". Please watch the robot to ensure it does not collide with anything.";
    
    move_to_position_dialog_ = addWidget(std::make_unique<helper::ReusableDialog>(
        "Move To Default Position",
        dialog_content,
        std::vector<helper::ButtonDescription> {
            helper::ButtonDescription("Cancel", helper::ButtonStyle::Secondary, true),
            helper::ButtonDescription("Move", helper::ButtonStyle::Danger, true)
        }
    ));
    move_to_position_dialog_->onBackgroundClicked().connect([this]() {
        dismissMoveToPositionDialog();
    });
    move_to_position_dialog_->onButtonClicked().connect([this, joint_positions](size_t button_index) {
        if (button_index == 0) // Cancel
        {
            dismissMoveToPositionDialog();
        }
        else if (button_index == 1) // Move
        {
            moveToPositionRequested(joint_positions);
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


void MainVisualizationUi::showSetPositionDialog(std::vector<double> joint_positions)
{
    std::string joint_positions_str = "[";
    for (size_t i = 0; i < joint_positions.size(); ++i)
    {
        joint_positions_str += std::to_string(joint_positions[i]) + (i < joint_positions.size() - 1 ? ", " : "");
    }
    joint_positions_str += "]";



    dismissSetPositionDialog();
    set_position_dialog_ = addWidget(std::make_unique<helper::ReusableDialog>(
        "Set Position",
        "Update the default position of the robot to joint positions: " + joint_positions_str + ". This position will be used as the default position for the robot on \"Move To Default Position\" button press.",
        std::vector<helper::ButtonDescription> {
            helper::ButtonDescription("Cancel", helper::ButtonStyle::Secondary, true),
            helper::ButtonDescription("Set", helper::ButtonStyle::Danger, true)
        }
    ));
    set_position_dialog_->onBackgroundClicked().connect([this]() {
        dismissSetPositionDialog();
    });
    set_position_dialog_->onButtonClicked().connect([this, joint_positions](size_t button_index) {
        if (button_index == 0) // Cancel
        {
            dismissSetPositionDialog();
        }
        else if (button_index == 1) // Set
        {
            setPositionRequested(joint_positions);
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


void MainVisualizationUi::moveToPositionRequested(const std::vector<double>& joint_positions)
{
    if (allocator_ == nullptr)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Move to position requested failed: Allocator is not initialized.");
        showInfoDialog("Move To Position Failed", "Allocator is not initialized. Please try again.");
        return;
    }

    uint32_t robot_interface_request_channel_id;
    if (!base_module_->getRequestChannelByName(ri::robot_interface_request_consumer.channel_type_identifier_, robot_interface_request_channel_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Move to position requested failed: Frontend module does not have robot interface request channel.");
        showInfoDialog("Move To Position Failed", "Frontend module does not have robot interface request channel. Please add a robot control module to the program.");
        return;
    }
    
    ICoreControl* core_control = base_module_->getCoreControl();
    if (core_control == nullptr)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Move to position requested failed: Frontend module does not have core control.");
        showInfoDialog("Move To Position Failed", "Frontend module does not have core control. Please add a core control module to the program.");
        return;
    }

    message::SharedDataBlob existing_robot_interface_modules = core_control->getExistingResponseChannelsByName(ri::robot_interface_request_consumer.channel_type_identifier_);
    if (!existing_robot_interface_modules.valid() || existing_robot_interface_modules.data() == nullptr)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Move to position requested failed: Response from core control is invalid.");
        showInfoDialog("Move To Position Failed", "Response from core control is invalid.");
        return;
    }
    
    deserialize::des::BufferReader reader(existing_robot_interface_modules.data(), existing_robot_interface_modules.size());
    std::vector<aergo::module::ChannelIdentifier> existing_robot_interface_modules_list;
    if (!deserialize::readExistingChannels(reader, existing_robot_interface_modules_list))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Move to position requested failed: Failed to deserialize existing robot interface modules list.");
        showInfoDialog("Move To Position Failed", "Failed to deserialize existing robot interface modules list.");
        return;
    }

    if (existing_robot_interface_modules_list.size() != 1)
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "Move to position requested failed: Expected exactly one robot control module, found " + std::to_string(existing_robot_interface_modules_list.size()));
        showInfoDialog("Move To Position Failed", "Expected exactly one robot control module, found " + std::to_string(existing_robot_interface_modules_list.size()));
        return;
    }
    
    aergo::module::ChannelIdentifier target_channel = existing_robot_interface_modules_list[0];

    ri::Request request {
        .req_type = ri::ReqType::START_ACTION,
        .feature = ri::RobotFeature::ROBOT_CONTROL,
        .action_id = 0
    };

    std::vector<std::byte> buffer;
    rc::start::requests::serialization::moveJoint(buffer, joint_positions, 1.0, 1.0);

    message::SharedDataBlob blob = allocator_->allocateFromData(std::span<std::byte>(buffer));

    if (!blob.valid())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Move to position requested failed: Failed to allocate blob.");
        showInfoDialog("Move To Position Failed", "Failed to allocate blob. Please try again.");
        return;
    }

    message::MessageHeader message = message::MessageHeader::Message(&request, &blob);
    uint64_t request_id = base_module_->sendRequest(robot_interface_request_channel_id, target_channel, message);
}


void MainVisualizationUi::setPositionRequested(const std::vector<double>& joint_positions)
{
    saveDefaultJointPositions(joint_positions);
    base_module_->log(aergo::module::logging::LogType::INFO, "Frontend module: Default joint positions saved.");
}


void MainVisualizationUi::setPositionPressed()
{
    std::lock_guard<std::mutex> lock(main_visualization_state_unsafe_.main_visualization_state_mutex_);
    if (!main_visualization_state_unsafe_.robot_status_message_valid_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Frontend module: set position requested: robot status message is not valid");
        showInfoDialog("Set Position Failed", "Please add a robot control module to the program.");
        return;
    }

    showSetPositionDialog(main_visualization_state_unsafe_.robot_status_message_.joint_positions);
}


bool MainVisualizationUi::loadDefaultJointPositions(std::vector<double>& out_joint_positions, std::string& out_err_msg)
{
    out_joint_positions.clear();
    out_err_msg.clear();

    std::filesystem::path json_path(base_module_->getDataPath());
    json_path /= MAIN_VISUALIZATION_DATA_JSON;

    if (!std::filesystem::exists(json_path))
    {
        out_err_msg = std::string(MAIN_VISUALIZATION_DATA_JSON) + " does not exist. Please use 'Set Position' to set a default position first.";
        base_module_->log(aergo::module::logging::LogType::INFO, "Frontend module: " + out_err_msg);
        return false;
    }

    std::ifstream file(json_path);
    if (!file.is_open())
    {
        out_err_msg = "Failed to open " + std::string(MAIN_VISUALIZATION_DATA_JSON) + " at: " + json_path.string();
        base_module_->log(aergo::module::logging::LogType::ERROR, "Frontend module: " + out_err_msg);
        return false;
    }

    nlohmann::json json_data;
    try
    {
        file >> json_data;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        out_err_msg = "Failed to parse " + std::string(MAIN_VISUALIZATION_DATA_JSON) + ": " + std::string(e.what());
        base_module_->log(aergo::module::logging::LogType::ERROR, "Frontend module: " + out_err_msg);
        return false;
    }

    if (!json_data.contains("default_joint_positions"))
    {
        out_err_msg = std::string(MAIN_VISUALIZATION_DATA_JSON) + " does not contain 'default_joint_positions' key.";
        base_module_->log(aergo::module::logging::LogType::WARNING, "Frontend module: " + out_err_msg);
        return false;
    }

    if (!json_data["default_joint_positions"].is_array())
    {
        out_err_msg = "'default_joint_positions' in " + std::string(MAIN_VISUALIZATION_DATA_JSON) + " must be an array.";
        base_module_->log(aergo::module::logging::LogType::ERROR, "Frontend module: " + out_err_msg);
        return false;
    }

    try
    {
        out_joint_positions = json_data["default_joint_positions"].get<std::vector<double>>();
    }
    catch (const nlohmann::json::type_error& e)
    {
        out_err_msg = "Failed to convert 'default_joint_positions' to vector<double>: " + std::string(e.what());
        base_module_->log(aergo::module::logging::LogType::ERROR, "Frontend module: " + out_err_msg);
        return false;
    }

    return true;
}


void MainVisualizationUi::saveDefaultJointPositions(const std::vector<double>& joint_positions)
{
    std::filesystem::path json_path(base_module_->getDataPath());
    json_path /= MAIN_VISUALIZATION_DATA_JSON;

    nlohmann::json json_data;
    
    // Try to load existing data if file exists
    if (std::filesystem::exists(json_path))
    {
        std::ifstream file(json_path);
        if (file.is_open())
        {
            try
            {
                file >> json_data;
            }
            catch (const nlohmann::json::parse_error& e)
            {
                base_module_->log(aergo::module::logging::LogType::WARNING, "Frontend module: failed to parse existing " + std::string(MAIN_VISUALIZATION_DATA_JSON) + ", overwriting: " + std::string(e.what()));
                json_data = nlohmann::json::object();
            }
        }
    }

    // Update or set the default_joint_positions
    json_data["default_joint_positions"] = joint_positions;

    // Write to file
    std::ofstream file(json_path);
    if (!file.is_open())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Frontend module: failed to open " + std::string(MAIN_VISUALIZATION_DATA_JSON) + " for writing at: " + json_path.string());
        return;
    }

    file << json_data.dump(4); // Pretty print with 4-space indentation
}