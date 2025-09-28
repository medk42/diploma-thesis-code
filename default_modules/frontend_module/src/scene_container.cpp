#include "webapp/ui/helper/scene_container.h"

#include <Wt/WServer.h>

#undef ERROR // Gotta love Windows.h

#include <sstream>
#include <iomanip>
#include <iostream>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;



// Binary protocol for SceneSocket:
//    HEADER:
//        u32 magic              // 0x314E4353  == "SCN1" in LE
//        u32 seq
//        u8  grid_commanded     // 0 or 1
//        u8  grid_enabled       // 0 or 1
//    
//    REGISTRATION_SECTION:
//        u32 registration_count
//        repeat registration_count times:
//        {
//            u32   resource_id
//            u32   part_count
//            repeat part_count times:
//            {
//                u8 type                 // 0=Box, 1=Sphere, 2=Cylinder
//                switch(type):
//                    case Box:      f32 sx, sy, sz
//                    case Sphere:   f32 r
//                    case Cylinder: f32 rTop, rBot, h
//                Pose7 origin            // local pose of this primitive
//                u8 r, g, b, a           // color
//            }
//        }
//        
//    OBJECT_SECTION:
//        u32 object_count
//        repeat object_count times:
//        {
//            u32 id
//            u8  action               // 0=Add, 1=Update, 2=Remove
//            if action == Add:
//                u32 resource_id
//            if action != Remove:
//                Pose7 pose
//        }
//    
//    TRAJECTORY_SECTION:
//        u32 trajectory_count
//        repeat trajectory_count times:
//        {
//            u32 id
//            u8  action               // 0=Add, 1=Update, 2=Remove
//        
//            if action == Add:
//                u8  dashed           // 0/1
//                u32 point_count
//                repeat point_count times: f32 x, y, z
//        
//            if action == Update:
//                u32 point_count
//                repeat point_count times: f32 x, y, z
//                u32 remove_from_head
//        
//            // if Remove: no payload
//        }



SceneSocket::SceneSocket()
{
    setTakesUpdateLock(false);
    startWorkers();
}



SceneSocket::~SceneSocket()
{
    running_ = false;
    cv_.notify_one();
    if (send_worker_.joinable())
    {
        send_worker_.join();
    }

    shutdown();
}



void pushUint32(std::vector<char>& buf, uint32_t v)
{
    const char* byte_data = reinterpret_cast<const char*>(&v);
    buf.insert(buf.end(), byte_data, byte_data + sizeof(uint32_t));
}



void pushUint8(std::vector<char>& buf, uint8_t v)
{
    buf.push_back(static_cast<char>(v));
}



void pushF32(std::vector<char>& buf, float v)
{
    static_assert(sizeof(float) == 4, "float must be 4 bytes");

    const char* byte_data = reinterpret_cast<const char*>(&v);
    buf.insert(buf.end(), byte_data, byte_data + sizeof(float));
}



/// @brief Push pose (t: x,y,z; q: x,y,z,w) as 7 floats (4 bytes each, little-endian) into buffer
void pushPose(std::vector<char>& buf, const Pose& pose)
{
    // [7*f32 t.x,t.y,t.z,q.x,q.y,q.z,q.w]
    pushF32(buf, pose.t.x);
    pushF32(buf, pose.t.y);
    pushF32(buf, pose.t.z);
    pushF32(buf, pose.q.x);
    pushF32(buf, pose.q.y);
    pushF32(buf, pose.q.z);
    pushF32(buf, pose.q.w);
}



