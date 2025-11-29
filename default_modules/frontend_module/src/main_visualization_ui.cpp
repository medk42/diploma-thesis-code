#include "webapp/ui/main_visualization_ui.h"

#include "webapp/ui/helper/topbar.h"

#undef ERROR // Gotta love Windows.h

using namespace aergo::default_modules::frontend_module::webapp::ui;

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

    scene_container_ = content_container->addWidget(std::make_unique<helper::SceneContainer>(base_module, 16 /* ~60fps */));
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