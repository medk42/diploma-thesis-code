#include "robot_module_dummy/robot_module_dummy.h"

#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/serialization_helper/serialization_helper.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <span>
#include <thread>

using namespace aergo::default_modules::robot_module_dummy;
using namespace aergo::module;

namespace ser = aergo::module::helpers::serialization_helper;

static uint64_t nowMicros()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

RobotModuleDummy::RobotModuleDummy(const char* data_path,
                                   ICore* core,
                                   InputChannelMapInfo channel_map_info,
                                   const logging::ILogger* logger,
                                   uint64_t module_id,
                                   const ModuleInfo* module_info)
    : BaseModule(data_path, core, channel_map_info, logger, module_id, module_info),
      allocator_(createDynamicAllocator())
{
    if (!getResponseChannelByName(ri::robot_interface_response_producer.channel_type_identifier_, response_channel_id_))
    {
        log(logging::LogType::ERROR, "Failed to locate robot interface response channel");
        return;
    }
    if (!getPublishChannelByName(ri::robot_interface_status_producer.channel_type_identifier_, status_publish_id_))
    {
        log(logging::LogType::ERROR, "Failed to locate robot interface status publish channel");
        return;
    }
    if (!getPublishChannelByName(ri::robot_interface_finished_producer.channel_type_identifier_, finished_publish_id_))
    {
        log(logging::LogType::ERROR, "Failed to locate robot interface finished publish channel");
        return;
    }
    if (!allocator_)
    {
        log(logging::LogType::ERROR, "Failed to initialize allocator for Dummy robot module");
        return;
    }

    visualization_helper_ = std::make_unique<vis3d::VisualizationHelper>(this);
    if (!visualization_helper_->valid())
    {
        log(logging::LogType::ERROR, "Failed to initialize 3D visualization helper");
        return;
    }
    if (!initVisResources())
    {
        log(logging::LogType::ERROR, "Failed to register dummy visualization resources");
        return;
    }

    // Initialize poses
    sim_.tfc_pose = makePoseFromXyzRpy(sim_.xyzrpy);
    sim_.tcp_pose = tcpFromTfc(sim_.tfc_pose);

    valid_ = true;
}

RobotModuleDummy::~RobotModuleDummy() noexcept
{
    stop_.store(true, std::memory_order_release);
    if (sim_thread_.joinable())
    {
        sim_thread_.join();
    }
}

void* RobotModuleDummy::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    return nullptr;
}

IModule::IngressDecision RobotModuleDummy::onIngress(ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier identifier,
                                                     const message::MessageHeader&, QueueStatus queue_status) noexcept
{
    if (kind == ProcessingType::REQUEST)
    {
        if (local_channel_id == response_channel_id_)
        {
            if (queue_status != QueueStatus::NORMAL)
            {
                log(logging::LogType::WARNING, "Dummy robot module dropping request due to request queue full: " +
                    std::to_string(identifier.module_id_) + "/" + std::to_string(identifier.local_channel_id_));
                return IngressDecision::DROP;
            }
            return IngressDecision::ACCEPT;
        }
        if (local_channel_id == visualization_helper_->getResponseProducerChannel())
        {
            return IngressDecision::ACCEPT;
        }
    }
    return IngressDecision::DROP;
}

void RobotModuleDummy::processMessage(uint32_t, ChannelIdentifier, message::MessageHeader) noexcept
{
    log(logging::LogType::WARNING, "Dummy robot module received unexpected subscribed message, dropping");
}

ResponseData RobotModuleDummy::processRequest(uint32_t response_producer_id, ChannelIdentifier, message::MessageHeader message) noexcept
{
    if (response_producer_id == visualization_helper_->getResponseProducerChannel())
    {
        std::lock_guard<std::mutex> lock(vis_mutex_);
        return visualization_helper_->processVisualizationRequest(message);
    }

    if (response_producer_id != response_channel_id_)
    {
        log(logging::LogType::WARNING, "Request received on unknown response channel, dropping");
        return { .success_ = false };
    }

    if (message.data_ == nullptr || message.data_len_ != sizeof(ri::Request))
    {
        log(logging::LogType::WARNING, "Robot request has invalid header size");
        return { .success_ = false };
    }

    const auto* request = reinterpret_cast<const ri::Request*>(message.data_);
    std::span<const std::byte> request_blob;
    if (message.blob_count_ > 0 && message.blobs_ && message.blobs_[0].valid())
    {
        request_blob = std::span<const std::byte>(reinterpret_cast<const std::byte*>(message.blobs_[0].data()),
                                                  static_cast<size_t>(message.blobs_[0].size()));
    }

    return handleRobotInterfaceRequest(*request, request_blob);
}

