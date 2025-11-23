#include "robot_module_kassow/robot_module_kassow.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <span>
#include <thread>

using namespace aergo::default_modules::robot_module_kassow;
using namespace aergo::module;

namespace
{
    constexpr const char* DEFAULT_HOST = "127.0.0.1";
    constexpr uint16_t DEFAULT_PORT = 15050;
    constexpr uint32_t REQUEST_TIMEOUT_MS = 3000;
    constexpr std::chrono::milliseconds ASYNC_POLL_WAIT{200};
    constexpr std::chrono::milliseconds RECONNECT_WAIT{500};

    uint16_t readPortFromEnv(const char* env_name, uint16_t fallback, const logging::ILogger* logger)
    {
        if (!env_name)
        {
            return fallback;
        }
        if (const char* value = std::getenv(env_name))
        {
            try
            {
                int parsed = std::stoi(value);
                if (parsed > 0 && parsed <= 65535)
                {
                    return static_cast<uint16_t>(parsed);
                }
                if (logger)
                {
                    logger->log(logging::LogType::WARNING, "Invalid port in environment variable, using default");
                }
            }
            catch (const std::exception&)
            {
                if (logger)
                {
                    logger->log(logging::LogType::WARNING, "Failed to parse port from environment variable, using default");
                }
            }
        }
        return fallback;
    }
}


RobotModuleKassow::RobotModuleKassow(const char* data_path,
                                     ICore* core,
                                     InputChannelMapInfo channel_map_info,
                                     const logging::ILogger* logger,
                                     uint64_t module_id,
                                     const ModuleInfo* module_info)
    : BaseModule(data_path, core, channel_map_info, logger, module_id, module_info),
      valid_(false),
      response_channel_id_(0),
      status_publish_id_(0),
      finished_publish_id_(0),
      rpc_client_(std::make_unique<aergo::robot::kassow::rpc::RpcClient>(logger)),
      allocator_(createDynamicAllocator()),
      stop_async_(false),
      kassow_host_(DEFAULT_HOST),
      kassow_port_(DEFAULT_PORT)
{
    const char* env_host = std::getenv("KASSOW_ROBOT_HOST");
    if (env_host && std::strlen(env_host) > 0)
    {
        kassow_host_ = env_host;
    }
    kassow_port_ = readPortFromEnv("KASSOW_ROBOT_PORT", DEFAULT_PORT, logger);

    if (!getResponseChannelByName(helpers::robot_interface::robot_interface_response_producer.channel_type_identifier_, response_channel_id_))
    {
        log(logging::LogType::ERROR, "Failed to locate robot interface response channel");
        return;
    }
    if (!getPublishChannelByName(helpers::robot_interface::robot_interface_status_producer.channel_type_identifier_, status_publish_id_))
    {
        log(logging::LogType::ERROR, "Failed to locate robot interface status publish channel");
        return;
    }
    if (!getPublishChannelByName(helpers::robot_interface::robot_interface_finished_producer.channel_type_identifier_, finished_publish_id_))
    {
        log(logging::LogType::ERROR, "Failed to locate robot interface finished publish channel");
        return;
    }

    if (!allocator_)
    {
        log(logging::LogType::ERROR, "Failed to initialize allocator for Kassow robot module");
        return;
    }

    rpc_client_->setStatusCallback([this](const helpers::robot_interface::StatusMessage& msg, std::span<const std::byte> blob)
    {
        forwardStatus(msg, blob);
    });
    rpc_client_->setFinishedCallback([this](const helpers::robot_interface::FinishedMessage& msg, std::span<const std::byte> blob)
    {
        forwardFinished(msg, blob);
    });

    valid_ = true;
}

RobotModuleKassow::~RobotModuleKassow() noexcept
{
    threadStop(1000);
}

void* RobotModuleKassow::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    return nullptr;
}

IModule::IngressDecision RobotModuleKassow::onIngress(ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier, const message::MessageHeader&, QueueStatus) noexcept
{
    if (kind == ProcessingType::REQUEST && local_channel_id == response_channel_id_)
    {
        return IngressDecision::ACCEPT;
    }
    return IngressDecision::DROP;
}

void RobotModuleKassow::processMessage(uint32_t, ChannelIdentifier, message::MessageHeader) noexcept
{
    log(logging::LogType::WARNING, "Kassow robot module received unexpected subscribed message, dropping");
}

