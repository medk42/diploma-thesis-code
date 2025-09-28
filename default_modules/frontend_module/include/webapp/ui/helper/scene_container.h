#pragma once

#include "module_common/base_module.h"

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
    // External Scene API
    struct Vec3 
    { 
        static Vec3 Zero() { return {0,0,0}; }
        static Vec3 One()  { return {1,1,1}; }

        float x,y,z; 
    };

    struct Quat
    { 
        static Quat Identity() { return {0,0,0,1}; }

        float x,y,z,w; 
    };

    struct Pose
    {
        Vec3 t = Vec3::Zero();
        Quat q = Quat::Identity();
    };

    union Color
    {
        struct { uint8_t r,g,b,a; };
        uint32_t rgba = 0x6699FFff; // 0xRRGGBBAA (A default 0xFF)
    };

    enum class PrimitiveShapeType : uint8_t { BOX=0, SPHERE=1, CYLINDER=2 };

    struct BoxDesc { float sx, sy, sz; };
    struct SphereDesc { float r; };
    struct CylinderDesc { float rTop, rBot, h; };

    struct PrimitiveShape
    {
        PrimitiveShapeType type;
        std::variant<BoxDesc, SphereDesc, CylinderDesc> desc;
        Pose     origin;          // local pose
        Color    color;
    };

    struct ComplexShape
    {
        std::vector<PrimitiveShape> parts;
    };

    enum class ObjectType : uint8_t { Complex=0 };

    struct ResourceId
    {
        uint32_t id; 
        auto operator<=>(const ResourceId&) const = default;
    };
    struct ObjectId
    { 
        uint32_t id; 
        auto operator<=>(const ObjectId&) const = default;
    };
    
    

    /// Internal Scene API

    struct CommandBuffer
    {
        enum class Action : uint8_t { ADD=0, UPDATE=1, REMOVE=2 };
        struct ObjectParameters { Action action; ResourceId resource_id; Pose pose; };
        struct TrajectoryParameters { Action action; bool dashed; std::vector<Vec3> points; uint32_t remove_from_head; };

        void clear()
        {
            pending_registrations_.clear();
            objects_.clear();
            trajectories_.clear();
            grid_commanded_ = false;
            grid_enabled_ = false;
        }

        bool isEmpty() const
        {
            return pending_registrations_.empty() && objects_.empty() && trajectories_.empty() && !grid_commanded_;
        }

        bool grid_commanded_{false};
        bool grid_enabled_{false};

        std::vector<std::tuple<ResourceId, ComplexShape>> pending_registrations_;

        std::map<ObjectId, ObjectParameters> objects_;
        std::map<ObjectId, TrajectoryParameters> trajectories_;
    };



    class SceneSocket : public Wt::WWebSocketResource
    {
    public:
        SceneSocket();
        ~SceneSocket() override;

        /// @brief Thread-safe, non-blocking. Returns number of queued messages (if >1, messages are not being sent fast enough).
        /// Returns 0 on failure (commands invalid).
        size_t sendCommandBuffer(const CommandBuffer& cmd_buf);

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
        ResourceId createObjectDescription(const ComplexShape& s); // returns resource_id
        // (reserve for future) uint32_t createMeshDescription(const MeshMeshDesc& m);

        // Instances (object_id)
        bool addObject(ResourceId resource_id, const Pose& pose, ObjectId& out_id);
        bool updateObject(ObjectId object_id, const Pose& pose);
        bool removeObject(ObjectId object_id);

        // Trajectories
        bool addTrajectory(const std::vector<Vec3>& pts, bool dashed, ObjectId& out_id);
        bool updateTrajectory(ObjectId trajectory_id, const std::vector<Vec3>& add_pts, uint32_t remove_from_head);
        bool removeTrajectory(ObjectId trajectory_id);

    private:
        void updateWorker();

        std::unique_ptr<SceneSocket> socket_;

        uint32_t next_resource_id_{0};
        uint32_t next_object_id_{0};

        CommandBuffer cmd_buf_;
        std::mutex cmd_buf_mtx_;

        std::map<ResourceId, ComplexShape> registered_resources_;
        std::map<ObjectId, std::tuple<ResourceId, Pose>> existing_objects_;
        std::map<ObjectId, std::tuple<bool, std::vector<Vec3>>> existing_trajectories_;

        std::thread update_thread_;
        std::atomic<bool> running_{true};

        aergo::module::BaseModule* base_module_{nullptr};
        uint8_t frame_sleep_millis_{16};
    };
}