void RobotModuleDummy::processResponse(uint32_t, ChannelIdentifier, message::MessageHeader) noexcept
{
    log(logging::LogType::WARNING, "Dummy robot module does not expect responses");
}

bool RobotModuleDummy::threadStart(uint32_t) noexcept
{
    if (!valid_) return false;

    stop_.store(false, std::memory_order_release);
    sim_thread_ = std::thread(&RobotModuleDummy::simLoop, this);
    return true;
}

bool RobotModuleDummy::threadStop(uint32_t) noexcept
{
    stop_.store(true, std::memory_order_release);
    if (sim_thread_.joinable())
    {
        sim_thread_.join();
    }

    std::lock_guard<std::mutex> lock(vis_mutex_);
    if (vis_objects_created_)
    {
        visualization_helper_->removeObject(world_axes_obj_);
        visualization_helper_->removeObject(tfc_axes_obj_);
        visualization_helper_->removeObject(tcp_axes_obj_);
        visualization_helper_->removeObject(head_obj_);
        visualization_helper_->sendUpdate();
        vis_objects_created_ = false;
    }
    return true;
}

ResponseData RobotModuleDummy::handleRobotInterfaceRequest(const ri::Request& req, std::span<const std::byte> blob)
{
    if (req.feature != ri::RobotFeature::ROBOT_CONTROL)
    {
        ri::Response resp{ .resp_type = ri::RespType::FEATURE_NOT_SUPPORTED, .action_id = req.action_id };
        ResponseData out;
        out.success_ = true;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&resp);
        out.data_.assign(p, p + sizeof(resp));
        return out;
    }

    if (req.req_type == ri::ReqType::START_ACTION)
    {
        return handleStartRobotControl(blob);
    }
    if (req.req_type == ri::ReqType::UPDATE_ACTION)
    {
        return handleUpdateRobotControl(req.action_id, blob);
    }

    return { .success_ = false };
}

static ResponseData buildRobotInterfaceResponse(aergo::module::BaseModule::AllocatorPtr& allocator, const ri::Response& resp, std::vector<std::byte>* blob)
{
    ResponseData out;
    out.success_ = true;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&resp);
    out.data_.assign(p, p + sizeof(resp));

    if (blob && !blob->empty())
    {
        auto blob_copy = allocator->allocateFromData(std::span<std::byte>(*blob));
        if (blob_copy.valid())
        {
            out.blobs_.push_back(std::move(blob_copy));
        }
    }
    return out;
}

