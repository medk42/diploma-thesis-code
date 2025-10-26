#include "webapp/ui/main_visualization_ui.h"

#include "webapp/ui/helper/topbar.h"

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

    scene_container_ = content_container->addWidget(std::make_unique<helper::SceneContainer>(base_module, 16 /* ~60fps */));
    program_tree_ = content_container->addWidget(std::make_unique<helper::ProgramTree>());
    camera_container_ = content_container->addWidget(std::make_unique<helper::CameraContainer>());

    auto register_id_complex = scene_container_->createObjectDescription(
        helper::vis3d::ComplexShape {
            .parts = {
                helper::vis3d::PrimitiveShape {
                    .type = helper::vis3d::PrimitiveShapeType::CYLINDER,
                    .desc = helper::vis3d::CylinderDesc{0.01f, 0.01f, 0.16f},
                    .origin = helper::vis3d::Pose {
                        .t = helper::vis3d::Vec3::Zero(),
                        .q = helper::vis3d::Quat::Identity()
                    },
                    .color = { 255, 0, 0 } // red
                },
                helper::vis3d::PrimitiveShape {
                    .type = helper::vis3d::PrimitiveShapeType::SPHERE,
                    .desc = helper::vis3d::SphereDesc{0.015f},
                    .origin = helper::vis3d::Pose {
                        .t = helper::vis3d::Vec3{0.0f, 0.0f, -0.08f},
                        .q = helper::vis3d::Quat::Identity()
                    },
                    .color = { 0, 255, 0 } // green
                },
                helper::vis3d::PrimitiveShape {
                    .type = helper::vis3d::PrimitiveShapeType::CYLINDER,
                    .desc = helper::vis3d::CylinderDesc{0.01f, 0, 0.03f},
                    .origin = helper::vis3d::Pose {
                        .t = {0, 0, 0.08f + 0.015f},
                        .q = helper::vis3d::Quat::Identity().RotateDegY(30.f)
                    },
                    .color = { 0, 0, 255 } // blue
                },
            }
        }
    );


    helper::vis3d::ObjectId obj_id;
    if (!scene_container_->addObject(register_id_complex, helper::vis3d::Pose{
        .t = helper::vis3d::Vec3{0.0f, 0.0f, 0.0f},
        .q = helper::vis3d::Quat::Identity()
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 0 to scene");
    }
    if (!scene_container_->addObject(register_id_complex, helper::vis3d::Pose{
        .t = helper::vis3d::Vec3{0.05f, 0.0f, 0.0f},
        .q = helper::vis3d::Quat::Identity().RotateDegX(-30.f)
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 1 to scene");
    }
    if (!scene_container_->addObject(register_id_complex, helper::vis3d::Pose{
        .t = helper::vis3d::Vec3{0.0f, 0.1f, 0.0f},
        .q = helper::vis3d::Quat::Identity().RotateDegZ(30.f)
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 2 to scene");
    }
    if (!scene_container_->addObject(register_id_complex, helper::vis3d::Pose{
        .t = helper::vis3d::Vec3{-0.2f, 0.0f, 0.0f},
        .q = helper::vis3d::Quat::Identity().RotateDegY(-90.f)
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 3 to scene");
    }

    scene_container_->enableGrid(true);

    std::vector<helper::vis3d::Vec3> pts;
    for (float t = 0; t < 2*3.141592; t += 0.01)
    {
        pts.push_back(helper::vis3d::Vec3{ 0.1f * std::cos(t), 0.1f * std::sin(t), 0.1f * std::sin(5*t) * 0.2f } );
    }
    
    scene_container_->addTrajectory(pts, {0, 0, 0}, true, obj_id);
}