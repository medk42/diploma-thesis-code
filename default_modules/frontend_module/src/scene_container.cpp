#include "webapp/ui/helper/scene_container.h"

#include "module_helpers/visualization_3d_interface/serialization_helper.h"

#include <Wt/WServer.h>

#undef ERROR // Gotta love Windows

#include <sstream>
#include <iomanip>
#include <iostream>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;
using namespace aergo::module::helpers::visualization_3d_interface;



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
//                u8 r, g, b, a        // color
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



SceneSocketConnection::SceneSocketConnection(Wt::WWebSocketResource* resource, Wt::AsioWrapper::asio::io_service& ioService, aergo::module::BaseModule* base_module)
: Wt::WWebSocketConnection(resource, ioService), base_module_(base_module)
{
    base_module_->log(aergo::module::logging::LogType::INFO, "SceneSocketConnection created");
}


SceneSocketConnection::~SceneSocketConnection()
{
    base_module_->log(aergo::module::logging::LogType::INFO, "SceneSocketConnection destroyed");
}


void SceneSocketConnection::handleMessage(const std::string& text)
{
    base_module_->log(aergo::module::logging::LogType::INFO, "SceneSocketConnection received text message: " + text);
    message_received_signal_.emit(text);
}


void SceneSocketConnection::handleMessage(const std::vector<char>& data)
{
    // For now, we do not handle binary messages
    base_module_->log(aergo::module::logging::LogType::WARNING, "SceneSocketConnection received unexpected binary message of size " + std::to_string(data.size()));
}


SceneSocket::SceneSocket(aergo::module::BaseModule* base_module)
: base_module_(base_module)
{
    setTakesUpdateLock(false);
}



SceneSocket::~SceneSocket()
{
    shutdown();
}



bool SceneSocket::sendCommandBuffer(const CommandBuffer& cmd_buf)
{
    std::lock_guard<std::mutex> lk(m_);
    if (!can_send_ || conn_ == nullptr)
    {
        return false; // not connected or not ready to send
    }

    std::vector<char> command_frame;

    uint32_t magic = 0x314E4353u; // 'SCN1' LE
    serialization::ser::push<uint32_t>(command_frame, magic);  // [u32 magic 'SCN1']
    uint32_t seq = ++seq_;
    serialization::ser::push<uint32_t>(command_frame, seq);    // [u32 seq]
    serialization::ser::push<uint8_t>(command_frame, cmd_buf.grid_commanded_ ? 1 : 0); // [u8 grid_commanded]
    serialization::ser::push<uint8_t>(command_frame, cmd_buf.grid_enabled_ ? 1 : 0);   // [u8 grid_enabled]
    
    if (!serialization::pushPendingRegistration(command_frame, cmd_buf.pending_registrations_))
    {
        return 0; // invalid registration
    }
    serialization::pushObjectCommands(command_frame, cmd_buf.objects_);
    serialization::pushTrajectoryCommands(command_frame, cmd_buf.trajectories_);

    can_send_ = false; // will be set to true again on done callback after message is sent
    conn_->sendMessage(command_frame);

    return true;
}



std::unique_ptr<Wt::WWebSocketConnection> SceneSocket::handleConnect(const Wt::Http::Request &req)
{
    auto c = std::make_unique<SceneSocketConnection>(this, Wt::WServer::instance()->ioService(), base_module_);

    std::lock_guard<std::mutex> lk(m_);

    if (!before_first_connection_)
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "SceneSocket: Additional WebSocket connection attempt. Only one connection is supported.");
        return std::move(c);
    }
    before_first_connection_ = false;

    conn_ = c.get();
    can_send_ = false; // will be set to true after we receive a message from the client

    c->done().connect([this](const Wt::AsioWrapper::error_code& ec) {
        std::lock_guard<std::mutex> lk(m_);

        if (!ec)
        {
            can_send_ = true;
        }
        else
        {
            conn_ = nullptr;
            can_send_ = false;
            base_module_->log(aergo::module::logging::LogType::WARNING, "SceneSocket: WebSocket connection closed: " + ec.message());
        }
    });

    c->closed().connect([this](Wt::AsioWrapper::error_code ec, const std::string& reason) {
        std::lock_guard<std::mutex> lk(m_);
        conn_ = nullptr;
        can_send_ = false;
        if (!ec)
        {
            base_module_->log(aergo::module::logging::LogType::INFO, "SceneSocket: WebSocket connection closed" + (reason.empty() ? std::string() : (": " + reason)));
        }
        else
        {
            base_module_->log(aergo::module::logging::LogType::WARNING, "SceneSocket: WebSocket connection closed with error: " + ec.message() + (reason.empty() ? std::string() : (", reason: " + reason)));
        }
    });
    
    c->messageReceivedSignal().connect([this](const std::string& msg) {
        std::lock_guard<std::mutex> lk(m_);
        if (!can_send_)
        {
            can_send_ = true;
            base_module_->log(aergo::module::logging::LogType::INFO, "SceneSocket: WebSocket connection established and ready to send messages.");
        }
    });
    
    return c;
}



