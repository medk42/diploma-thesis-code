#include "module_helpers/activation_wrapper/async_task.h"

using namespace aergo::module::helpers::activation_wrapper;



AsyncTask::AsyncTask(std::function<bool(std::atomic<bool>&)> func)
: func_(func), cancel_flag_(false), started_(false) {}



AsyncTask::~AsyncTask()
{
    cancel();
    if (thread_.joinable())
    {
        thread_.join();
    }
}



void AsyncTask::start()
{
    if (started_)
    {
        return;
    }

    started_ = true;
    cancel_flag_ = false;
    finished_ = false;

    thread_ = std::thread([this]() {
        completed_ = !func_(cancel_flag_);
        finished_ = true;
    });
}



void AsyncTask::cancel()
{
    if (!started_)
    {
        return;
    }

    cancel_flag_ = true;
}



AsyncTaskState AsyncTask::getState()
{
    if (!started_)
    {
        return AsyncTaskState::NOT_STARTED;
    }
    else if (finished_)
    {
        return completed_ ? AsyncTaskState::COMPLETED : AsyncTaskState::CANCELLED;
    }
    else
    {
        return AsyncTaskState::RUNNING;
    }
}