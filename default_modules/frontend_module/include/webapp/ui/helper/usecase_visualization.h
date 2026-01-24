#pragma once

#include "scene_container.h"
#include "module_helpers/usecase_wrapper/usecase_module_interface.h"

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    namespace uw = aergo::module::helpers::usecase_wrapper;

    class UsecaseVisualization
    {   
    public:
        UsecaseVisualization(
            SceneContainer* scene_container,
            const vis3d::Color& point_color = vis3d::Color{0x80, 0x80, 0x80, 0xFF},
            const vis3d::Color& trajectory_color = vis3d::Color{0x80, 0x80, 0x80, 0xFF}
        );

        /// @brief Clear existing visualization from the scene.
        void clearVisualization();

        /// @brief Clear and display new visualization info.
        /// @param visualization_info Visualization data to display. If supports_visualization is false, 
        /// scene is still cleared and nothing is displayed.
        void displayVisualization(const uw::IUsecaseModule::UsecaseVisualization& visualization_info);
        
    private:
        void registerResources();

        SceneContainer* scene_container_{nullptr};

        vis3d::ResourceId pose_resource_id_{0};
        vis3d::ResourceId point_resource_id_{0};

        vis3d::Color point_color_;
        vis3d::Color trajectory_color_;

        std::vector<vis3d::ObjectId> active_object_ids_;
        std::vector<vis3d::ObjectId> active_trajectory_ids_;
    };
}