bool pushPendingRegistration(std::vector<char>& buf, const std::vector<std::tuple<ResourceId, ComplexShape>>& registrations)
{
    uint32_t registration_count = static_cast<uint32_t>(registrations.size());
    pushUint32(buf, registration_count); // [u32 registration_count]

    for (const auto& [res_id, shape] : registrations)
    {
        pushUint32(buf, res_id.id);      // [u32 resource_id]
        uint32_t part_count = static_cast<uint32_t>(shape.parts.size());
        pushUint32(buf, part_count);     // [u32 part_count]
        for (const auto& part : shape.parts)
        {
            // Box: [u8 type=0][3*f32 sx,sy,sz][7*f32 origin][4*u8 color]
            // Sphere: [u8 type=1][1*f32 r][7*f32 origin][4*u8 color]
            // Cylinder: [u8 type=2][3*f32 rTop,rBot,h][7*f32 origin][4*u8 color]

            // push type
            pushUint8(buf, static_cast<uint8_t>(part.type));

            // push description
            if (part.type == PrimitiveShapeType::BOX)
            {
                if (!std::holds_alternative<BoxDesc>(part.desc))
                {
                    return false; // invalid
                }
                const BoxDesc& d = std::get<BoxDesc>(part.desc);
                pushF32(buf, d.sx);
                pushF32(buf, d.sy);
                pushF32(buf, d.sz);
            }
            else if (part.type == PrimitiveShapeType::SPHERE)
            {
                if (!std::holds_alternative<SphereDesc>(part.desc))
                {
                    return false; // invalid
                }
                const SphereDesc& d = std::get<SphereDesc>(part.desc);
                pushF32(buf, d.r);
            }
            else if (part.type == PrimitiveShapeType::CYLINDER)
            {
                if (!std::holds_alternative<CylinderDesc>(part.desc))
                {
                    return false; // invalid
                }
                const CylinderDesc& d = std::get<CylinderDesc>(part.desc);
                pushF32(buf, d.rTop);
                pushF32(buf, d.rBot);
                pushF32(buf, d.h);
            }

            // push local origin pose
            pushPose(buf, part.origin);

            // push color
            pushUint8(buf, part.color.r);
            pushUint8(buf, part.color.g);
            pushUint8(buf, part.color.b);
            pushUint8(buf, part.color.a);
        }
    }

    return true;
}



void pushObjectCommands(std::vector<char>& buf, const std::map<ObjectId, CommandBuffer::ObjectParameters>& objects)
{
    uint32_t object_count = static_cast<uint32_t>(objects.size());
    pushUint32(buf, object_count); // [u32 object_count]

    for (const auto& [obj_id, params] : objects)
    {
        // Add: [u32 new_id][u8 action=0][u32 resource_id][7*f32 pose]
        // Update: [u32 id][u8 action=1][7*f32 pose]
        // Remove: [u32 id][u8 action=2]

        pushUint32(buf, obj_id.id);
        pushUint8(buf, static_cast<uint8_t>(params.action));
        if (params.action == CommandBuffer::Action::ADD)
        {
            pushUint32(buf, params.resource_id.id);
            pushPose(buf, params.pose);
        }
        if (params.action == CommandBuffer::Action::UPDATE)
        {
            pushPose(buf, params.pose);
        }
    }
}



void pushTrajectoryCommands(std::vector<char>& buf, const std::map<ObjectId, CommandBuffer::TrajectoryParameters>& trajectories)
{
    uint32_t trajectory_count = static_cast<uint32_t>(trajectories.size());
    pushUint32(buf, trajectory_count); // [u32 trajectory_count]

    for (const auto& [traj_id, params] : trajectories)
    {
        // Add: [u32 new_id][u8 action=0][u8 dashed][u32 point_count][point_count*3*f32 points]
        // Update: [u32 id][u8 action=1][u32 point_count][point_count*3*f32 points][u32 remove_from_head]
        // Remove: [u32 id][u8 action=2]

        pushUint32(buf, traj_id.id);                             // [u32 id]
        pushUint8(buf, static_cast<uint8_t>(params.action));    // [u8 action]
        if (params.action == CommandBuffer::Action::ADD)
        {
            pushUint8(buf, params.dashed ? 1 : 0);          // [u8 dashed]
            uint32_t point_count = static_cast<uint32_t>(params.points.size());
            pushUint32(buf, point_count);                        // [u32 point_count]
            for (const auto& p : params.points)
            {
                pushF32(buf, p.x);                             // [f32 x]
                pushF32(buf, p.y);                             // [f32 y]
                pushF32(buf, p.z);                             // [f32 z]
            }
        }
        else if (params.action == CommandBuffer::Action::UPDATE)
        {
            uint32_t point_count = static_cast<uint32_t>(params.points.size());
            pushUint32(buf, point_count);                        // [u32 point_count]
            for (const auto& p : params.points)
            {
                pushF32(buf, p.x);                             // [f32 x]
                pushF32(buf, p.y);                             // [f32 y]
                pushF32(buf, p.z);                             // [f32 z]
            }
            pushUint32(buf, params.remove_from_head);            // [u32 remove_from_head]
        }
    }
}



