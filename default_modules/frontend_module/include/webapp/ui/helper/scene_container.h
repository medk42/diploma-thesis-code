#pragma once

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

#include <iostream>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class SceneSocket : public Wt::WWebSocketResource
    {
    public:
        SceneSocket() { setTakesUpdateLock(false); startWorkers(); }
        ~SceneSocket() override
        { 
            running_ = false;
            cv_.notify_one();
            if (worker_.joinable())
                worker_.join();
            if (send_worker_.joinable())
                send_worker_.join();
            shutdown();
        }

    protected:
        std::unique_ptr<Wt::WWebSocketConnection> handleConnect(const Wt::Http::Request &req) override
        {
            auto c = std::make_unique<Wt::WWebSocketConnection>(this, Wt::WServer::instance()->ioService());
            c->setTakesUpdateLock(false);

            {
                std::lock_guard<std::mutex> lk(m_);
                conn_ = c.get();
            }
            
            c->done().connect([this](const Wt::AsioWrapper::error_code& ec) {
                std::lock_guard<std::mutex> lk(m_);
                sending_ = false;
                cv_.notify_one();
            });
            
            return c;
        }

    private:

        int64_t _micros()
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        }

        void startWorkers()
        {
            if (running_)
                return;
            running_ = true;

            worker_ = std::thread([this]() {
                while (running_) {
                    static int count = 0;
                    std::string s = std::to_string(count++);
                    std::vector<char> frame(s.begin(), s.end());
                    size_t q_size = sendNext(frame);
                    if (q_size > 1)
                    {
                        std::cerr << "Warning: CounterSocket queue size " << q_size << " growing\n";
                    }

                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                } 
            });

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

        size_t sendNext(std::vector<char>& frame)
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.emplace_back(std::move(frame));
            cv_.notify_one();
            return q_.size(); // if >1, messages are not being sent fast enough
        }

        std::mutex m_;
        std::condition_variable cv_;
        Wt::WWebSocketConnection* conn_{nullptr};
        std::thread worker_;
        std::thread send_worker_;
        std::atomic<bool> running_{false};
        std::deque<std::vector<char>> q_;
        bool sending_{false};
    };

    struct Vec3  { float x,y,z; };
    struct Quat  { float x,y,z,w; };
    struct Pose  { Vec3 t; Quat q; };         // translation + orientation (quat)
    inline Quat  IdentityQ() { return {0,0,0,1}; }
    inline Vec3  One3()      { return {1,1,1}; }

    enum class PrimType : uint8_t { Box=1, Sphere=2, Cylinder=3, Cone=4 };
    // For Box: size = {sx,sy,sz}
    // Sphere:  size = {r, -, -}
    // Cylinder:size = {rTop, rBot, h}
    // Cone:    size = {r, 0, h}

    struct ShapeDesc {
        PrimType type;
        Vec3     size;            // see mapping above
        uint32_t rgba = 0x6699FFff; // 0xRRGGBBAA (A default 0xFF)
    };

    class SceneContainer : public Wt::WContainerWidget
    {
    public:
        SceneContainer();

         // --------- External API (what other modules call) ----------
        void enableGrid(bool on);

        // Static resource registry
        uint32_t createObjectDescription(const ShapeDesc& s);              // returns resource_id
        // (reserve for future) uint32_t createMeshDescription(const MeshMeshDesc& m);

        // Instances (object_id)
        uint32_t addObject(uint32_t resource_id, const Pose& pose, const Vec3& scale = One3());
        void     updateObject(uint32_t object_id, const Pose& pose, const Vec3& scale = One3());
        void     removeObject(uint32_t object_id);

        // Trajectories
        uint32_t addTrajectory(const std::vector<Vec3>& pts, bool dashed);
        void     updateTrajectory(uint32_t traj_id, const std::vector<Vec3>& addPts, uint32_t removeFromHead);
        void     removeTrajectory(uint32_t traj_id);

        // Force-flush this frame (otherwise a 60 Hz tick does it)
        void     flush();

    private:
        std::unique_ptr<SceneSocket> socket_;
        std::string containerId_;
        std::atomic<uint32_t> nextId_{1};
    };
}