SceneContainer::SceneContainer(aergo::module::BaseModule* base_module, uint8_t frame_sleep_millis)
: base_module_(base_module), frame_sleep_millis_(frame_sleep_millis)
{
    setId("scene-container");
    setStyleClass("scene-container");

    socket_ = std::make_unique<SceneSocket>(base_module_);

    auto *app = Wt::WApplication::instance();
    app->doJavaScript("window.sceneSocketURL = " + Wt::WString(socket_->url()).jsStringLiteral() + ";"); // make URL available to JS before loading the script (if script is not yet loaded)
    app->require("/static/scene_frontend.js");
    
    cmd_coalescer_.clearBuffer();

    update_thread_ = std::thread(&SceneContainer::updateWorker, this);
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
            std::lock_guard<std::mutex> lock(cmd_coalescer_mtx_);
            auto& cmd_buf = cmd_coalescer_.getBuffer();
            if (!cmd_buf.isEmpty())
            {
                // base_module_->log(aergo::module::logging::LogType::INFO, "SceneSocket::sendCommandBuffer(): sending command buffer:  " + 
                //     std::to_string(cmd_buf.pending_registrations_.size()) + " registrations, " +
                //     std::to_string(cmd_buf.objects_.size()) + " object commands, " +
                //     std::to_string(cmd_buf.trajectories_.size()) + " trajectory commands, " +
                //     (cmd_buf.grid_commanded_ ? (cmd_buf.grid_enabled_ ? "enabling" : "disabling") : "no grid command")
                // );

                if (socket_->sendCommandBuffer(cmd_buf))
                {
                    // sent successfully, clear the coalescer buffer
                    cmd_coalescer_.clearBuffer();
                }
                else
                {
                    // could not send (not connected), try again later
                    // base_module_->log(aergo::module::logging::LogType::WARNING, "SceneSocket::sendCommandBuffer(): could not send command buffer, not connected");
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(frame_sleep_millis_));
    }
}



void SceneContainer::enableGrid(bool enable)
{
    std::lock_guard<std::mutex> lock(cmd_coalescer_mtx_);
    cmd_coalescer_.enableGrid(enable);
}



ResourceId SceneContainer::createObjectDescription(const ComplexShape& shape)
{
    std::lock_guard<std::mutex> lock(cmd_coalescer_mtx_);

    ResourceId rid { next_resource_id_++ };

    cmd_coalescer_.createObjectDescription(rid, shape);
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

    std::lock_guard<std::mutex> lock(cmd_coalescer_mtx_);


    ObjectId oid { next_object_id_++ };
    cmd_coalescer_.addObject(resource_id, pose, oid);
    existing_objects_[oid] = std::make_tuple(resource_id, pose);
    out_id = oid;

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

    std::lock_guard<std::mutex> lock(cmd_coalescer_mtx_);
    cmd_coalescer_.updateObject(object_id, pose);

    return true;
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

    std::lock_guard<std::mutex> lock(cmd_coalescer_mtx_);
    cmd_coalescer_.removeObject(object_id);

    return true;
}



bool SceneContainer::addTrajectory(const std::vector<Vec3>& pts, Color color, bool dashed, ObjectId& out_id)
{
    if (pts.size() < 2)
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "SceneContainer::addTrajectory(): need at least 2 points");
        return false;
    }

    std::lock_guard<std::mutex> lock(cmd_coalescer_mtx_);

    ObjectId oid { next_object_id_++ };
    cmd_coalescer_.addTrajectory(pts, color, dashed, oid);
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


    std::lock_guard<std::mutex> lock(cmd_coalescer_mtx_);
    cmd_coalescer_.updateTrajectory(trajectory_id, add_pts, remove_from_head);

    return true;
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


    std::lock_guard<std::mutex> lock(cmd_coalescer_mtx_);
    cmd_coalescer_.removeTrajectory(trajectory_id);

    return true;
}