size_t SceneSocket::sendCommandBuffer(const CommandBuffer& cmd_buf)
{
    std::vector<char> command_frame;

    uint32_t magic = 0x314E4353u; // 'SCN1' LE
    pushUint32(command_frame, magic);  // [u32 magic 'SCN1']
    uint32_t seq = ++seq_;
    pushUint32(command_frame, seq);    // [u32 seq]
    pushUint8(command_frame, cmd_buf.grid_commanded_ ? 1 : 0); // [u8 grid_commanded]
    pushUint8(command_frame, cmd_buf.grid_enabled_ ? 1 : 0);   // [u8 grid_enabled]
    
    if (!pushPendingRegistration(command_frame, cmd_buf.pending_registrations_))
    {
        return 0; // invalid registration
    }
    pushObjectCommands(command_frame, cmd_buf.objects_);
    pushTrajectoryCommands(command_frame, cmd_buf.trajectories_);

    std::lock_guard<std::mutex> lk(m_);
    q_.emplace_back(std::move(command_frame));
    cv_.notify_one();
    return q_.size();
}



std::unique_ptr<Wt::WWebSocketConnection> SceneSocket::handleConnect(const Wt::Http::Request &req)
{
    auto c = std::make_unique<Wt::WWebSocketConnection>(this, Wt::WServer::instance()->ioService());
    c->setTakesUpdateLock(false);

    {
        std::lock_guard<std::mutex> lk(m_);
        conn_ = c.get();
        cv_.notify_one(); // wake up send worker, we might have queued messages
    }
    
    c->done().connect([this](const Wt::AsioWrapper::error_code& ec) {
        std::lock_guard<std::mutex> lk(m_);
        sending_ = false;
        cv_.notify_one();
    });
    
    return c;
}



void SceneSocket::startWorkers()
{
    if (running_)
    {
        return;
    }
    running_ = true;

    send_worker_ = std::thread([this]() {
        std::unique_lock<std::mutex> lk(m_);
        while (running_) {
            cv_.wait(lk, [this] { return (!sending_ && !q_.empty() && conn_) || !running_; });
            if (!running_) 
            {
                break;
            }
            if (sending_ || !conn_ || q_.empty())
            {
                continue;
            }

            sending_ = true;
            auto& frame = q_.front();

            // lk.unlock();
            bool queued = conn_->sendMessage(frame);
            // lk.lock();

            if (queued)
            {
                q_.pop_front();
            }
            else
            {
                sending_ = false;
            }
        }
    });
}



SceneContainer::SceneContainer(aergo::module::BaseModule* base_module, uint8_t frame_sleep_millis)
: base_module_(base_module), frame_sleep_millis_(frame_sleep_millis)
{
    setId("scene-container");
    setStyleClass("scene-container");

    socket_ = std::make_unique<SceneSocket>();

    auto *app = Wt::WApplication::instance();
    app->doJavaScript("window.CounterWS_URL = " + Wt::WString(socket_->url()).jsStringLiteral() + ";");
    app->require("/static/scene_frontend.js");

    cmd_buf_.clear();

    update_thread_ = std::thread(&SceneContainer::updateWorker, this);

    // TODO request all modules to register shapes
    // TODO modules will call announce() at startup (without registering), SceneContainer will 
    // request registration on modules that it doesn't yet know
    // TODO if scene container receives update from a module that it doesn't know, it requests registration
    // TODO registration will include the current scene state for that module
}



SceneContainer::~SceneContainer()
{
    running_ = false;
    if (update_thread_.joinable())
        update_thread_.join();
}



