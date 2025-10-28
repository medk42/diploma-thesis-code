#pragma once

#include "module_common/base_module.h"
#include "module_helpers/usecase_wrapper/usecase_module_interface.h"
#include "module_helpers/usecase_wrapper/serialization_helper.h"

#include <expected>
#include <optional>
#include <mutex>
#include <map>
#include <thread>
#include <exception>
#include <condition_variable>

namespace aergo::module::helpers::base_usecase
{
    namespace message_types = aergo::module::helpers::usecase_wrapper::message_types;
    namespace usecase_wrapper_helper = aergo::module::helpers::usecase_wrapper::helper;
    namespace uw = aergo::module::helpers::usecase_wrapper;
    namespace p_desc = aergo::module::helpers::parameter_description;

    /// @brief Base class for usecase modules, provides common functionality.
    /// It implements the IUsecaseModule interface, providing functionality to
    /// run commands based on parameters and handle program control requests.
    /// Usecases will typically only need the validateProgram, startProgram function, 
    /// calling requests and remaining functions from the IUsecaseModule interface.
    /// This base class provides basic implementation of IModule interface, ignoring messages
    /// and requests, and providing default implementations for save/load and threading.
    /// It also i
    class BaseUsecase : public aergo::module::BaseModule, public aergo::module::helpers::usecase_wrapper::IUsecaseModule
    {
    public:
        /// @param supports_multi_program If true, usecase supports running multiple programs simultaneously.
        /// @param supports_pause If true, usecase supports pausing and resuming execution.
        /// @param supports_stop If true, usecase supports stopping execution.
        BaseUsecase(
            const char* data_path, 
            ICore* core, 
            InputChannelMapInfo channel_map_info, 
            const logging::ILogger* logger, 
            uint64_t module_id, 
            const ModuleInfo* module_info,
            bool supports_multi_program,
            bool supports_pause,
            bool supports_stop
        );

        ~BaseUsecase() noexcept override;

        virtual uint64_t sendRequestFromUsecase(uint32_t request_consumer_id) override { return 0; } // implement for usecases that do not need to send requests

        virtual void processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override {} // ignore messages
        virtual aergo::module::ResponseData processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override { return aergo::module::ResponseData::createFailure(); } // ignore requests
        virtual void processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override {} // ignore responses
        virtual aergo::module::IModule::IngressDecision onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept override { return aergo::module::IModule::IngressDecision::DROP; } // UsecaseWrapper will accept required messages, drop all here
        virtual bool valid() noexcept override { return true; } // always valid
        virtual void* query_capability(const std::type_info& id) noexcept override;
        virtual bool threadStart(uint32_t timeout_ms) noexcept override { return true; } // threads are started only on programStart() call
        virtual bool threadStop(uint32_t timeout_ms) noexcept override;
        virtual ISerializableModule::SaveData save() noexcept override final; // usecases are stateless, no need to save anything
        virtual bool load(ISerializableModule::SaveData data) noexcept override final { return true; }; // usecases are stateless, no need to save anything

        virtual message_types::Result programStart(const nlohmann::json& command_json, bool simulated, uint64_t& out_task_id, usecase_wrapper_helper::ErrorInfo& out_error_info) override;
        virtual message_types::Result programPause(uint64_t task_id) override;
        virtual message_types::Result programResume(uint64_t task_id) override;
        virtual message_types::Result programStatus(uint64_t task_id, message_types::ProgramStatus& out_status, usecase_wrapper_helper::ErrorInfo& out_error_info) override;
        virtual message_types::Result programStop(uint64_t task_id) override;
        virtual message_types::Result programRemove(uint64_t task_id) override;

    protected:
        /// @brief If program supports pause/resume and/or stop, it should call this function periodically to handle control requests.
        /// This function will block if a pause is requested, until resume is called.
        /// It will also stop execution if a stop is requested, by throwing an exception (caught in startProgram(...) caller).
        /// @param allow_pause 
        /// @param allow_stop 
        void handleControlRequests(bool allow_pause, bool allow_stop);

        /// @brief Validate if the provided parameters JSON is valid and can be started.
        /// @return {} if parameters are valid, std::unexpected(ErrorInfo) otherwise
        virtual std::expected<void, usecase_wrapper_helper::ErrorInfo> validateParameters(const nlohmann::json& command_json) = 0;

        /// @brief Run the usecase with the provided parameters JSON.
        /// This function is called after validateParameters() has returned successfully.
        /// This function is started in a separate thread and should contain the full command including all blocking operations.
        /// Call handleControlRequests(...) periodically to handle pause/stop requests if supported.
        /// Exceptions thrown from this function will be caught in the caller and treated as command failure (but prefer using std::expected return type).
        /// @param command_json Parameters for the command in JSON format.
        /// @return {} if command completed successfully, std::unexpected(ErrorInfo) otherwise
        virtual std::expected<void, usecase_wrapper_helper::ErrorInfo> runProgram(const nlohmann::json& command_json, bool simulated) = 0;

        template<typename T>
        static inline bool readMessageDataAs(const p_desc::ParameterValue& param_value, T& out_data, std::vector<std::vector<uint8_t>>* out_blobs = nullptr) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>, "readMessageDataAs requires trivially copyable type");

            if (!std::holds_alternative<std::vector<uint8_t>>(param_value))
            {
                return false;
            }

            const auto& data = std::get<std::vector<uint8_t>>(param_value);
            uw::deserialize::des::BufferReader reader(data.data(), data.size());

            uw::deserialize::MessageData msg_data;
            if (!uw::deserialize::readMessage(reader, msg_data))
            {
                return false;
            }

            aergo::module::message::MessageHeader header = aergo::module::message::MessageHeader::Message(std::span<const uint8_t>(msg_data.data_));
            if (!header.readAs<T>(out_data))
            {
                return false;
            }

            if (out_blobs != nullptr)
            {
                *out_blobs = std::move(msg_data.blobs_);
            }

            return true;
        }

    private:
        struct UsecaseStatus;
        struct StopException;

        void setStatus(message_types::ProgramStatus status, std::optional<usecase_wrapper_helper::ErrorInfo> error_info = std::nullopt);
        UsecaseStatus* getStatusForIdUnsafe(uint64_t task_id); // not thread-safe, caller must hold mutex_!

        const bool supports_multi_program_;
        const bool supports_pause_;
        const bool supports_stop_;

        std::mutex mutex_;
        std::condition_variable control_cv_;

        uint64_t next_task_id_ = 1;
        std::map<uint64_t, std::thread::id> task_id_to_thread_id_;
        std::map<std::thread::id, UsecaseStatus> program_statuses_;
    };

    struct BaseUsecase::UsecaseStatus
    {
        std::thread thread_;

        bool pause_requested_ = false;
        bool stop_requested_ = false;

        message_types::ProgramStatus status_;
        usecase_wrapper_helper::ErrorInfo error_info_ = usecase_wrapper_helper::ErrorInfo::WithoutDetails();
    };

    struct BaseUsecase::StopException : public std::exception
    {
        const char* what() const noexcept override
        {
            return "Program stopped by request.";
        }
    };
}