ResponseData RobotModuleDummy::handleStartRobotControl(std::span<const std::byte> blob)
{
    rc::start::requests::deserialization::BufferReader reader(blob.data(), blob.size());
    rc::start::requests::deserialization::RequestVariant reqv;
    if (!rc::start::requests::deserialization::deserialize(reader, reqv))
    {
        ri::Response resp{ .resp_type = ri::RespType::DATA_INVALID, .action_id = 0 };
        return buildRobotInterfaceResponse(allocator_, resp, nullptr);
    }

    // GetRobotSpecs completes immediately
    if (std::holds_alternative<rc::start::requests::deserialization::GetRobotSpecsRequest>(reqv))
    {
        rc::RobotSpecs specs{};
        specs.max_velocity_linear = 5.0;
        specs.max_velocity_angular = 10.0;
        specs.max_acceleration_linear = 20.0;
        specs.max_acceleration_angular = 50.0;
        specs.num_joints = 6;
        specs.joint_limits.assign(6, rc::JointRange{ .min = -1e9, .max = 1e9 });

        std::vector<std::byte> resp_blob;
        rc::start::responses::serialization::robotSpecs(resp_blob, specs);

        ri::Response resp{ .resp_type = ri::RespType::SUCCESS, .action_id = 0 };
        return buildRobotInterfaceResponse(allocator_, resp, &resp_blob);
    }

    // MoveArc / MoveTrajectory currently not supported by dummy simulator.
    if (std::holds_alternative<rc::start::requests::deserialization::MoveArcRequest>(reqv) ||
        std::holds_alternative<rc::start::requests::deserialization::MoveTrajectoryRequest>(reqv))
    {
        std::vector<std::byte> err_blob;
        rc::common::serialization::errorMessage(err_blob, "Dummy robot does not support moveArc/moveTrajectory yet.");
        ri::Response resp{ .resp_type = ri::RespType::FAILURE, .action_id = 0 };
        return buildRobotInterfaceResponse(allocator_, resp, &err_blob);
    }

    uint64_t action_id{};
    {
        std::lock_guard<std::mutex> lock(sim_mutex_);
        // only one active move at a time (simple dummy)
        if (move_.active)
        {
            std::vector<std::byte> err_blob;
            rc::common::serialization::errorMessage(err_blob, "Dummy robot is busy (single active motion supported).");
            ri::Response resp{ .resp_type = ri::RespType::FAILURE, .action_id = 0 };
            return buildRobotInterfaceResponse(allocator_, resp, &err_blob);
        }

        action_id = next_action_id_++;
        move_.active = true;
        move_.cancel_requested = false;
        move_.action_id = action_id;
        std::copy(std::begin(sim_.xyzrpy), std::end(sim_.xyzrpy), std::begin(move_.start));
        std::copy(std::begin(sim_.xyzrpy), std::end(sim_.xyzrpy), std::begin(move_.target));
        move_.t_s = 0.0;

        if (auto* mj = std::get_if<rc::start::requests::deserialization::MoveJointRequest>(&reqv))
        {
            for (size_t i = 0; i < std::min<size_t>(6, mj->joint_targets.size()); ++i)
            {
                move_.target[i] = mj->joint_targets[i];
            }
            // duration from max delta / speed (avoid zero)
            double maxd = 0.0;
            for (int i = 0; i < 6; ++i) maxd = std::max(maxd, std::abs(move_.target[i] - move_.start[i]));
            double v = std::max(1e-6, std::abs(mj->speed));
            move_.duration_s = std::max(0.05, maxd / v);
        }
        else if (auto* ml = std::get_if<rc::start::requests::deserialization::MoveLinearRequest>(&reqv))
        {
            move_.target[0] = ml->pose_target.position.x;
            move_.target[1] = ml->pose_target.position.y;
            move_.target[2] = ml->pose_target.position.z;
            double r, p, y;
            rpyFromQuatWorld(ml->pose_target.orientation, r, p, y);
            move_.target[3] = r;
            move_.target[4] = p;
            move_.target[5] = y;

            double dx = move_.target[0] - move_.start[0];
            double dy = move_.target[1] - move_.start[1];
            double dz = move_.target[2] - move_.start[2];
            double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            double v = std::max(1e-6, std::abs(ml->speed));
            move_.duration_s = std::max(0.05, dist / v);
        }
    }

    ri::Response resp{ .resp_type = ri::RespType::SUCCESS_IN_PROGRESS, .action_id = action_id };
    return buildRobotInterfaceResponse(allocator_, resp, nullptr);
}

ResponseData RobotModuleDummy::handleUpdateRobotControl(uint64_t action_id, std::span<const std::byte> blob)
{
    rc::update::requests::deserialization::BufferReader reader(blob.data(), blob.size());
    rc::update::requests::MoveRequest mr{};
    if (!rc::update::requests::deserialization::deserializeMoveRequest(reader, mr))
    {
        ri::Response resp{ .resp_type = ri::RespType::DATA_INVALID, .action_id = action_id };
        return buildRobotInterfaceResponse(allocator_, resp, nullptr);
    }

    if (mr != rc::update::requests::MoveRequest::CancelMovement)
    {
        ri::Response resp{ .resp_type = ri::RespType::DATA_INVALID, .action_id = action_id };
        return buildRobotInterfaceResponse(allocator_, resp, nullptr);
    }

    bool did_cancel = false;
    {
        std::lock_guard<std::mutex> lock(sim_mutex_);
        if (!move_.active || move_.action_id != action_id)
        {
            ri::Response resp{ .resp_type = ri::RespType::NOT_IN_PROGRESS, .action_id = action_id };
            return buildRobotInterfaceResponse(allocator_, resp, nullptr);
        }
        move_.cancel_requested = true;
        did_cancel = true;
    }

    if (did_cancel)
    {
        ri::Response resp{ .resp_type = ri::RespType::SUCCESS, .action_id = action_id };
        return buildRobotInterfaceResponse(allocator_, resp, nullptr);
    }

    return { .success_ = false };
}

