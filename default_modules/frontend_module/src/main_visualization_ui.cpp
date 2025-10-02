#include "webapp/ui/main_visualization_ui.h"

#include "webapp/ui/helper/topbar.h"
#include "webapp/ui/helper/scene_container.h"

#undef ERROR // Gotta love Windows.h

using namespace aergo::default_modules::frontend_module::webapp::ui;

MainVisualizationUi::MainVisualizationUi(aergo::module::BaseModule* base_module)
: base_module_(base_module)
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

    auto content_container = addWidget(std::make_unique<Wt::WContainerWidget>());
    content_container->setStyleClass("main-content-container");

    auto scene_container = content_container->addWidget(std::make_unique<helper::SceneContainer>(base_module, 16 /* ~60fps */));
    camera_container_ = content_container->addWidget(std::make_unique<helper::CameraContainer>());

    auto register_id_complex = scene_container->createObjectDescription(
        helper::ComplexShape {
            .parts = {
                helper::PrimitiveShape {
                    .type = helper::PrimitiveShapeType::CYLINDER,
                    .desc = helper::CylinderDesc{0.1f, 0.1f, 1.6f},
                    .origin = helper::Pose {
                        .t = helper::Vec3::Zero(),
                        .q = helper::Quat::Identity()
                    },
                    .color = { 255, 0, 0 } // red
                },
                helper::PrimitiveShape {
                    .type = helper::PrimitiveShapeType::SPHERE,
                    .desc = helper::SphereDesc{0.15f},
                    .origin = helper::Pose {
                        .t = helper::Vec3{0.0f, 0.0f, -0.8f},
                        .q = helper::Quat::Identity()
                    },
                    .color = { 0, 255, 0 } // green
                },
                helper::PrimitiveShape {
                    .type = helper::PrimitiveShapeType::CYLINDER,
                    .desc = helper::CylinderDesc{0.1f, 0, 0.3f},
                    .origin = helper::Pose {
                        .t = {0, 0, 0.8f + 0.15f},
                        // rotate 30deg around y
                        .q = {0, 0.2588f, 0, 0.9659f} // sin(15deg), 0, 0, cos(15deg)
                    },
                    .color = { 0, 0, 255 } // blue
                },
            }
        }
    );


    helper::ObjectId obj_id;
    if (!scene_container->addObject(register_id_complex, helper::Pose{
        .t = helper::Vec3{0.0f, 0.0f, 0.0f},
        .q = helper::Quat::Identity()
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 0 to scene");
    }
    if (!scene_container->addObject(register_id_complex, helper::Pose{
        .t = helper::Vec3{0.5f, 0.0f, 0.0f},
        // rotate -30 deg around X
        .q = { -0.2588f, 0, 0, 0.9659f } // sin(-15deg), 0, 0, cos(-15deg)
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 1 to scene");
    }
    if (!scene_container->addObject(register_id_complex, helper::Pose{
        .t = helper::Vec3{0.0f, 1.0f, 0.0f},
        // rotate 30 deg around Z
        .q = {0, 0, 0.2588f, 0.9659f} // sin(15deg), 0, cos(15deg)
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 2 to scene");
    }
    if (!scene_container->addObject(register_id_complex, helper::Pose{
        .t = helper::Vec3{-2.f, 0.0f, 0.0f},
        // rotate -90deg around Y
        .q = {0, -0.7071f, 0, 0.7071f} // sin(-45deg), 0, 0, cos(-45deg)
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 3 to scene");
    }

    scene_container->enableGrid(true);
}