ResponseData RobotModuleKassow::processRequest(uint32_t response_producer_id, ChannelIdentifier, message::MessageHeader message) noexcept
{
    if (response_producer_id != response_channel_id_)
    {
        log(logging::LogType::WARNING, "Request received on unknown response channel, dropping");
        return { .success_ = false };
    }

    if (message.data_ == nullptr || message.data_len_ != sizeof(helpers::robot_interface::Request))
    {
        log(logging::LogType::WARNING, "Robot request has invalid header size");
        return { .success_ = false };
    }

    helpers::robot_interface::Request* request = reinterpret_cast<helpers::robot_interface::Request*>(message.data_);

    std::span<const std::byte> request_blob;
    if (message.blob_count_ > 0 && message.blobs_ && message.blobs_[0].valid())
    {
        request_blob = std::span<const std::byte>(reinterpret_cast<const std::byte*>(message.blobs_[0].data()), static_cast<size_t>(message.blobs_[0].size()));
        if (message.blob_count_ > 1)
        {
            log(logging::LogType::WARNING, "Robot request contains multiple blobs, using only the first one");
        }
    }

    if (!ensureConnected())
    {
        return { .success_ = false };
    }

    helpers::robot_interface::Response response{};
    std::vector<std::byte> response_blob;
    bool ok = rpc_client_->sendRequest(*request,
                                       request_blob,
                                       response,
                                       response_blob,
                                       std::chrono::milliseconds(REQUEST_TIMEOUT_MS));

    ResponseData resp_data;
    resp_data.success_ = ok;
    if (!ok)
    {
        return resp_data;
    }

    const uint8_t* data_start = reinterpret_cast<const uint8_t*>(&response);
    resp_data.data_.assign(data_start, data_start + sizeof(response));

    if (!response_blob.empty())
    {
        auto blob_copy = makeBlobCopy(response_blob);
        if (blob_copy.valid())
        {
            resp_data.blobs_.push_back(std::move(blob_copy));
        }
        else
        {
            log(logging::LogType::WARNING, "Failed to allocate response blob, sending response without blob data");
        }
    }

    return resp_data;
}

void RobotModuleKassow::processResponse(uint32_t, ChannelIdentifier, message::MessageHeader) noexcept
{
    log(logging::LogType::WARNING, "Kassow robot module does not expect responses");
}

bool RobotModuleKassow::threadStart(uint32_t) noexcept
{
    if (!valid_)
    {
        return false;
    }
    stop_async_.store(false, std::memory_order_release);
    async_thread_ = std::thread(&RobotModuleKassow::asyncPollLoop, this);
    return true;
}

bool RobotModuleKassow::threadStop(uint32_t timeout_ms) noexcept
{
    stop_async_.store(true, std::memory_order_release);
    if (async_thread_.joinable())
    {
        if (timeout_ms == 0)
        {
            async_thread_.join();
        }
        else
        {
            auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeout_ms))
            {
                if (async_thread_.joinable())
                {
                    async_thread_.join();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (async_thread_.joinable())
            {
                async_thread_.join();
            }
        }
    }
    return true;
}

ISerializableModule::SaveData RobotModuleKassow::save() noexcept
{
    SaveData data;
    data.supports_saving_ = false;
    data.success_ = true;
    data.schema_version_ = 1;
    return data;
}

bool RobotModuleKassow::load(ISerializableModule::SaveData) noexcept
{
    return true;
}

void RobotModuleKassow::asyncPollLoop()
{
    while (!stop_async_.load(std::memory_order_acquire))
    {
        if (!ensureConnected())
        {
            std::this_thread::sleep_for(RECONNECT_WAIT);
            continue;
        }

        if (!rpc_client_->pollOnce(ASYNC_POLL_WAIT))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

bool RobotModuleKassow::ensureConnected()
{
    if (rpc_client_->isConnected())
    {
        return true;
    }

    bool connected = rpc_client_->connect(kassow_host_, kassow_port_);
    if (!connected)
    {
        log(logging::LogType::WARNING, "Failed to connect to Kassow CBun TCP server");
    }
    return connected;
}

void RobotModuleKassow::forwardStatus(const helpers::robot_interface::StatusMessage& status, std::span<const std::byte> blob)
    {
        message::SharedDataBlob blob_copy;
        if (!blob.empty())
        {
            blob_copy = makeBlobCopy(blob);
        if (!blob_copy.valid())
        {
            log(logging::LogType::WARNING, "Dropping status blob because allocation failed");
        }
    }

    auto status_copy = status;

    message::MessageHeader header{
        .data_ = reinterpret_cast<uint8_t*>(&status_copy),
        .data_len_ = sizeof(status_copy),
        .blobs_ = blob_copy.valid() ? &blob_copy : nullptr,
        .blob_count_ = blob_copy.valid() ? 1 : 0,
        .id_ = 0,
        .timestamp_ns_ = nowNs(),
        .success_ = true
    };

    sendMessage(status_publish_id_, header);
}

void RobotModuleKassow::forwardFinished(const helpers::robot_interface::FinishedMessage& finished, std::span<const std::byte> blob)
    {
        message::SharedDataBlob blob_copy;
        if (!blob.empty())
        {
            blob_copy = makeBlobCopy(blob);
        if (!blob_copy.valid())
        {
            log(logging::LogType::WARNING, "Dropping finished blob because allocation failed");
        }
    }

    auto finished_copy = finished;

    message::MessageHeader header{
        .data_ = reinterpret_cast<uint8_t*>(&finished_copy),
        .data_len_ = sizeof(finished_copy),
        .blobs_ = blob_copy.valid() ? &blob_copy : nullptr,
        .blob_count_ = blob_copy.valid() ? 1 : 0,
        .id_ = 0,
        .timestamp_ns_ = nowNs(),
        .success_ = true
    };

    sendMessage(finished_publish_id_, header);
}

message::SharedDataBlob RobotModuleKassow::makeBlobCopy(std::span<const std::byte> blob)
{
    if (!allocator_)
    {
        return {};
    }

    return allocator_->allocateFromData(blob);
}