void SceneContainer::updateWorker()
{
    while (running_)
    {
        {
            std::lock_guard<std::mutex> lock(cmd_buf_mtx_);
            if (!cmd_buf_.isEmpty())
            {
                base_module_->log(aergo::module::logging::LogType::INFO, "SceneSocket::sendCommandBuffer(): sending command buffer:  " + 
                    std::to_string(cmd_buf_.pending_registrations_.size()) + " registrations, " +
                    std::to_string(cmd_buf_.objects_.size()) + " object commands, " +
                    std::to_string(cmd_buf_.trajectories_.size()) + " trajectory commands, " +
                    (cmd_buf_.grid_commanded_ ? (cmd_buf_.grid_enabled_ ? "enabling" : "disabling") : "no grid command")
                );

                size_t queued = socket_->sendCommandBuffer(cmd_buf_);
                if (queued == 0)
                {
                    base_module_->log(aergo::module::logging::LogType::ERROR, "SceneSocket::sendCommandBuffer() failed (invalid commands)");
                }
                else if (queued > 1)
                {
                    base_module_->log(aergo::module::logging::LogType::WARNING, "WebSocket send queue has " + std::to_string(queued) + " messages queued (not sending fast enough?)");
                }
                cmd_buf_.clear();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(frame_sleep_millis_));
    }
}



void SceneContainer::enableGrid(bool enable)
{
    std::lock_guard<std::mutex> lock(cmd_buf_mtx_);

    cmd_buf_.grid_commanded_ = true;
    cmd_buf_.grid_enabled_ = enable;
}



ResourceId SceneContainer::createObjectDescription(const ComplexShape& shape)
{
    std::lock_guard<std::mutex> lock(cmd_buf_mtx_);

    ResourceId rid { next_resource_id_++ };
    cmd_buf_.pending_registrations_.emplace_back(rid, shape);

    registered_resources_.emplace(rid, shape);

    return rid;
}



bool SceneContainer::addObject(ResourceId resource_id, const Pose& pose, ObjectId& out_id)
{
    if (registered_resources_.find(resource_id) == registered_resources_.end())
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "SceneContainer::addObject(): resource_id " + std::to_string(resource_id.id) + " not registered");
        return false;
    }

    std::lock_guard<std::mutex> lock(cmd_buf_mtx_);


    ObjectId oid { next_object_id_++ };
    CommandBuffer::ObjectParameters params {
        .action = CommandBuffer::Action::ADD,
        .resource_id = resource_id,
        .pose = pose
    };
    cmd_buf_.objects_[oid] = params;

    existing_objects_[oid] = std::make_tuple(resource_id, pose);

    return true;
}



bool SceneContainer::updateObject(ObjectId object_id, const Pose& pose)
{
    auto it = existing_objects_.find(object_id);
    if (it == existing_objects_.end())
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "SceneContainer::updateObject(): object_id " + std::to_string(object_id.id) + " not found");
        return false;
    }
    it->second = std::make_tuple(std::get<0>(it->second), pose);

    std::lock_guard<std::mutex> lock(cmd_buf_mtx_);

    if (cmd_buf_.objects_.find(object_id) != cmd_buf_.objects_.end())
    {
        // already in command buffer (as ADD, UPDATE or REMOVE), just update the pose (REMOVE ignores pose)
        cmd_buf_.objects_[object_id].pose = pose;
        return true;
    }
    else
    {
        // not yet in command buffer, add an update command
        CommandBuffer::ObjectParameters params {
            .action = CommandBuffer::Action::UPDATE,
            .pose = pose
        };
        cmd_buf_.objects_[object_id] = params;
        return true;
    }
}



bool SceneContainer::removeObject(ObjectId object_id)
{
    auto it = existing_objects_.find(object_id);
    if (it == existing_objects_.end())
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "SceneContainer::removeObject(): object_id " + std::to_string(object_id.id) + " not found");
        return false;
    }
    existing_objects_.erase(it);

    std::lock_guard<std::mutex> lock(cmd_buf_mtx_);

    if (cmd_buf_.objects_.find(object_id) != cmd_buf_.objects_.end())
    {
        // already in command buffer (as ADD, UPDATE or REMOVE)
        if (cmd_buf_.objects_[object_id].action == CommandBuffer::Action::ADD)
        {
            // was an ADD, just remove the command
            cmd_buf_.objects_.erase(object_id);
        }
        else
        {
            // was an UPDATE or REMOVE, change to REMOVE
            cmd_buf_.objects_[object_id].action = CommandBuffer::Action::REMOVE;
        }

        return true;
    }
    else
    {
        // not yet in command buffer, add a remove command
        CommandBuffer::ObjectParameters params {
            .action = CommandBuffer::Action::REMOVE
        };
        cmd_buf_.objects_[object_id] = params;
        
        return true;
    }
}