void RobotModuleDummy::simLoop()
{
    // 60+ Hz status publishing
    constexpr double dt_s = 1.0 / 120.0; // run at 120 Hz, publish every tick
    const auto dt = std::chrono::duration<double>(dt_s);

    while (!stop_.load(std::memory_order_acquire))
    {
        auto t0 = std::chrono::steady_clock::now();

        uint64_t finished_id = 0;
        bool finished_success = true;
        const char* finished_err = nullptr;

        {
            std::lock_guard<std::mutex> lock(sim_mutex_);

            if (move_.active)
            {
                if (move_.cancel_requested)
                {
                    finished_id = move_.action_id;
                    finished_success = false;
                    finished_err = "Cancelled";
                    move_ = ActiveMove{};
                }
                else
                {
                    move_.t_s += dt_s;
                    double a = (move_.duration_s <= 1e-9) ? 1.0 : std::clamp(move_.t_s / move_.duration_s, 0.0, 1.0);
                    for (int i = 0; i < 6; ++i)
                    {
                        sim_.xyzrpy[i] = move_.start[i] + (move_.target[i] - move_.start[i]) * a;
                    }

                    if (a >= 1.0)
                    {
                        finished_id = move_.action_id;
                        finished_success = true;
                        move_ = ActiveMove{};
                    }
                }
            }

            sim_.tfc_pose = makePoseFromXyzRpy(sim_.xyzrpy);
            sim_.tcp_pose = tcpFromTfc(sim_.tfc_pose);
        }

        // Publish status + update visualization
        {
            std::lock_guard<std::mutex> lock(vis_mutex_);
            if (!visualization_announced_)
            {
                visualization_helper_->announce();
                visualization_announced_ = true;
            }
            ensureVisObjectsCreatedLocked();
            updateVisLocked();
        }

        publishStatusLocked(nowMicros());

        if (finished_id != 0)
        {
            publishFinished(finished_id, finished_success, finished_err);
        }

        auto elapsed = std::chrono::steady_clock::now() - t0;
        if (elapsed < dt)
        {
            std::this_thread::sleep_for(dt - elapsed);
        }
    }
}

void RobotModuleDummy::publishStatusLocked(uint64_t timestamp_us)
{
    // Snapshot sim state
    SimState s;
    bool moving = false;
    {
        std::lock_guard<std::mutex> lock(sim_mutex_);
        s = sim_;
        moving = move_.active;
    }

    std::vector<double> joints(6);
    for (int i = 0; i < 6; ++i) joints[static_cast<size_t>(i)] = s.xyzrpy[i];

    std::vector<std::byte> blob;
    rc::status_messages::serialization::statusMessage(
        blob,
        timestamp_us,
        rc::Pose{ .position = {0,0,0}, .orientation = {0,0,0,1} }, // world origin / base
        s.tfc_pose,
        s.tcp_pose,
        joints,
        moving ? rc::RobotStatus::MOVING : rc::RobotStatus::IDLE,
        nullptr
    );

    message::SharedDataBlob blob_copy = allocator_->allocateFromData(std::span<std::byte>(blob));
    if (!blob_copy.valid())
    {
        return;
    }

    ri::StatusMessage status{ .feature = ri::RobotFeature::ROBOT_CONTROL };
    sendMessage(status_publish_id_, message::MessageHeader::Message(&status, &blob_copy));
}

void RobotModuleDummy::publishFinished(uint64_t action_id, bool success, const char* error_msg)
{
    ri::FinishedMessage finished{ .action_id = action_id, .success = success };

    if (success || error_msg == nullptr)
    {
        sendMessage(finished_publish_id_, message::MessageHeader::Message(&finished));
        return;
    }

    std::vector<std::byte> blob;
    rc::common::serialization::errorMessage(blob, error_msg);
    auto blob_copy = allocator_->allocateFromData(std::span<std::byte>(blob));
    if (!blob_copy.valid())
    {
        sendMessage(finished_publish_id_, message::MessageHeader::Message(&finished));
        return;
    }
    sendMessage(finished_publish_id_, message::MessageHeader::Message(&finished, &blob_copy));
}

