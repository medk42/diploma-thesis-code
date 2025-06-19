#include "periodic_thread.h"

using namespace std::chrono_literals;
using namespace aergo::module::thread;



PeriodicThread::PeriodicThread(uint32_t thread_sleep_ms)
: thread_sleep_ms_(thread_sleep_ms) {}



bool PeriodicThread::_threadStart(uint32_t timeout_ms)
{
    if (module_thread_ || thread_running_)
    {
        return false;
    }

    thread_running_ = false;
    stop_request_ = false;
    module_thread_ = std::make_unique<std::thread>([this]() {
        _threadInit();
        thread_running_ = true;
        while (!stop_request_)
        {
            _threadCycle();
            if (thread_sleep_ms_ > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(thread_sleep_ms_));
            }
        }
        _threadDeinit();
        thread_running_ = false;
        stop_request_ = false;
    });

    int64_t start_ms = millis();
    while (!thread_running_ && millis() < start_ms + timeout_ms)
    {
        std::this_thread::sleep_for(1ms);
    }

    return thread_running_;
}



bool PeriodicThread::_threadStop(uint32_t timeout_ms)
{
    if (!module_thread_)
    {
        return false;
    }

    if (!thread_running_)
    {
        if (module_thread_->joinable())
        {
            module_thread_->join();
            module_thread_ = nullptr;
            
            return true;
        }
        else
        {
            return false;
        }
    }

    stop_request_ = true;
    int64_t start_ms = millis();
    while (stop_request_ && millis() < start_ms + timeout_ms)
    {
        std::this_thread::sleep_for(1ms);
    }

    if (stop_request_)
    {
        return false;
    }
    else
    {
        if (module_thread_->joinable())
        {
            module_thread_->join();
            module_thread_ = nullptr;
        }
        return true;
    }
}



int64_t PeriodicThread::millis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}