bool SceneContainer::addTrajectory(const std::vector<Vec3>& pts, bool dashed, ObjectId& out_id)
{
    if (pts.size() < 2)
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "SceneContainer::addTrajectory(): need at least 2 points");
        return false;
    }

    std::lock_guard<std::mutex> lock(cmd_buf_mtx_);

    ObjectId oid { next_object_id_++ };
    CommandBuffer::TrajectoryParameters params {
        .action = CommandBuffer::Action::ADD,
        .dashed = dashed,
        .points = pts,
        .remove_from_head = 0
    };
    cmd_buf_.trajectories_[oid] = params;

    existing_trajectories_[oid] = std::make_tuple(dashed, pts);

    out_id = oid;
    return true;
}



bool SceneContainer::updateTrajectory(ObjectId trajectory_id, const std::vector<Vec3>& add_pts, uint32_t remove_from_head)
{
    auto it = existing_trajectories_.find(trajectory_id);
    if (it == existing_trajectories_.end())
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "SceneContainer::updateTrajectory(): trajectory_id " + std::to_string(trajectory_id.id) + " not found");
        return false;
    }

    // update existing points
    auto& [dashed, pts] = it->second;
    if (remove_from_head > pts.size())
        remove_from_head = pts.size();
    if (pts.size() - remove_from_head + add_pts.size() < 2)
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "SceneContainer::updateTrajectory(): need at least 2 points after update");
        return false; // need at least 2 points
    }

    pts.erase(pts.begin(), pts.begin() + remove_from_head);
    pts.insert(pts.end(), add_pts.begin(), add_pts.end());



    std::lock_guard<std::mutex> lock(cmd_buf_mtx_);

    if (cmd_buf_.trajectories_.find(trajectory_id) != cmd_buf_.trajectories_.end())
    {
        // already in command buffer (as ADD, UPDATE or REMOVE), just update the points (REMOVE ignores points)
        if (cmd_buf_.trajectories_[trajectory_id].action == CommandBuffer::Action::ADD)
        {
            // was an ADD, just update the points
            auto& buf_pts = cmd_buf_.trajectories_[trajectory_id].points;
            if (remove_from_head > buf_pts.size())
                remove_from_head = buf_pts.size();
            buf_pts.erase(buf_pts.begin(), buf_pts.begin() + remove_from_head);
            buf_pts.insert(buf_pts.end(), add_pts.begin(), add_pts.end());
        }
        else if (cmd_buf_.trajectories_[trajectory_id].action == CommandBuffer::Action::UPDATE)
        {
            // was an UPDATE, add new points and add up the remove_from_head
            cmd_buf_.trajectories_[trajectory_id].remove_from_head += remove_from_head;
            auto& buf_pts = cmd_buf_.trajectories_[trajectory_id].points;
            buf_pts.insert(buf_pts.end(), add_pts.begin(), add_pts.end());
        }

        return true;
    }
    else
    {
        // not yet in command buffer, add an update command
        CommandBuffer::TrajectoryParameters params {
            .action = CommandBuffer::Action::UPDATE,
            .points = add_pts,
            .remove_from_head = remove_from_head
        };
        cmd_buf_.trajectories_[trajectory_id] = params;
        return true;
    }
}



bool SceneContainer::removeTrajectory(ObjectId trajectory_id)
{
    auto it = existing_trajectories_.find(trajectory_id);
    if (it == existing_trajectories_.end())
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "SceneContainer::removeTrajectory(): trajectory_id " + std::to_string(trajectory_id.id) + " not found");
        return false;
    }
    existing_trajectories_.erase(it);



    std::lock_guard<std::mutex> lock(cmd_buf_mtx_);

    if (cmd_buf_.trajectories_.find(trajectory_id) != cmd_buf_.trajectories_.end())
    {
        // already in command buffer (as ADD, UPDATE or REMOVE)
        if (cmd_buf_.trajectories_[trajectory_id].action == CommandBuffer::Action::ADD)
        {
            // was an ADD, just remove the command
            cmd_buf_.trajectories_.erase(trajectory_id);
        }
        else
        {
            // was an UPDATE or REMOVE, change to REMOVE
            cmd_buf_.trajectories_[trajectory_id].action = CommandBuffer::Action::REMOVE;
        }

        return true;
    }
    else
    {
        // not yet in command buffer, add a remove command
        CommandBuffer::TrajectoryParameters params {
            .action = CommandBuffer::Action::REMOVE
        };
        cmd_buf_.trajectories_[trajectory_id] = params;
        
        return true;
    }
}