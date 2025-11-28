#include "robot_module_kassow/robot_module_kassow.h"

#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/robot_interface/features/robot_control/messages.h"
#include "module_helpers/serialization_helper/serialization_helper.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <span>
#include <thread>

using namespace aergo::default_modules::robot_module_kassow;
using namespace aergo::module;
using namespace aergo::module::helpers::robot_interface;
using namespace robot_control;
using json = nlohmann::json;
namespace vis3d = aergo::module::helpers::visualization_3d_interface;


void RobotModuleKassow::LoggerAdapter::log(aergo::robot::kassow::rpc::RpcLogType type, const char* message) const noexcept
{
    if (!logger_ || !message)
    {
        return;
    }

    using LT = aergo::module::logging::LogType;
    LT mapped = LT::INFO;
    switch (type)
    {
        case aergo::robot::kassow::rpc::RpcLogType::INFO: mapped = LT::INFO; break;
        case aergo::robot::kassow::rpc::RpcLogType::WARNING: mapped = LT::WARNING; break;
        case aergo::robot::kassow::rpc::RpcLogType::ERROR: mapped = LT::ERROR; break;
    }
    logger_->log(mapped, message);
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
      rpc_logger_(logger),
      rpc_client_(std::make_unique<aergo::robot::kassow::rpc::RpcClient>(&rpc_logger_)),
      allocator_(createDynamicAllocator()),
      stop_async_(false),
      activated_(false)
{
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

    visualization_helper_ = std::make_unique<vis3d::VisualizationHelper>(this);
    if (!visualization_helper_->valid())
    {
        log(logging::LogType::ERROR, "Failed to initialize 3D visualization helper");
        return;
    }

    robot_visualization_ = std::make_unique<robot_vis::RobotVisualization>(visualization_helper_.get());
    if (!robot_visualization_->registerResources())
    {
        log(logging::LogType::ERROR, "Failed to register robot visualization resources");
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

RobotModuleKassow::~RobotModuleKassow() noexcept = default;

void* RobotModuleKassow::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    if (id == typeid(helpers::activation_wrapper::IActivableModule)) return static_cast<helpers::activation_wrapper::IActivableModule*>(this);
    return nullptr;
}

IModule::IngressDecision RobotModuleKassow::onIngress(ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier identifier, const message::MessageHeader&, QueueStatus queue_status) noexcept
{
    if (kind == ProcessingType::REQUEST)
    {
        if (local_channel_id == response_channel_id_)
        {
            if (queue_status != QueueStatus::NORMAL)
            {
                log(logging::LogType::WARNING, "Kassow robot module dropping request due to request queue full: " + std::to_string(identifier.producer_module_id_) + "/" + std::to_string(identifier.producer_channel_id_));
                return IngressDecision::DROP;
            }
            return IngressDecision::ACCEPT;
        }
        else if (local_channel_id == visualization_helper_->getResponseProducerChannel())
        {
            return IngressDecision::ACCEPT; // accept all visualization requests
        }
    }
    return IngressDecision::DROP;
}

void RobotModuleKassow::processMessage(uint32_t, ChannelIdentifier, message::MessageHeader) noexcept
{
    log(logging::LogType::WARNING, "Kassow robot module received unexpected subscribed message, dropping");
}

ResponseData RobotModuleKassow::processRequest(uint32_t response_producer_id, ChannelIdentifier, message::MessageHeader message) noexcept
{
    if (response_producer_id == visualization_helper_->getResponseProducerChannel())
    {
        std::lock_guard<std::mutex> lock(vis3d_mutex_);
        return visualization_helper_->processVisualizationRequest(message);
    }

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
    bool ok = rpc_client_->sendRequest(
        *request,
        request_blob,
        response,
        response_blob,
        std::chrono::milliseconds(request_timeout_ms_)
    );

    ResponseData resp_data;
    resp_data.success_ = ok;
    if (!ok) // failure to receive response from the CBun TCP server
    {
        return resp_data;
    }

    const uint8_t* data_start = reinterpret_cast<const uint8_t*>(&response);
    resp_data.data_.assign(data_start, data_start + sizeof(response));

    if (!response_blob.empty())
    {
        auto blob_copy = allocator_->allocateFromData(std::span<std::byte>(response_blob));
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
    return true; // threads are started on activation
}

bool RobotModuleKassow::threadStop(uint32_t timeout_ms) noexcept
{
    // ensure we are deactivated (we are ignoring timeout here)
    std::atomic<bool> cancel{false};
    std::atomic<bool> cancelled{false};
    deactivate(cancel, cancelled);

    return true;
}

bool RobotModuleKassow::activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled)
{
    std::lock_guard<std::mutex> lock(activation_mutex_);

    if (!valid_)
    {
        return false;
    }

    if (activated_.load(std::memory_order_acquire))
    {
        return true;
    }

    if (parameter_values.size() != 5)
    {
        log(logging::LogType::ERROR, "Activation parameters malformed (expected 5 parameters)");
        return false;
    }

    auto readInt64 = [](const std::vector<uint8_t>& buf, int64_t& out) -> bool {
        if (buf.size() != sizeof(int64_t))
        {
            return false;
        }
        std::memcpy(&out, buf.data(), sizeof(int64_t));
        return true;
    };

    std::string host(reinterpret_cast<const char*>(parameter_values[0][0].data()), parameter_values[0][0].size());
    int64_t port{};
    int64_t req_timeout{};
    int64_t poll_interval{};
    int64_t reconnect_wait{};

    if (!readInt64(parameter_values[1][0], port) ||
        !readInt64(parameter_values[2][0], req_timeout) ||
        !readInt64(parameter_values[3][0], poll_interval) ||
        !readInt64(parameter_values[4][0], reconnect_wait))
    {
        log(logging::LogType::ERROR, "Activation parameters malformed (integer size mismatch)");
        return false;
    }

    if (host.empty() || port <= 0 || port > 65535 || req_timeout <= 0 || poll_interval <= 0 || reconnect_wait <= 0)
    {
        log(logging::LogType::ERROR, "Activation parameters out of range");
        return false;
    }

    kassow_host_ = host;
    kassow_port_ = static_cast<uint16_t>(port);
    request_timeout_ms_ = static_cast<uint32_t>(req_timeout);
    poll_interval_ms_ = static_cast<uint32_t>(poll_interval);
    reconnect_wait_ms_ = static_cast<uint32_t>(reconnect_wait);

    if (!rpc_client_->connect(kassow_host_, kassow_port_))
    {
        log(logging::LogType::ERROR, "Failed to connect to Kassow CBun during activation");
        return false;
    }

    stop_async_.store(false, std::memory_order_release);
    activated_.store(true, std::memory_order_release);
    async_thread_ = std::thread(&RobotModuleKassow::asyncPollLoop, this);

    return true;
}

bool RobotModuleKassow::deactivate(const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled)
{
    std::lock_guard<std::mutex> lock(activation_mutex_);

    if (!activated_.load(std::memory_order_acquire))
    {
        return true;
    }

    activated_.store(false, std::memory_order_release);
    stop_async_.store(true, std::memory_order_release);
    if (async_thread_.joinable())
    {
        async_thread_.join();
    }
    rpc_client_->disconnect();

    return true;
}

ISerializableModule::SaveData RobotModuleKassow::save() noexcept
{
    std::lock_guard<std::mutex> lock(activation_mutex_);

    SaveData data;
    data.supports_saving_ = true;
    data.schema_version_ = 1;
    data.success_ = true;

    json header;
    header["activated"] = activated_.load(std::memory_order_acquire);
    header["host"] = kassow_host_;
    header["port"] = kassow_port_;
    header["request_timeout_ms"] = request_timeout_ms_;
    header["poll_interval_ms"] = poll_interval_ms_;
    header["reconnect_wait_ms"] = reconnect_wait_ms_;

    data.json_header_ = header.dump();
    return data;
}

bool RobotModuleKassow::load(ISerializableModule::SaveData data) noexcept
{
    // Deactivate first in case we are already activated
    std::atomic<bool> cancel{false};
    std::atomic<bool> cancelled{false};
    deactivate(cancel, cancelled);

    std::lock_guard<std::mutex> lock(activation_mutex_);
    if (!data.supports_saving_ || data.schema_version_ != 1)
    {
        log(logging::LogType::ERROR, "Unsupported save data for Kassow robot module");
        return false;
    }

    try
    {
        auto header = json::parse(data.json_header_);
        if (!header.contains("activated") || !header.contains("host") || !header.contains("port") ||
            !header.contains("request_timeout_ms") || !header.contains("poll_interval_ms") || !header.contains("reconnect_wait_ms"))
        {
            log(logging::LogType::ERROR, "Save data missing required fields");
            return false;
        }

        kassow_host_ = header["host"].get<std::string>();
        kassow_port_ = static_cast<uint16_t>(header["port"].get<uint64_t>());
        request_timeout_ms_ = static_cast<uint32_t>(header["request_timeout_ms"].get<uint64_t>());
        poll_interval_ms_ = static_cast<uint32_t>(header["poll_interval_ms"].get<uint64_t>());
        reconnect_wait_ms_ = static_cast<uint32_t>(header["reconnect_wait_ms"].get<uint64_t>());

        if (kassow_host_.empty() || kassow_port_ == 0 || request_timeout_ms_ == 0 || poll_interval_ms_ == 0 || reconnect_wait_ms_ == 0)
        {
            log(logging::LogType::ERROR, "Loaded parameters are invalid");
            return false;
        }

        bool was_activated = header["activated"].get<bool>();
        if (was_activated)
        {
            stop_async_.store(false, std::memory_order_release);
            if (rpc_client_->connect(kassow_host_, kassow_port_))
            {
                activated_.store(true, std::memory_order_release);
                async_thread_ = std::thread(&RobotModuleKassow::asyncPollLoop, this);
            }
            else
            {
                log(logging::LogType::ERROR, "Failed to reconnect during load");
                activated_.store(false, std::memory_order_release);
                return false;
            }
        }
    }
    catch (const std::exception& e)
    {
        log(logging::LogType::ERROR, std::string("Failed to parse save data: ") + e.what());
        return false;
    }

    return true;
}

void RobotModuleKassow::asyncPollLoop()
{
    while (!stop_async_.load(std::memory_order_acquire))
    {
        if (!ensureConnected())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_wait_ms_));
            continue;
        }

        if (!rpc_client_->pollOnce(std::chrono::milliseconds(poll_interval_ms_)))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

bool RobotModuleKassow::ensureConnected()
{
    if (stop_async_.load(std::memory_order_acquire) || !activated_.load(std::memory_order_acquire))
    {
        return false;
    }

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
    if (!activated_.load(std::memory_order_acquire))
    {
        return;
    }

    updateVisualization(status, blob);

    message::SharedDataBlob blob_copy;
    if (!blob.empty())
    {
        blob_copy = allocator_->allocateFromData(blob);
        if (!blob_copy.valid())
        {
            log(logging::LogType::WARNING, "Failed to allocate blob in status forwarding, sending without blob");
        }
    }

    auto status_copy = status;
    if (blob_copy.valid())  // blob must not be empty + successful allocation of blob_copy
    {
        sendMessage(status_publish_id_, message::MessageHeader::Message(&status_copy, &blob_copy));
    }
    else
    {
        sendMessage(status_publish_id_, message::MessageHeader::Message(&status_copy));
    }
}

void RobotModuleKassow::forwardFinished(const helpers::robot_interface::FinishedMessage& finished, std::span<const std::byte> blob)
{
    if (!activated_.load(std::memory_order_acquire))
    {
        return;
    }

    message::SharedDataBlob blob_copy;
    if (blob.empty())
    {
        blob_copy = allocator_->allocateFromData(blob);
        if (!blob_copy.valid())
        {
            log(logging::LogType::WARNING, "Failed to allocate blob in finished forwarding, sending without blob");
        }
    }

    auto finished_copy = finished;
    if (blob_copy.valid()) // blob must not be empty + successful allocation of blob_copy
    {
        sendMessage(finished_publish_id_, message::MessageHeader::Message(&finished_copy, &blob_copy));
    }
    else
    {
        sendMessage(finished_publish_id_, message::MessageHeader::Message(&finished_copy));
    }
}


void RobotModuleKassow::updateVisualization(const helpers::robot_interface::StatusMessage& status, std::span<const std::byte> blob)
{
    if (status.feature != helpers::robot_interface::RobotFeature::ROBOT_CONTROL)
    {
        return; // Only process visualization updates from robot control feature
    }

    status_messages::deserialization::BufferReader reader(blob.data(), blob.size());
    if (!status_messages::deserialization::deserializeStatusMessage(reader, status_message_buffered_))
    {
        log(logging::LogType::WARNING, "Failed to deserialize status message for visualization update");
        return;
    }

    log(logging::LogType::INFO, "Pose update: x=" + std::to_string(status_message_buffered_.current_pose.position.x) +
        " y=" + std::to_string(status_message_buffered_.current_pose.position.y) +
        " z=" + std::to_string(status_message_buffered_.current_pose.position.z) + 
        " joints=[" + std::to_string(status_message_buffered_.joint_positions[0]) + ", " +
        std::to_string(status_message_buffered_.joint_positions[1]) + ", " +
        std::to_string(status_message_buffered_.joint_positions[2]) + ", " +
        std::to_string(status_message_buffered_.joint_positions[3]) + ", " +
        std::to_string(status_message_buffered_.joint_positions[4]) + ", " +
        std::to_string(status_message_buffered_.joint_positions[5]) + ", " +
        std::to_string(status_message_buffered_.joint_positions[6]) + "]"
    );

    std::lock_guard<std::mutex> lock(vis3d_mutex_);
    if (!robot_visualization_->isVisualizationCreated())
    {
        visualization_helper_->announce();
        robot_visualization_->createVisualization();
    }

    robot_visualization_->updateVisualization(std::span<const double>(status_message_buffered_.joint_positions));
}