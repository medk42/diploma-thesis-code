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
    class CounterSocket : public Wt::WWebSocketResource
    {
    public:
        CounterSocket() { setTakesUpdateLock(false); startWorkers(); }
        ~CounterSocket() override
        { 
            running_ = false;
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

    class SceneSocket : public Wt::WWebSocketResource
    {
    public:
        SceneSocket();
        ~SceneSocket() override;

        // Queue a binary frame to send to the (single) client.
        void sendFrame(std::vector<char> &&frame);

        // Convenience builders
        void sendAddBox(uint32_t id, float x, float y, float z, float sx, float sy, float sz);

    protected:
        std::unique_ptr<Wt::WWebSocketConnection>
        handleConnect(const Wt::Http::Request &req) override;

    private:
        void sendNextLocked(); // assumes m_ held

        std::mutex m_;
        Wt::WWebSocketConnection *conn_{nullptr};
        bool sending_{false};
        std::deque<std::vector<char>> q_;
        uint32_t seq_{0};
    };

    class SceneContainer : public Wt::WContainerWidget
    {
    public:
        // threeJsPath = local path to your bundled three.min.js (no CDN)
        SceneContainer(int widthPx = 800, int heightPx = 450);

        // API: add a box; returns the ID used (you can supply your own ID scheme later)
        uint32_t addBox(float x, float y, float z, float sx, float sy, float sz);

    private:
        void bootstrapThree(const std::string &wsUrl);

        std::unique_ptr<CounterSocket> counterSocket_;
        std::unique_ptr<SceneSocket> socket_;
        std::string containerId_;
        std::atomic<uint32_t> nextId_{1};
    };
}