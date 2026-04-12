#include "robot_module_dummy/robot_module_dummy.h"
#include "robot_module_dummy/dummy_robot_web_app.h"

#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/serialization_helper/serialization_helper.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
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
    if (!parseConfigFile())
    {
        log(logging::LogType::ERROR, "Failed to parse dummy robot web UI configuration");
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

    try
    {
        if (w_server_)
        {
            return false;
        }

        auto args = makeServerArgs();
        std::vector<char*> cargs;
        cargs.reserve(args.size());
        for (auto& arg : args)
        {
            cargs.push_back(arg.data());
        }

        w_server_ = std::make_unique<Wt::WServer>("dummy_robot_ui", server_parameters_.wt_config_path);
        w_server_->setServerConfiguration(static_cast<int>(cargs.size()), cargs.data());
        w_server_->addEntryPoint(Wt::EntryPointType::Application, [this](const Wt::WEnvironment& env) {
            return std::make_unique<DummyRobotWebApp>(env, this);
        });
        if (!w_server_->start())
        {
            log(logging::LogType::ERROR, "Dummy robot UI server failed to start");
            w_server_.reset();
            return false;
        }

        stop_.store(false, std::memory_order_release);
        sim_thread_ = std::thread(&RobotModuleDummy::simLoop, this);
        return true;
    }
    catch (const std::exception& e)
    {
        log(logging::LogType::ERROR, std::string("Failed to start dummy robot UI server: ") + e.what());
        w_server_.reset();
        return false;
    }
}

