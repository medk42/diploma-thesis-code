#pragma once

#include <functional>
#include <atomic>
#include <thread>
#include <optional>

namespace aergo::module::helpers::async_helpers
{
    enum class AsyncTaskState
    {
        NOT_STARTED,  // task not started yet
        RUNNING,      // task is running
        CANCELLED,    // task finished due to cancellation
        COMPLETED     // task finished normally
    };



    /// @tparam T has to be movable
    template <typename T>
    class AsyncTask
    {
    public:
        /// @brief Create async task that can be started and cancelled.
        /// @param func function that takes const atomic cancel flag reference and atomic cancelled flag reference and returns result of type T.
        /// The function should periodically check the cancel flag and return early if set to true. If the function returns due to cancellation, it should set cancelled flag to true.
        AsyncTask(std::function<T(const std::atomic<bool>&, std::atomic<bool>&)> func);
        ~AsyncTask();

        /// @brief Start the task in a new thread.
        /// If task is already running, does nothing.
        void start();

        /// @brief If task is running, set cancel flag. Task should check the flag and stop itself.
        void cancel();

        /// @brief Get current state of the task.
        AsyncTaskState getState();

        /// @brief Get result of the task if state is COMPLETED or CANCELLED. Otherwise returns empty optional.
        std::optional<T> getResult();

    private:
        bool started_;

        std::function<T(const std::atomic<bool>&, std::atomic<bool>&)> func_;
        std::atomic<bool> cancel_flag_;
        std::atomic<bool> finished_;
        std::atomic<bool> cancelled_;
        std::thread thread_;

        std::optional<T> result_;
    };









    template <typename T>
    AsyncTask<T>::AsyncTask(std::function<T(const std::atomic<bool>&, std::atomic<bool>&)> func)
    : started_(false), func_(func), cancel_flag_(false), finished_(false), cancelled_(false) {}



    template <typename T>
    AsyncTask<T>::~AsyncTask()
    {
        cancel();
        if (thread_.joinable())
        {
            thread_.join();
        }
    }



    template <typename T>
    void AsyncTask<T>::start()
    {
        if (started_)
        {
            return;
        }

        started_ = true;
        cancel_flag_ = false;
        cancelled_ = false;
        finished_ = false;

        thread_ = std::thread([this]() {
            result_ = std::move(func_(cancel_flag_, cancelled_));
            finished_ = true;
        });
    }



    template <typename T>
    void AsyncTask<T>::cancel()
    {
        if (!started_)
        {
            return;
        }

        cancel_flag_ = true;
    }



    template <typename T>
    AsyncTaskState AsyncTask<T>::getState()
    {
        if (!started_)
        {
            return AsyncTaskState::NOT_STARTED;
        }
        else if (finished_)
        {
            return cancelled_ ? AsyncTaskState::CANCELLED : AsyncTaskState::COMPLETED;
        }
        else
        {
            return AsyncTaskState::RUNNING;
        }
    }



    template <typename T>
    std::optional<T> AsyncTask<T>::getResult()
    {
        return result_;
    }
}