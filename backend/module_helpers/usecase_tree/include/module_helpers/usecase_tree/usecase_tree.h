#pragma once

#include "structs.h"
#include "program_instance.h"

#include <map>
#include <vector>
#include <functional>
#include <optional>
#include <compare>
#include <mutex>
#include <string>
#include <set>
#include <atomic>
#include <memory>

#include "module_common/base_module.h"
#include "module_common/module_interface_.h"
#include "module_helpers/parameter_description/parameter_description.h"
#include "module_helpers/usecase_wrapper/helper_types.h"
#include "module_helpers/usecase_wrapper/message_types.h"

namespace aergo::module::helpers::usecase_tree
{
    namespace p_desc = aergo::module::helpers::parameter_description;
    namespace uw = aergo::module::helpers::usecase_wrapper;


    class UsecaseTree
    {
    public:
        UsecaseTree(aergo::module::BaseModule* base_module);
        ~UsecaseTree() noexcept = default;

        bool valid() const { return valid_; }
        uint32_t getUsecaseRequestChannelId() const { return usecase_request_channel_id_; }
        void handleResponse(ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& message);

        /// @brief Update the list of available usecases from the connected modules
        /// @return true on success, false on failure (failed to read available usecases, etc.)
        bool updateAvailableUsecases(std::optional<std::function<void(const std::map<std::string, structs::AvailableUsecase>&)>> on_finish = std::nullopt);
        const std::map<std::string, structs::AvailableUsecase>& getAvailableUsecases() const; // there is no protection that the map won't change while being accessed, ensure external synchronization if needed

        bool appendCommand(const std::string& param_identifier); // appends command at the end, returns true if successful; command is specified by usecase parameter identifier
        bool insertCommand(size_t list_index, structs::ExistingCommand command); // inserts command at specified index, returns true if successful
        bool removeCommand(size_t list_index); // removes command at specified index, returns true if successful
        void clearCommands(); // clears all existing commands

        size_t size() const { return existing_commands_list_.size(); } // number of existing commands in the usecase tree, there is no protection that the list won't change while being accessed, ensure external synchronization if needed
        structs::ExistingCommand& operator[](size_t list_index) { return existing_commands_list_.at(list_index); } // get existing command at specified index, throws out_of_range if index invalid; there is no protection that the list won't change while being accessed, ensure external synchronization if needed

        // send request to generate command data JSON for command at specified index, returns true if request was sent, false if wrong list_index or parameters invalid
        bool generateCommandDataJson(size_t list_index, std::optional<std::function<void(bool, uw::helper::ErrorInfo)>> on_finish = std::nullopt);
        
        bool readCustomValue(size_t list_index, size_t param_index, size_t list_index_in_param, std::atomic<bool>& cancel_read, std::optional<std::function<void(bool)>> on_value_ready_callback = std::nullopt);

        /// @brief starts all commands in the usecase tree, returns true if start request was accepted (all commands valid), 
        /// false otherwise (invalid commands, no commands, already running, etc.).
        /// Makes a copy of the current commands, so commands can be modified after calling start().
        bool start(bool simulate);
        void stop(); // stop as soon as possible (latest when current command finishes)
        void pause(); // pause after current command finishes
        void resume(); // resume from paused state
        std::optional<ProgramInstance::ProgramState> getProgramState() const; // get current program state, std::nullopt if no program is running
        std::optional<ProgramInstance::ProgramResult> getProgramResult() const; // returns result only when in STOPPED state, std::nullopt otherwise

        /// @brief serialize usecase tree to JSON string (serializes existing commands WITHOUT command_ids)
        /// @return JSON string if successful, std::nullopt otherwise
        std::optional<std::string> toJson() const;

        /// @brief deserialize usecase tree from JSON string, returns true if successful, false otherwise
        /// @param json_str JSON string to deserialize from
        /// @param out_missing_usecase_identifier if failure occurs due to missing usecase, contains the identifier of the missing usecase
        bool fromJson(const std::string& json_str, std::string& out_missing_usecase_identifier); 


    private:
        void processCustomValueResponse(uint64_t command_id, uint64_t task_id, size_t param_index, size_t list_index_in_param, std::atomic<bool>& cancel_read, std::optional<std::function<void(bool)>> on_value_ready_callback, ChannelIdentifier source_channel, const aergo::module::message::MessageHeader& message);

        /// @brief Send request and wait for response synchronously (blocking).
        /// @param target_channel 
        /// @param request request to send
        /// @param send_data optional data to send with the request
        /// @param out_response response received
        /// @param out_response_blob blob received in response (if any), use nullptr if no blob needed
        /// @return true if response received successfully, false otherwise (failed response, mismatched response size, etc.)
        bool sendRequestSynchronized(ChannelIdentifier target_channel, uw::message_types::Request request, std::optional<std::span<const std::byte>> send_data, uw::message_types::Response& out_response, aergo::module::message::SharedDataBlob* out_response_blob);

        aergo::module::BaseModule* base_module_ref_;

        aergo::module::BaseModule::AllocatorPtr dynamic_allocator_;
        uint32_t usecase_request_channel_id_;
        bool valid_;
        mutable std::mutex mutex_;

        std::map<uint64_t, std::function<void(ChannelIdentifier, const aergo::module::message::MessageHeader&)>> response_handlers_;

        // If we do not receive an update, we would currently wait forever. Keep in mind for future improvements.
        std::set<uint64_t> pending_modules_for_update_; // modules from which we are waiting for available usecases during updateAvailableUsecases()
        std::optional<std::function<void(const std::map<std::string, structs::AvailableUsecase>&)>> pending_update_on_finish_; // callback to call when pending_modules_for_update_ is empty

        std::map<std::string, structs::AvailableUsecase> available_usecases_map_;
        std::vector<structs::ExistingCommand> existing_commands_list_;
        std::map<uint64_t, size_t> command_id_to_index_map_;

        std::unique_ptr<ProgramInstance> running_program_instance_;

        uint64_t next_command_id_{1};
    };
}