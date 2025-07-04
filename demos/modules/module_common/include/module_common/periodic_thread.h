#pragma once

#include <stdint.h>
#include <atomic>
#include <thread>
#include <memory>

namespace aergo::module::thread
{
    class PeriodicThread
    {
    protected:
        PeriodicThread(uint32_t thread_sleep_ms);

        /// @brief Start the background thread.
        /// @param timeout_ms Wait up to "timeout_ms" milliseconds for the thread to start.
        /// @return true if started within timeout_ms. false on fail to start / timeout. Thread may exist if false.
        bool _threadStart(uint32_t timeout_ms);

        /// @brief Stop and join the background thread.
        /// @return true if the thread was running, stopped within "timeout_ms" milliseconds and joined. false otherwise. 
        bool _threadStop(uint32_t timeout_ms);

        /// @brief Function called at the start of the thread (after _thread_start) / initialization.
        virtual void _threadInit() = 0;

        /// @brief Function called periodically while the thread is running / cycle.
        virtual void _threadCycle() = 0;
        
        /// @brief Function called at the end of the thread (after _thread_stop) / deinitialization.
        virtual void _threadDeinit() = 0;


    private:
        int64_t millis();

        std::unique_ptr<std::thread> module_thread_;
        std::atomic_bool thread_running_;
        std::atomic_bool stop_request_;
        uint32_t thread_sleep_ms_;
    };
}