bool RobotModuleDummy::threadStop(uint32_t) noexcept
{
    stop_.store(true, std::memory_order_release);
    if (w_server_)
    {
        try
        {
            w_server_->stop();
        }
        catch (const std::exception& e)
        {
            log(logging::LogType::ERROR, std::string("Failed to stop dummy robot UI server: ") + e.what());
        }
        w_server_.reset();
    }
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
        resetTrajectoryLocked();
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
        move_.move_space = MoveSpace::TFC;
        move_.start_pos[0] = sim_.tfc_pose.position.x;
        move_.start_pos[1] = sim_.tfc_pose.position.y;
        move_.start_pos[2] = sim_.tfc_pose.position.z;
        move_.target_pos[0] = sim_.tfc_pose.position.x;
        move_.target_pos[1] = sim_.tfc_pose.position.y;
        move_.target_pos[2] = sim_.tfc_pose.position.z;
        move_.start_quat = sim_.tfc_pose.orientation;
        move_.target_quat = sim_.tfc_pose.orientation;
        move_.t_s = 0.0;

        if (auto* mj = std::get_if<rc::start::requests::deserialization::MoveJointRequest>(&reqv))
        {
            double target_xyzrpy[6];
            std::copy(std::begin(sim_.xyzrpy), std::end(sim_.xyzrpy), std::begin(target_xyzrpy));
            for (size_t i = 0; i < std::min<size_t>(6, mj->joint_targets.size()); ++i)
            {
                target_xyzrpy[i] = mj->joint_targets[i];
            }
            const rc::Pose target_tfc = makePoseFromXyzRpy(target_xyzrpy);
            move_.target_pos[0] = target_tfc.position.x;
            move_.target_pos[1] = target_tfc.position.y;
            move_.target_pos[2] = target_tfc.position.z;
            move_.target_quat = target_tfc.orientation;
            // duration from max delta / speed (avoid zero)
            double maxd = 0.0;
            for (int i = 0; i < 6; ++i)
            {
                maxd = std::max(maxd, std::abs(target_xyzrpy[i] - sim_.xyzrpy[i]));
            }
            double v = std::max(1e-6, std::abs(mj->speed));
            move_.duration_s = std::max(0.05, maxd / v);
        }
        else if (auto* ml = std::get_if<rc::start::requests::deserialization::MoveLinearRequest>(&reqv))
        {
            move_.move_space = MoveSpace::TCP;
            move_.target_pos[0] = ml->pose_target.position.x;
            move_.target_pos[1] = ml->pose_target.position.y;
            move_.target_pos[2] = ml->pose_target.position.z;
            move_.target_quat = quatNormalize(ml->pose_target.orientation);

            move_.start_pos[0] = sim_.tcp_pose.position.x;
            move_.start_pos[1] = sim_.tcp_pose.position.y;
            move_.start_pos[2] = sim_.tcp_pose.position.z;
            move_.start_quat = sim_.tcp_pose.orientation;

            double dx = move_.target_pos[0] - move_.start_pos[0];
            double dy = move_.target_pos[1] - move_.start_pos[1];
            double dz = move_.target_pos[2] - move_.start_pos[2];
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

RobotModuleDummy::UiSnapshot RobotModuleDummy::getUiSnapshot()
{
    UiSnapshot snapshot{};
    std::lock_guard<std::mutex> lock(sim_mutex_);

    snapshot.valid = valid_;
    snapshot.moving = move_.active;
    snapshot.active_action_id = move_.active ? move_.action_id : 0;
    snapshot.has_error = !last_error_message_.empty();
    snapshot.error_text = last_error_message_;
    for (int i = 0; i < 6; ++i)
    {
        snapshot.joints_rad[static_cast<std::size_t>(i)] = sim_.xyzrpy[i];
        snapshot.tfc_xyzrpy[static_cast<std::size_t>(i)] = sim_.xyzrpy[i];
    }

    snapshot.tcp_xyzrpy[0] = sim_.tcp_pose.position.x;
    snapshot.tcp_xyzrpy[1] = sim_.tcp_pose.position.y;
    snapshot.tcp_xyzrpy[2] = sim_.tcp_pose.position.z;
    rpyFromQuatWorld(sim_.tcp_pose.orientation, snapshot.tcp_xyzrpy[3], snapshot.tcp_xyzrpy[4], snapshot.tcp_xyzrpy[5]);

    return snapshot;
}

RobotModuleDummy::UiCommandResult RobotModuleDummy::tryStartMoveLocked(const ActiveMove& move_template)
{
    UiCommandResult result{};

    if (move_.active)
    {
        result.message = "Dummy robot is busy.";
        return result;
    }

    move_ = move_template;
    move_.active = true;
    move_.cancel_requested = false;
    move_.action_id = next_action_id_++;
    move_.t_s = 0.0;
    last_error_message_.clear();

    result.success = true;
    result.action_id = move_.action_id;
    result.message = "Move started.";
    return result;
}

RobotModuleDummy::UiCommandResult RobotModuleDummy::startUiMoveJoint(const std::array<double, 6>& joint_targets_rad, double speed_rad_s)
{
    std::lock_guard<std::mutex> lock(sim_mutex_);

    if (speed_rad_s <= 0.0)
    {
        return UiCommandResult{ .success = false, .message = "Joint speed must be positive." };
    }

    ActiveMove move_template{};
    move_template.move_space = MoveSpace::TFC;
    move_template.start_pos[0] = sim_.tfc_pose.position.x;
    move_template.start_pos[1] = sim_.tfc_pose.position.y;
    move_template.start_pos[2] = sim_.tfc_pose.position.z;
    move_template.start_quat = sim_.tfc_pose.orientation;

    double target_xyzrpy[6];
    std::copy(std::begin(sim_.xyzrpy), std::end(sim_.xyzrpy), std::begin(target_xyzrpy));
    for (std::size_t i = 0; i < 6; ++i)
    {
        target_xyzrpy[i] = joint_targets_rad[i];
    }

    const rc::Pose target_tfc = makePoseFromXyzRpy(target_xyzrpy);
    move_template.target_pos[0] = target_tfc.position.x;
    move_template.target_pos[1] = target_tfc.position.y;
    move_template.target_pos[2] = target_tfc.position.z;
    move_template.target_quat = target_tfc.orientation;

    double max_delta = 0.0;
    for (std::size_t i = 0; i < 6; ++i)
    {
        max_delta = std::max(max_delta, std::abs(joint_targets_rad[i] - sim_.xyzrpy[i]));
    }
    move_template.duration_s = std::max(0.05, max_delta / speed_rad_s);

    return tryStartMoveLocked(move_template);
}

RobotModuleDummy::UiCommandResult RobotModuleDummy::startUiMoveLinear(const std::array<double, 6>& tcp_xyzrpy_rad, double speed_m_s)
{
    std::lock_guard<std::mutex> lock(sim_mutex_);

    if (speed_m_s <= 0.0)
    {
        return UiCommandResult{ .success = false, .message = "Linear speed must be positive." };
    }

    ActiveMove move_template{};
    move_template.move_space = MoveSpace::TCP;
    move_template.start_pos[0] = sim_.tcp_pose.position.x;
    move_template.start_pos[1] = sim_.tcp_pose.position.y;
    move_template.start_pos[2] = sim_.tcp_pose.position.z;
    move_template.start_quat = sim_.tcp_pose.orientation;

    move_template.target_pos[0] = tcp_xyzrpy_rad[0];
    move_template.target_pos[1] = tcp_xyzrpy_rad[1];
    move_template.target_pos[2] = tcp_xyzrpy_rad[2];
    move_template.target_quat = quatFromWorldRpy(tcp_xyzrpy_rad[3], tcp_xyzrpy_rad[4], tcp_xyzrpy_rad[5]);

    const double dx = move_template.target_pos[0] - move_template.start_pos[0];
    const double dy = move_template.target_pos[1] - move_template.start_pos[1];
    const double dz = move_template.target_pos[2] - move_template.start_pos[2];
    const double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    move_template.duration_s = std::max(0.05, dist / speed_m_s);

    return tryStartMoveLocked(move_template);
}

RobotModuleDummy::UiCommandResult RobotModuleDummy::cancelUiMove()
{
    std::lock_guard<std::mutex> lock(sim_mutex_);
    if (!move_.active)
    {
        return UiCommandResult{ .success = false, .message = "No active move." };
    }

    move_.cancel_requested = true;
    return UiCommandResult{ .success = true, .action_id = move_.action_id, .message = "Cancel requested." };
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
        rc::Vector3 tcp_trajectory_point{};

        {
            std::lock_guard<std::mutex> lock(sim_mutex_);

            if (move_.active)
            {
                if (move_.cancel_requested)
                {
                    finished_id = move_.action_id;
                    finished_success = false;
                    finished_err = "Cancelled";
                    last_error_message_ = finished_err;
                    move_ = ActiveMove{};
                }
                else
                {
                    move_.t_s += dt_s;
                    double a = (move_.duration_s <= 1e-9) ? 1.0 : std::clamp(move_.t_s / move_.duration_s, 0.0, 1.0);
                    double pos[3]{
                        move_.start_pos[0] + (move_.target_pos[0] - move_.start_pos[0]) * a,
                        move_.start_pos[1] + (move_.target_pos[1] - move_.start_pos[1]) * a,
                        move_.start_pos[2] + (move_.target_pos[2] - move_.start_pos[2]) * a
                    };
                    const rc::Quaternion quat = quatSlerpShortest(move_.start_quat, move_.target_quat, a);
                    const rc::Pose motion_pose = makePoseFromPosQuat(pos, quat);

                    if (move_.move_space == MoveSpace::TCP)
                    {
                        sim_.tcp_pose = motion_pose;
                        sim_.tfc_pose = tfcFromTcp(sim_.tcp_pose);
                    }
                    else
                    {
                        sim_.tfc_pose = motion_pose;
                        sim_.tcp_pose = tcpFromTfc(sim_.tfc_pose);
                    }

                    sim_.xyzrpy[0] = sim_.tfc_pose.position.x;
                    sim_.xyzrpy[1] = sim_.tfc_pose.position.y;
                    sim_.xyzrpy[2] = sim_.tfc_pose.position.z;
                    rpyFromQuatWorld(sim_.tfc_pose.orientation, sim_.xyzrpy[3], sim_.xyzrpy[4], sim_.xyzrpy[5]);

                    if (a >= 1.0)
                    {
                        finished_id = move_.action_id;
                        finished_success = true;
                        last_error_message_.clear();
                        move_ = ActiveMove{};
                    }
                }
            }

            if (!move_.active)
            {
                sim_.tfc_pose = makePoseFromXyzRpy(sim_.xyzrpy);
                sim_.tcp_pose = tcpFromTfc(sim_.tfc_pose);
            }
            tcp_trajectory_point = sim_.tcp_pose.position;
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
            updateTrajectoryLocked(tcp_trajectory_point);
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

// Dummy robot UI convention:
// user-facing order is roll, then pitch, then yaw, but yaw should act in the
// already-rotated local frame so that changing yaw spins the head around its
// own axis instead of orbiting around the world origin.
// For column-vector composition this is R = Rx(roll) * Ry(pitch) * Rz(yaw).
rc::Quaternion RobotModuleDummy::quatFromWorldRpy(double roll, double pitch, double yaw)
{
    const double cr = std::cos(roll * 0.5), sr = std::sin(roll * 0.5);
    const double cp = std::cos(pitch * 0.5), sp = std::sin(pitch * 0.5);
    const double cy = std::cos(yaw * 0.5), sy = std::sin(yaw * 0.5);

    // q = qx(roll) * qy(pitch) * qz(yaw)
    rc::Quaternion q{};
    q.w = cr*cp*cy - sr*sp*sy;
    q.x = sr*cp*cy + cr*sp*sy;
    q.y = cr*sp*cy - sr*cp*sy;
    q.z = cr*cp*sy + sr*sp*cy;
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
    const rc::Quaternion q = quatNormalize(q_in);
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    const double r00 = 1.0 - 2.0*(y*y + z*z);
    const double r01 = 2.0*(x*y - w*z);
    const double r02 = 2.0*(x*z + w*y);
    const double r12 = 2.0*(y*z - w*x);
    const double r22 = 1.0 - 2.0*(x*x + y*y);

    out_pitch = std::asin(std::clamp(r02, -1.0, 1.0));
    out_roll  = std::atan2(-r12, r22);
    out_yaw   = std::atan2(-r01, r00);
}

rc::Pose RobotModuleDummy::makePoseFromXyzRpy(const double xyzrpy[6])
{
    rc::Pose p{};
    p.position = rc::Vector3{ xyzrpy[0], xyzrpy[1], xyzrpy[2] };
    p.orientation = quatFromWorldRpy(xyzrpy[3], xyzrpy[4], xyzrpy[5]);
    return p;
}

rc::Pose RobotModuleDummy::makePoseFromPosQuat(const double pos[3], const rc::Quaternion& quat)
{
    rc::Pose p{};
    p.position = rc::Vector3{ pos[0], pos[1], pos[2] };
    p.orientation = quatNormalize(quat);
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

rc::Pose RobotModuleDummy::tfcFromTcp(const rc::Pose& tcp_pose)
{
    rc::Pose tfc = tcp_pose;
    const auto dz = rotateLocalZToWorld(tcp_pose.orientation, 0.10);
    tfc.position.x -= dz.x;
    tfc.position.y -= dz.y;
    tfc.position.z -= dz.z;
    return tfc;
}

rc::Quaternion RobotModuleDummy::quatNormalize(const rc::Quaternion& q)
{
    const double n = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (n <= 1e-12)
    {
        return rc::Quaternion{0.0, 0.0, 0.0, 1.0};
    }
    return rc::Quaternion{ q.x / n, q.y / n, q.z / n, q.w / n };
}

double RobotModuleDummy::quatDot(const rc::Quaternion& a, const rc::Quaternion& b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
}

rc::Quaternion RobotModuleDummy::quatNegated(const rc::Quaternion& q)
{
    return rc::Quaternion{ -q.x, -q.y, -q.z, -q.w };
}

rc::Quaternion RobotModuleDummy::quatSlerpShortest(rc::Quaternion a, rc::Quaternion b, double t)
{
    a = quatNormalize(a);
    b = quatNormalize(b);

    double dot = quatDot(a, b);
    if (dot < 0.0)
    {
        b = quatNegated(b);
        dot = -dot;
    }

    dot = std::clamp(dot, -1.0, 1.0);
    if (dot > 0.9995)
    {
        return quatNormalize(rc::Quaternion{
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        });
    }

    const double theta = std::acos(dot);
    const double sin_theta = std::sin(theta);
    const double w0 = std::sin((1.0 - t) * theta) / sin_theta;
    const double w1 = std::sin(t * theta) / sin_theta;
    return quatNormalize(rc::Quaternion{
        a.x * w0 + b.x * w1,
        a.y * w0 + b.y * w1,
        a.z * w0 + b.z * w1,
        a.w * w0 + b.w * w1
    });
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

bool RobotModuleDummy::updateTrajectoryLocked(const rc::Vector3& point, uint16_t history_length)
{
    const auto last_point = last_trajectory_point_;
    last_trajectory_point_ = point;

    if (trajectory_length_ == 0)
    {
        trajectory_length_ = 1;
        return true;
    }

    const vis3d::Vec3 vis_point{
        static_cast<float>(point.x),
        static_cast<float>(point.y),
        static_cast<float>(point.z)
    };

    if (trajectory_length_ == 1)
    {
        if (!visualization_helper_->addTrajectory(
            {
                vis3d::Vec3{ static_cast<float>(last_point.x), static_cast<float>(last_point.y), static_cast<float>(last_point.z) },
                vis_point
            },
            trajectory_color_,
            false,
            trajectory_obj_))
        {
            trajectory_obj_ = vis3d::ObjectId{0};
            trajectory_length_ = 0;
            return false;
        }

        trajectory_length_ = 2;
        return true;
    }

    if (std::abs(last_point.x - point.x) < 1e-6 &&
        std::abs(last_point.y - point.y) < 1e-6 &&
        std::abs(last_point.z - point.z) < 1e-6)
    {
        return true;
    }

    if (!visualization_helper_->updateTrajectory(
        trajectory_obj_,
        { vis_point },
        (trajectory_length_ < history_length) ? 0 : 1))
    {
        return false;
    }

    if (trajectory_length_ < history_length)
    {
        trajectory_length_ += 1;
    }
    return true;
}

void RobotModuleDummy::resetTrajectoryLocked()
{
    if (trajectory_obj_.id != 0)
    {
        visualization_helper_->removeTrajectory(trajectory_obj_);
        trajectory_obj_ = vis3d::ObjectId{0};
    }
    trajectory_length_ = 0;
    last_trajectory_point_ = rc::Vector3{};
}

std::string RobotModuleDummy::trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

bool RobotModuleDummy::parseConfigFile()
{
    namespace fs = std::filesystem;

    const auto& data_path = getDataPath();
    if (data_path.empty())
    {
        log(logging::LogType::ERROR, "Dummy robot module requires a data path for web UI config.");
        return false;
    }

    const fs::path cfg_path = fs::path(data_path) / "config.txt";
    if (!fs::exists(cfg_path))
    {
        log(logging::LogType::ERROR, "Dummy robot config file not found: " + cfg_path.string());
        return false;
    }

    std::ifstream input(cfg_path);
    if (!input)
    {
        log(logging::LogType::ERROR, "Failed to open dummy robot config file: " + cfg_path.string());
        return false;
    }

    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.rfind('#', 0) == 0)
        {
            continue;
        }
        const auto pos = line.find('=');
        if (pos == std::string::npos)
        {
            continue;
        }

        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        if (!key.empty())
        {
            kv[key] = value;
        }
    }

    for (const auto* key : { "DOCROOT", "PORT_HTTP", "WT_CONFIG" })
    {
        if (kv.find(key) == kv.end())
        {
            log(logging::LogType::ERROR, std::string("Missing dummy robot config key: ") + key);
            return false;
        }
    }

    try
    {
        const int port = std::stoi(kv["PORT_HTTP"]);
        if (port < 1 || port > 65535)
        {
            log(logging::LogType::ERROR, "Invalid dummy robot PORT_HTTP value.");
            return false;
        }
        server_parameters_.port_http = static_cast<uint16_t>(port);
    }
    catch (...)
    {
        log(logging::LogType::ERROR, "Failed to parse dummy robot PORT_HTTP.");
        return false;
    }

    auto make_path = [&](const std::string& raw) {
        fs::path path = raw;
        if (path.is_relative())
        {
            path = fs::path(data_path) / path;
        }
        return path.string();
    };

    server_parameters_.docroot = make_path(kv["DOCROOT"]);
    server_parameters_.wt_config_path = make_path(kv["WT_CONFIG"]);

    if (!fs::exists(server_parameters_.docroot))
    {
        log(logging::LogType::ERROR, "Dummy robot DOCROOT not found: " + server_parameters_.docroot);
        return false;
    }
    if (!fs::exists(server_parameters_.wt_config_path))
    {
        log(logging::LogType::ERROR, "Dummy robot WT_CONFIG not found: " + server_parameters_.wt_config_path);
        return false;
    }

    return true;
}

std::vector<std::string> RobotModuleDummy::makeServerArgs() const
{
    return {
        "dummy_robot_ui",
        "--docroot", server_parameters_.docroot,
        "--http-address", "0.0.0.0",
        "--http-port", std::to_string(server_parameters_.port_http)
    };
}