// Kassow convention: roll about world X, pitch about world Y (after roll), yaw about world Z (after pitch)
rc::Quaternion RobotModuleDummy::quatFromWorldRpy(double roll, double pitch, double yaw)
{
    const double cr = std::cos(roll * 0.5), sr = std::sin(roll * 0.5);
    const double cp = std::cos(pitch * 0.5), sp = std::sin(pitch * 0.5);
    const double cy = std::cos(yaw * 0.5), sy = std::sin(yaw * 0.5);

    // q = qz(yaw) * qy(pitch) * qx(roll)
    rc::Quaternion q{};
    q.w = cy*cp*cr + sy*sp*sr;
    q.x = cy*cp*sr - sy*sp*cr;
    q.y = cy*sp*cr + sy*cp*sr;
    q.z = sy*cp*cr - cy*sp*sr;
    // normalize
    double s = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (s > 1e-12)
    {
        q.x /= s; q.y /= s; q.z /= s; q.w /= s;
    }
    else
    {
        q = rc::Quaternion{0,0,0,1};
    }
    return q;
}

void RobotModuleDummy::rpyFromQuatWorld(const rc::Quaternion& q_in, double& out_roll, double& out_pitch, double& out_yaw)
{
    // Extract RPY from quaternion assuming R = Rz(yaw) * Ry(pitch) * Rx(roll)
    const double x = q_in.x, y = q_in.y, z = q_in.z, w = q_in.w;
    const double r00 = 1 - 2*(y*y + z*z);
    const double r10 = 2*(x*y + w*z);
    const double r20 = 2*(x*z - w*y);
    const double r21 = 2*(y*z + w*x);
    const double r22 = 1 - 2*(x*x + y*y);

    out_pitch = std::asin(std::clamp(-r20, -1.0, 1.0));
    out_roll  = std::atan2(r21, r22);
    out_yaw   = std::atan2(r10, r00);
}

rc::Pose RobotModuleDummy::makePoseFromXyzRpy(const double xyzrpy[6])
{
    rc::Pose p{};
    p.position = rc::Vector3{ xyzrpy[0], xyzrpy[1], xyzrpy[2] };
    p.orientation = quatFromWorldRpy(xyzrpy[3], xyzrpy[4], xyzrpy[5]);
    return p;
}

static rc::Vector3 rotateLocalZToWorld(const rc::Quaternion& q, double z)
{
    // v_world = q * (0,0,z) * q^-1
    const double x = q.x, y = q.y, zq = q.z, w = q.w;
    // quaternion-vector multiplication shortcut
    const double vx = 0.0, vy = 0.0, vz = z;
    const double tx = 2.0 * (y * vz - zq * vy);
    const double ty = 2.0 * (zq * vx - x * vz);
    const double tz = 2.0 * (x * vy - y * vx);
    return rc::Vector3{
        vx + w * tx + (y * tz - zq * ty),
        vy + w * ty + (zq * tx - x * tz),
        vz + w * tz + (x * ty - y * tx)
    };
}

rc::Pose RobotModuleDummy::tcpFromTfc(const rc::Pose& tfc_pose)
{
    rc::Pose tcp = tfc_pose;
    const auto dz = rotateLocalZToWorld(tfc_pose.orientation, 0.10); // 10 cm
    tcp.position.x += dz.x;
    tcp.position.y += dz.y;
    tcp.position.z += dz.z;
    return tcp;
}

