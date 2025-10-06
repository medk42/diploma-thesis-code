#pragma once

#include "module_common/base_module.h"

#include "module_helpers/visualization_3d_interface/scene_desc_api.h"
#include "module_helpers/visualization_3d_interface/command_buffer.h"
#include "module_helpers/visualization_3d_interface/command_coalescer.h"

#include <Wt/WContainerWidget.h>
#include <Wt/WFileResource.h>
#include <Wt/WLink.h>
#include <Wt/WApplication.h>
#include <Wt/WWebSocketResource.h>
#include <Wt/WWebSocketConnection.h>
#include <Wt/WServer.h>

#include <deque>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    namespace vis3d = aergo::module::helpers::visualization_3d_interface;

    class SceneSocket : public Wt::WWebSocketResource
    {
    public:
        SceneSocket();
        ~SceneSocket() override;

        /// @brief Thread-safe, non-blocking. Returns number of queued messages (if >1, messages are not being sent fast enough).
        /// Returns 0 on failure (commands invalid).
        size_t sendCommandBuffer(const vis3d::CommandBuffer& cmd_buf);

    protected:
        std::unique_ptr<Wt::WWebSocketConnection> handleConnect(const Wt::Http::Request &req) override;

    private:
        void startWorkers();

        std::mutex m_;
        std::condition_variable cv_;
        Wt::WWebSocketConnection* conn_{nullptr};
        
        bool sending_{false};
        std::deque<std::vector<char>> q_;
        uint64_t seq_{0};

        std::atomic<bool> running_{false};
        std::thread send_worker_;
    };



    class SceneContainer : public Wt::WContainerWidget
    {
    public:
        SceneContainer(aergo::module::BaseModule* base_module, uint8_t frame_sleep_millis);
        ~SceneContainer() override;

         // --------- External API (what other modules call) ----------
        void enableGrid(bool on);

        // Static resource registry
        vis3d::ResourceId createObjectDescription(const vis3d::ComplexShape& s); // returns resource_id
        // (reserve for future) uint32_t createMeshDescription(const MeshMeshDesc& m);

        // Instances (object_id)
        bool addObject(vis3d::ResourceId resource_id, const vis3d::Pose& pose, vis3d::ObjectId& out_id);
        bool updateObject(vis3d::ObjectId object_id, const vis3d::Pose& pose);
        bool removeObject(vis3d::ObjectId object_id);

        // Trajectories
        bool addTrajectory(const std::vector<vis3d::Vec3>& pts, vis3d::Color color, bool dashed, vis3d::ObjectId& out_id);
        bool updateTrajectory(vis3d::ObjectId trajectory_id, const std::vector<vis3d::Vec3>& add_pts, uint32_t remove_from_head);
        bool removeTrajectory(vis3d::ObjectId trajectory_id);

    private:
        void updateWorker();

        std::unique_ptr<SceneSocket> socket_;

        uint32_t next_resource_id_{0};
        uint32_t next_object_id_{0};

        std::mutex cmd_coalescer_mtx_;
        vis3d::CommandCoalescer cmd_coalescer_;

        std::map<vis3d::ResourceId, vis3d::ComplexShape> registered_resources_;
        std::map<vis3d::ObjectId, std::tuple<vis3d::ResourceId, vis3d::Pose>> existing_objects_;
        std::map<vis3d::ObjectId, std::tuple<bool, std::vector<vis3d::Vec3>>> existing_trajectories_;

        std::thread update_thread_;
        std::atomic<bool> running_{true};

        aergo::module::BaseModule* base_module_{nullptr};
        uint8_t frame_sleep_millis_{16};
    };
}