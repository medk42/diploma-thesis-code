#pragma once

#include "structs.h"

#include "module_common/module_interface_.h"
#include "module_helpers/usecase_wrapper/message_types.h"
#include "module_helpers/usecase_wrapper/helper_types.h"

#include <functional>
#include <vector>
#include <mutex>
#include <thread>
#include <span>
#include <cstdint>
#include <optional>

#undef ERROR // yay for windows.h

namespace aergo::module::helpers::usecase_tree
{
    namespace uw = aergo::module::helpers::usecase_wrapper;

    class ProgramInstance
    {
    public:
        enum class ProgramState { NOT_STARTED, RUNNING, PAUSED, STOPPED, RESUMING, PAUSING, STOPPING };
        enum class ProgramStopReason { NONE, COMPLETED, STOP_REQUESTED, ERROR, EXCEPTION, INTERNAL_ERROR };
        struct ProgramResult
        {
            ProgramStopReason program_stop_reason_ { ProgramStopReason::NONE };
            std::optional<uw::helper::ErrorInfo> program_last_error_info_{ std::nullopt };
            bool failed_to_remove_programs_{false};
        };

        using req_func_t = std::function<bool(aergo::module::ChannelIdentifier, uw::message_types::Request, std::optional<std::span<const std::byte>>, uw::message_types::Response&, aergo::module::message::SharedDataBlob*)>;
        
        ProgramInstance(bool simulate, std::vector<structs::ExistingCommand> commands, req_func_t send_request_function);
        ~ProgramInstance() noexcept;

        bool valid() const { return valid_; }

        void stop(); // stop as soon as possible (latest when current command finishes)
        void pause(); // pause after current command finishes
        void resume(); // resume from paused state

        ProgramState state() const;
        std::optional<ProgramResult> getResult() const; // returns result only when in STOPPED state, std::nullopt otherwise

    private:
        enum class CurrentRequest { NONE, STOP, PAUSE, RESUME };

        void programExecutionLoop();
        void stopWithReason(ProgramStopReason reason, std::optional<uw::helper::ErrorInfo> error_info = std::nullopt);
        bool startCommand(const std::string& command_data_json, const ChannelIdentifier usecase_channel, uint64_t& out_task_id);
        bool updateStatus(const uint64_t task_id, const ChannelIdentifier usecase_channel, uw::message_types::ProgramStatus& out_status, uw::helper::ErrorInfo& out_error_info);
        bool removeCommand(const uint64_t task_id, const ChannelIdentifier usecase_channel);
        bool handleRequest(const uint64_t task_id, const ChannelIdentifier usecase_channel);

        void updateCurrentState(uw::message_types::ProgramStatus status);
        void handleStateAfterFinishingCommand(bool& out_stop_requested);

        bool valid_{false};

        req_func_t send_request_function_;
        bool simulate_run_{false};
        std::vector<structs::ExistingCommand> running_program_command_list_;
        std::vector<ChannelIdentifier> running_program_usecase_channels_;

        mutable std::mutex mutex_;

        CurrentRequest current_request_{ CurrentRequest::NONE };

        std::thread program_thread_;
        ProgramState program_state_{ ProgramState::NOT_STARTED };

        // readable only when STOPPED, program finished executing and we can check the reason
        ProgramResult result_;
    };
}