bool RobotModuleDummy::initVisResources()
{
    // Arrow resource (axes)
    auto axis_shape = [](const vis3d::Vec3& axis_dir, const vis3d::Color& color, vis3d::ComplexShape& out_shape) -> void {
        constexpr float line_length_m = 0.20f;
        constexpr float line_radius_m = 0.005f;
        constexpr float tip_radius_m = 0.01f;
        constexpr float tip_length_m = 0.02f;

        auto alignZToDir = [](const vis3d::Vec3& dir_in) -> vis3d::Quat {
            float norm = std::sqrt(dir_in.x * dir_in.x + dir_in.y * dir_in.y + dir_in.z * dir_in.z);
            if (norm < 1e-6f) return vis3d::Quat::Identity();
            float dx = dir_in.x / norm, dy = dir_in.y / norm, dz = dir_in.z / norm;
            vis3d::Vec3 z{0.f, 0.f, 1.f};
            vis3d::Vec3 v{z.y * dz - z.z * dy, z.z * dx - z.x * dz, z.x * dy - z.y * dx};
            float dot = z.x * dx + z.y * dy + z.z * dz;
            float s = std::sqrt((1.f + dot) * 2.f);
            if (s < 1e-6f) return vis3d::Quat::FromAxisDeg(1.f, 0.f, 0.f, 180.f);
            float invs = 1.f / s;
            vis3d::Quat q{v.x * invs, v.y * invs, v.z * invs, s * 0.5f};
            return q.normalized();
        };

        float line_offset = line_length_m * 0.5f;
        vis3d::PrimitiveShape line{
            .type = vis3d::PrimitiveShapeType::CYLINDER,
            .desc = vis3d::CylinderDesc{ .rBot = line_radius_m, .rTop = line_radius_m, .h = line_length_m },
            .origin = vis3d::Pose{ .t = vis3d::Vec3{axis_dir.x * line_offset, axis_dir.y * line_offset, axis_dir.z * line_offset}, .q = alignZToDir(axis_dir) },
            .color = color
        };

        float tip_offset = line_length_m + (tip_length_m * 0.5f);
        vis3d::PrimitiveShape tip{
            .type = vis3d::PrimitiveShapeType::CYLINDER,
            .desc = vis3d::CylinderDesc{ .rBot = tip_radius_m, .rTop = 0.0f, .h = tip_length_m },
            .origin = vis3d::Pose{ .t = vis3d::Vec3{axis_dir.x * tip_offset, axis_dir.y * tip_offset, axis_dir.z * tip_offset}, .q = alignZToDir(axis_dir) },
            .color = color
        };

        out_shape.parts.push_back(line);
        out_shape.parts.push_back(tip);
    };

    vis3d::ComplexShape arrow_shape{};
    axis_shape(vis3d::Vec3{1.f, 0.f, 0.f}, vis3d::Color{255, 0, 0, 255}, arrow_shape);
    axis_shape(vis3d::Vec3{0.f, 1.f, 0.f}, vis3d::Color{0, 255, 0, 255}, arrow_shape);
    axis_shape(vis3d::Vec3{0.f, 0.f, 1.f}, vis3d::Color{0, 0, 255, 255}, arrow_shape);
    axes_resource_ = visualization_helper_->registerResource(arrow_shape);
    if (axes_resource_.id == 0) return false;

    // "Head" sphere at TFC
    vis3d::ComplexShape head_shape{};
    head_shape.parts.push_back(vis3d::PrimitiveShape{
        .type = vis3d::PrimitiveShapeType::SPHERE,
        .desc = vis3d::SphereDesc{ .r = 0.035f },
        .origin = vis3d::Pose{},
        .color = vis3d::Color{ 0x21, 0x21, 0x21, 0xFF }
    });
    head_resource_ = visualization_helper_->registerResource(head_shape);
    if (head_resource_.id == 0) return false;

    return true;
}

bool RobotModuleDummy::ensureVisObjectsCreatedLocked()
{
    if (vis_objects_created_) return true;
    if (!visualization_announced_) return false;

    vis3d::Pose pose{};
    bool ok = true;
    ok = ok && visualization_helper_->addObject(axes_resource_, pose, world_axes_obj_);
    ok = ok && visualization_helper_->addObject(axes_resource_, pose, tfc_axes_obj_);
    ok = ok && visualization_helper_->addObject(axes_resource_, pose, tcp_axes_obj_);
    ok = ok && visualization_helper_->addObject(head_resource_, pose, head_obj_);
    ok = ok && visualization_helper_->sendUpdate();

    vis_objects_created_ = ok;
    return ok;
}

static vis3d::Pose toVisPose(const rc::Pose& p)
{
    return vis3d::Pose{
        .t = vis3d::Vec3{ static_cast<float>(p.position.x), static_cast<float>(p.position.y), static_cast<float>(p.position.z) },
        .q = vis3d::Quat{ static_cast<float>(p.orientation.x), static_cast<float>(p.orientation.y), static_cast<float>(p.orientation.z), static_cast<float>(p.orientation.w) }.normalized()
    };
}

void RobotModuleDummy::updateVisLocked()
{
    if (!vis_objects_created_) return;

    SimState s;
    {
        std::lock_guard<std::mutex> lock(sim_mutex_);
        s = sim_;
    }

    // world origin axes
    visualization_helper_->updateObject(world_axes_obj_, vis3d::Pose{ .t = vis3d::Vec3{0,0,0}, .q = vis3d::Quat::Identity() });
    // TFC axes + head
    visualization_helper_->updateObject(tfc_axes_obj_, toVisPose(s.tfc_pose));
    visualization_helper_->updateObject(head_obj_, toVisPose(s.tfc_pose));
    // TCP axes
    visualization_helper_->updateObject(tcp_axes_obj_, toVisPose(s.tcp_pose));
    visualization_helper_->sendUpdate();
}

