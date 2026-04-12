#pragma once

#include "module_common/base_module.h"
#include "module_helpers/robot_interface/message_types.h"
#include "module_helpers/robot_interface/features/robot_control/messages.h"
#include "module_helpers/visualization_3d_interface/visualization_helper.h"

#include <Wt/WServer.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace aergo::default_modules::robot_module_dummy
{
    namespace ri = aergo::module::helpers::robot_interface;
    namespace rc = aergo::module::helpers::robot_interface::robot_control;
    namespace vis3d = aergo::module::helpers::visualization_3d_interface;

    class RobotModuleDummy : public aergo::module::BaseModule
    {
    public:
        struct UiSnapshot
        {
            bool valid{false};
            bool moving{false};
            bool has_error{false};
            uint64_t active_action_id{0};
            std::string error_text;
            std::array<double, 6> joints_rad{};
            std::array<double, 6> tfc_xyzrpy{};
            std::array<double, 6> tcp_xyzrpy{};
        };

        struct UiCommandResult
        {
            bool success{false};
            uint64_t action_id{0};
            std::string message;
        };

        RobotModuleDummy(const char* data_path,
                         aergo::module::ICore* core,
                         aergo::module::InputChannelMapInfo channel_map_info,
                         const aergo::module::logging::ILogger* logger,
                         uint64_t module_id,
                         const aergo::module::ModuleInfo* module_info);
        ~RobotModuleDummy() noexcept override;

        bool valid() noexcept override { return valid_; }
        void* query_capability(const std::type_info& id) noexcept override;

        IngressDecision onIngress(ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src,
                                  const aergo::module::message::MessageHeader& msg, QueueStatus queue_status) noexcept override;

        void processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;
        aergo::module::ResponseData processRequest(uint32_t response_producer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;
        void processResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;

        bool threadStart(uint32_t timeout_ms) noexcept override;
        bool threadStop(uint32_t timeout_ms) noexcept override;

        ISerializableModule::SaveData save() noexcept override
        {
            return ISerializableModule::SaveData{ .supports_saving_ = false };
        }

        bool load(ISerializableModule::SaveData) noexcept override
        {
            return true;
        }

        UiSnapshot getUiSnapshot();
        UiCommandResult startUiMoveJoint(const std::array<double, 6>& joint_targets_rad, double speed_rad_s);
        UiCommandResult startUiMoveLinear(const std::array<double, 6>& tcp_xyzrpy_rad, double speed_m_s);
        UiCommandResult cancelUiMove();

    private:
        struct SimState
        {
            // x,y,z + roll,pitch,yaw represent the flange/head pose in world coordinates.
            double xyzrpy[6]{0, 0, 0, 0, 0, 0};
            rc::Pose tfc_pose{}; // flange pose (TFC) in world
            rc::Pose tcp_pose{}; // end effector pose (TCP) in world
        };

        enum class MoveSpace : uint8_t
        {
            TFC,
            TCP
        };

        struct ActiveMove
        {
            uint64_t action_id{0};
            bool active{false};
            bool cancel_requested{false};
            MoveSpace move_space{MoveSpace::TFC};

            double start_pos[3]{};
            double target_pos[3]{};
            rc::Quaternion start_quat{0.0, 0.0, 0.0, 1.0};
            rc::Quaternion target_quat{0.0, 0.0, 0.0, 1.0};
            double duration_s{0.0};
            double t_s{0.0};
        };

        void simLoop();

        // request handlers
        aergo::module::ResponseData handleRobotInterfaceRequest(const ri::Request& req, std::span<const std::byte> blob);
        aergo::module::ResponseData handleStartRobotControl(std::span<const std::byte> blob);
        aergo::module::ResponseData handleUpdateRobotControl(uint64_t action_id, std::span<const std::byte> blob);

        // publishing helpers
        void publishStatusLocked(uint64_t timestamp_us);
        void publishFinished(uint64_t action_id, bool success, const char* error_msg);

        // pose helpers (Kassow convention: roll about world X, pitch about world Y (after roll), yaw about world Z (after pitch))
        static rc::Quaternion quatFromWorldRpy(double roll, double pitch, double yaw);
        static void rpyFromQuatWorld(const rc::Quaternion& q_in, double& out_roll, double& out_pitch, double& out_yaw);
        static rc::Pose makePoseFromXyzRpy(const double xyzrpy[6]);
        static rc::Pose makePoseFromPosQuat(const double pos[3], const rc::Quaternion& quat);
        static rc::Pose tcpFromTfc(const rc::Pose& tfc_pose); // TCP = 10 cm in +Z of TFC
        static rc::Pose tfcFromTcp(const rc::Pose& tcp_pose); // TFC = TCP shifted by -10 cm in tool +Z

        static rc::Quaternion quatNormalize(const rc::Quaternion& q);
        static double quatDot(const rc::Quaternion& a, const rc::Quaternion& b);
        static rc::Quaternion quatNegated(const rc::Quaternion& q);
        static rc::Quaternion quatSlerpShortest(rc::Quaternion a, rc::Quaternion b, double t);

        // visualization
        bool initVisResources();
        bool ensureVisObjectsCreatedLocked();
        void updateVisLocked();
        bool updateTrajectoryLocked(const rc::Vector3& point, uint16_t history_length = 1000);
        void resetTrajectoryLocked();
        UiCommandResult tryStartMoveLocked(const ActiveMove& move_template);
        static std::string trim(const std::string& s);
        bool parseConfigFile();
        std::vector<std::string> makeServerArgs() const;

        bool valid_{false};

        uint32_t response_channel_id_{0};
        uint32_t status_publish_id_{0};
        uint32_t finished_publish_id_{0};

        struct ServerParameters
        {
            std::string docroot;
            std::string wt_config_path;
            uint16_t port_http{0};
        };

        ServerParameters server_parameters_{};
        std::unique_ptr<Wt::WServer> w_server_;
        std::string last_error_message_;

        aergo::module::BaseModule::AllocatorPtr allocator_;

        std::unique_ptr<vis3d::VisualizationHelper> visualization_helper_;
        std::mutex vis_mutex_;
        bool visualization_announced_{false};
        vis3d::ResourceId axes_resource_{0};
        vis3d::ResourceId head_resource_{0};
        vis3d::ObjectId world_axes_obj_{0};
        vis3d::ObjectId tfc_axes_obj_{0};
        vis3d::ObjectId tcp_axes_obj_{0};
        vis3d::ObjectId head_obj_{0};
        vis3d::ObjectId trajectory_obj_{0};
        bool vis_objects_created_{false};
        uint16_t trajectory_length_{0};
        rc::Vector3 last_trajectory_point_{};
        vis3d::Color trajectory_color_{ 0xff, 0x6b, 0xf0, 0xFF };

        std::atomic<bool> stop_{false};
        std::thread sim_thread_;
        std::mutex sim_mutex_;
        SimState sim_{};
        ActiveMove move_{};
        uint64_t next_action_id_{1};
    };
}

