#pragma once

#include <functional>
#include <atomic>
#include <thread>

namespace aergo::module::helpers::activation_wrapper
{
    enum class AsyncTaskState
    {
        NOT_STARTED,
        RUNNING,
        CANCELLED,
        COMPLETED
    };



    class AsyncTask
    {
    public:
        /// @brief Create async task that can be started and cancelled.
        /// @param func function that takes atomic cancel flag reference, returns true if task completed, false if cancelled
        AsyncTask(std::function<bool(std::atomic<bool>&)> func);
        ~AsyncTask();

        /// @brief Start the task in a new thread.
        /// If task is already running, does nothing.
        void start();

        /// @brief If task is running, set cancel flag. Task should check the flag and stop itself.
        void cancel();

        /// @brief Get current state of the task.
        AsyncTaskState getState();


    private:
        std::function<bool(std::atomic<bool>&)> func_;
        std::atomic<bool> cancel_flag_;
        bool started_;
        std::atomic<bool> finished_;
        std::atomic<bool> completed_;
        std::thread thread_;
    };
}