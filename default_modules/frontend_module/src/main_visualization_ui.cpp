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

    auto register_id = scene_container->createObjectDescription(
        helper::ComplexShape {
            .parts = {
                helper::PrimitiveShape {
                    .type = helper::PrimitiveShapeType::BOX,
                    .desc = helper::BoxDesc{1.0f, 3.0f, 2.0f},
                    .origin = helper::Pose {
                        .t = helper::Vec3{1.0f, 0.0f, 0.0f},
                        .q = helper::Quat::Identity()
                    },
                    .color = { .rgba = 0xFF0000FF } // red
                },
                helper::PrimitiveShape {
                    .type = helper::PrimitiveShapeType::SPHERE,
                    .desc = helper::SphereDesc{2.0f},
                    .origin = helper::Pose {
                        .t = helper::Vec3{-1.0f, 0.0f, 0.0f},
                        .q = helper::Quat::Identity()
                    },
                    .color = { .rgba = 0x00FF00FF } // green
                },
            }
        }
    );

    scene_container->enableGrid(true);

    helper::ObjectId obj_id;
    if (!scene_container->addObject(register_id, helper::Pose{
        .t = helper::Vec3{0.0f, 0.0f, 0.0f},
        .q = helper::Quat::Identity()
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 0 to scene");
    }
    if (!scene_container->addObject(register_id, helper::Pose{
        .t = helper::Vec3{1.0f, 0.0f, 0.0f},
        .q = helper::Quat::Identity()
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 1 to scene");
    }
    if (!scene_container->addObject(register_id, helper::Pose{
        .t = helper::Vec3{0.0f, 1.0f, 0.0f},
        .q = helper::Quat::Identity()
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 2 to scene");
    }
    if (!scene_container->addObject(register_id, helper::Pose{
        .t = helper::Vec3{-1.0f, 0.0f, 0.0f},
        .q = helper::Quat::Identity()
    }, obj_id))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to add object 3 to scene");
    }

    scene_container->enableGrid(false);
}