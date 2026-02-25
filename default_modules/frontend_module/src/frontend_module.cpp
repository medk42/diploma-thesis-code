#include "frontend_module.h"


#include "module_common/module_interface_.h"
#include "webapp/frontend_app.h"

#include "module_helpers/camera_messages/messages.h"
#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/robot_interface/message_types.h"
#include "module_helpers/pen_messages/message_types.h"

#include <filesystem>
#include <fstream>
#include <opencv2/opencv.hpp>

#undef ERROR


#define APP_NAME "aergo_frontend"


using namespace aergo::default_modules::frontend_module;
using namespace aergo::module;
namespace cm = aergo::module::helpers::camera_messages;
namespace ri = aergo::module::helpers::robot_interface;
namespace rc = ri::robot_control;
namespace pm = aergo::module::helpers::pen_messages;



FrontendModule::FrontendModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, const logging::ILogger* logger, uint64_t module_id, const ModuleInfo* module_info)
: BaseModule(data_path, core, channel_map_info, logger, module_id, module_info), valid_(false)
{
    if (!parseConfigFile()) 
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to parse configuration file.");
        return;
    }

    // find channel IDs
    if (!getRequestChannelByName(helpers::activation_wrapper::message_types::activation_request_consumer.channel_type_identifier_, activation_request_channel_id_))
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to find activation request channel.");
        return;
    }

    if (!getSubscribeChannelByName(cm::camera_image_consumer.channel_type_identifier_, camera_subscribe_channel_id_))
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to find camera image subscribe channel.");
        return;
    }

    if (!getSubscribeChannelByName(helpers::robot_interface::robot_interface_status_consumer.channel_type_identifier_, robot_interface_status_subscribe_channel_id_))
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to find robot interface status subscribe channel.");
        return;
    }

    if (!getRequestChannelByName(helpers::robot_interface::robot_interface_request_consumer.channel_type_identifier_, robot_interface_request_channel_id_))
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to find robot interface request channel.");
        return;
    }

    if (!getSubscribeChannelByName(helpers::pen_messages::pen_message_intent_subscribe_consumer.channel_type_identifier_, pen_message_intent_subscribe_channel_id_))
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to find pen message intent subscribe channel.");
        return;
    }

    frontend_state_.scene_visualization_handler_ = std::make_unique<webapp::ui::helper::SceneVisualizationHandler>(this);
    if (!frontend_state_.scene_visualization_handler_->valid())
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to initialize scene visualization handler.");
        return;
    }

    frontend_state_.program_tree_state_.usecase_tree_ = std::make_unique<helpers::usecase_tree::UsecaseTree>(this);
    if (!frontend_state_.program_tree_state_.usecase_tree_->valid())
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to initialize usecase tree.");
        return;
    }

    valid_ = true;
}



void* FrontendModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    return nullptr;
}



IModule::IngressDecision FrontendModule::onIngress(ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, QueueStatus queue_status) noexcept
{
    if (kind == ProcessingType::RESPONSE) 
    {
        if (queue_status == QueueStatus::QUEUE_FULL)
        {
            // TODO if queue is full, we should notify the visualizer that the message was dropped
            log(aergo::module::logging::LogType::WARNING, "Response queue full, dropping response message on channel " + std::to_string(local_channel_id));
        }

        if (local_channel_id == activation_request_channel_id_) // accept activation responses
        {
            return IngressDecision::ACCEPT; // accept all, responses will be dropped automatically if queue is full
        }
        if (local_channel_id == frontend_state_.scene_visualization_handler_->getSceneRequestChannelId()) // accept visualization responses
        {
            return IngressDecision::ACCEPT; // accept all, responses will be dropped automatically if queue is full
        }
        if (local_channel_id == frontend_state_.program_tree_state_.usecase_tree_->getUsecaseRequestChannelId()) // accept usecase tree responses
        {
            return IngressDecision::ACCEPT; // accept all, responses will be dropped automatically if queue is full
        }
        
        return IngressDecision::DROP; // drop all other responses (they are not expected)
    }
    else if (kind == ProcessingType::MESSAGE)
    {
        if (queue_status == QueueStatus::QUEUE_FULL)
        {
            // TODO if queue is full, we should notify the visualizer that the message was dropped
            log(aergo::module::logging::LogType::WARNING, "Message queue full, dropping message on channel " + std::to_string(local_channel_id));
        }

        if (local_channel_id == camera_subscribe_channel_id_) // accept camera input messages 
        {
            if (!frontend_state_.has_camera_input_)
            {
                frontend_state_.camera_module_id_ = src.module_id_;
                frontend_state_.has_camera_input_ = true;
            }

            if (frontend_state_.camera_module_id_ == src.module_id_)
            {
                return IngressDecision::ACCEPT_REPLACE_QUEUE; // accept camera messages from the selected module, keep only the latest message
            }
            else
            {
                return IngressDecision::DROP; // drop messages from other modules
            }
        }
        if (local_channel_id == frontend_state_.scene_visualization_handler_->getSceneSubscribeChannelId()) // accept visualization messages
        {
            return IngressDecision::ACCEPT; // accept all, messages will be dropped automatically if queue is full
        }
        if (local_channel_id == robot_interface_status_subscribe_channel_id_) // accept robot interface status messages
        {
            return IngressDecision::ACCEPT; // accept all, messages will be dropped automatically if queue is full
        }
        if (local_channel_id == pen_message_intent_subscribe_channel_id_) // accept pen message intent messages
        {
            return IngressDecision::ACCEPT; // accept all, keep only the latest message
        }

        return IngressDecision::DROP; // drop all other messages (they are not expected)
    }
    return IngressDecision::DROP;
}



bool FrontendModule::threadStart(uint32_t timeout_ms) noexcept
{
    if (w_server_)
    {
        return false; // already running
    }

    try
    {
        auto args = makeArgs();
        std::vector<char*> cargs; cargs.reserve(args.size());
        for (auto& s : args) cargs.push_back(const_cast<char*>(s.c_str()));

        // Create and configure WServer instance
        w_server_ = std::make_unique<Wt::WServer>(APP_NAME, server_parameters_.wt_config_path);
        w_server_->setServerConfiguration(static_cast<int>(cargs.size()), cargs.data());
        w_server_->addEntryPoint(Wt::EntryPointType::Application, [this](const Wt::WEnvironment& env) {
            return std::make_unique<webapp::FrontendApp>(env, w_server_.get(), &frontend_state_, this, activation_request_channel_id_);
        });
        if (!w_server_->start())
        {
            log(aergo::module::logging::LogType::ERROR, "WT Server failed to start");
            w_server_.reset();
            return false;
        }
        return true;
    }
    catch(const std::exception& e)
    {
        log(aergo::module::logging::LogType::ERROR, "Received exception while starting WT Server: \"" + std::string(e.what()) + "\"");
        w_server_.reset();
        return false;
    }
}



std::vector<std::string> FrontendModule::makeArgs()
{
    if (server_parameters_.enable_https)
    {
        return {
            APP_NAME, // argv[0]
            "--docroot", server_parameters_.docroot,
            "--http-address", "0.0.0.0",
            "--http-port", std::to_string(server_parameters_.port_http),
            "--https-address", "0.0.0.0",
            "--https-port", std::to_string(server_parameters_.port_https),
            "--ssl-certificate", server_parameters_.ssl_certificate_path,
            "--ssl-private-key", server_parameters_.ssl_private_key_path,
            "--ssl-tmp-dh", server_parameters_.ssl_tmp_dh_path,
        };
    }
    else
    {
        return {
            APP_NAME, // argv[0]
            "--docroot", server_parameters_.docroot,
            "--http-address", "0.0.0.0",
            "--http-port", std::to_string(server_parameters_.port_http),
        };
    }
}



bool FrontendModule::threadStop(uint32_t timeout_ms) noexcept
{
    if (!w_server_)
    {
        return true; // stopped already
    }

    try
    {
        w_server_->stop();   // blocks until threads shut down
        return true;
    }
    catch (const std::exception& e)
    {
        log(aergo::module::logging::LogType::ERROR, "Received exception while stopping WT Server: \"" + std::string(e.what()) + "\"");
        return false;
    }
}



void FrontendModule::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (subscribe_consumer_id == camera_subscribe_channel_id_) // camera input messages
    {
        if (source_channel.module_id_ != frontend_state_.camera_module_id_)
            return;

        cm::CameraMessage cam_msg;
        if (!message.readAs<cm::CameraMessage>(cam_msg))
        {
            log(aergo::module::logging::LogType::WARNING, "Failed to read camera message header.");
            return;
        }

        if (message.blobs_ == nullptr || message.blob_count_ < 1 || !message.blobs_[0].valid())
        {
            log(aergo::module::logging::LogType::WARNING, "Camera message missing image blob.");
            return;
        }

        auto& blob = message.blobs_[0];

        if (!cm::isBlobValid(reinterpret_cast<std::byte*>(blob.data()), blob.size()))
        {
            log(aergo::module::logging::LogType::WARNING, "Camera blob is not valid.");
            return;
        }

        cm::BlobHeader blob_header;
        cm::ImageHeader img_header;
        if (!cm::readBlobHeader(reinterpret_cast<std::byte*>(blob.data()), blob.size(), blob_header)
        ||  !cm::readImageHeader(reinterpret_cast<std::byte*>(blob.data()), blob.size(), 0, img_header)
        ||  blob_header.image_count_ < 1)
        {
            log(aergo::module::logging::LogType::WARNING, "Camera blob headers are not valid.");
            return;
        }
        
        int mat_type = (blob_header.format_ == cm::ImageFormat::BGR8) ? CV_8UC3 :
                       (blob_header.format_ == cm::ImageFormat::BGRA8) ? CV_8UC4 : -1;
        if (mat_type == -1)
        {
            log(aergo::module::logging::LogType::WARNING, "Unsupported camera image format in blob.");
            return;
        }

        cv::Mat img(img_header.height_, img_header.width_, mat_type, blob.data() + img_header.data_offset_, blob_header.stride_);
        std::vector<uint8_t> jpeg;
        std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 80 };

        // TODO lower resolution or frame rate if too high, replace direct send with some better solution 
        cv::imencode(".jpg", img, jpeg, params);
        // log(aergo::module::logging::LogType::INFO, "Received camera frame " + std::to_string(img_header->width_) + "x" + std::to_string(img_header->height_) + ", encoded to JPEG size " + std::to_string(jpeg.size()) + " bytes");

        {
            std::lock_guard lock(frontend_state_.mutex_);
            if (frontend_state_.active_app_ && frontend_state_.current_screen_ == webapp::FrontendScreen::MAIN_VISUALIZATION)
            {
                frontend_state_.active_app_->updateFrame(std::move(jpeg));
            }
        }
    }
    else if (subscribe_consumer_id == frontend_state_.scene_visualization_handler_->getSceneSubscribeChannelId()) // 3D visualization messages
    {
        std::lock_guard lock(frontend_state_.mutex_);
        frontend_state_.scene_visualization_handler_->processMessage(subscribe_consumer_id, source_channel, message);
    }
    else if (subscribe_consumer_id == robot_interface_status_subscribe_channel_id_) // robot interface status messages
    {
        ri::StatusMessage status_msg;
        if (!message.readAs<ri::StatusMessage>(status_msg))
        {
            log(logging::LogType::WARNING, "FrontendModule: Failed to deserialize robot status message header.");
            return;
        }

        if (status_msg.feature != ri::RobotFeature::ROBOT_CONTROL)
        {
            log(logging::LogType::WARNING, "FrontendModule: Received robot status message for unsupported feature.");
            return;
        }

        if (message.blob_count_ != 1 || message.blobs_ == nullptr || !message.blobs_[0].valid())
        {
            log(logging::LogType::WARNING, "FrontendModule: Robot status message missing data blob.");
            return;
        }

        message::SharedDataBlob blob = message.blobs_[0];
        rc::BufferReader reader(blob.data(), blob.size());

        rc::status_messages::deserialization::StatusMessage status;
        if (!rc::status_messages::deserialization::deserializeStatusMessage(reader, status))
        {
            log(logging::LogType::WARNING, "FrontendModule: Failed to deserialize robot status message.");
            return;
        }

        {
            std::lock_guard lock(frontend_state_.main_visualization_state_.main_visualization_state_mutex_);
            frontend_state_.main_visualization_state_.robot_status_message_valid_ = true;
            frontend_state_.main_visualization_state_.robot_status_message_ = status;
        }
    }
    else if (subscribe_consumer_id == pen_message_intent_subscribe_channel_id_) // pen message intent messages
    {
        pm::PenMessageIntent intent_msg;
        if (!message.readAs<pm::PenMessageIntent>(intent_msg))
        {
            log(logging::LogType::WARNING, "FrontendModule: Failed to deserialize pen message intent header.");
            return;
        }

        if (intent_msg.intent == pm::PenIntent::SPECIAL_ACTION)
        {
            std::lock_guard lock(frontend_state_.mutex_);
            // Trigger auto-loading of next unloaded CUSTOM parameter
            frontend_state_.program_tree_state_.auto_load_triggered_ = true;
        }
    }
}



void FrontendModule::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    // process responses on request consumer channel 0 from activation wrapper to control module activation and parameters
    if (request_consumer_id == activation_request_channel_id_)
    {
        std::lock_guard lock(frontend_state_.mutex_);

        helpers::activation_wrapper::message_types::Response response;
        if (message.readAs<helpers::activation_wrapper::message_types::Response>(response))
        {
            std::vector<uint8_t> data_blob;
            if (message.blob_count_ > 0 && message.blobs_ && message.blobs_[0].valid())
            {
                data_blob.resize(message.blobs_[0].size());
                std::memcpy(data_blob.data(), message.blobs_[0].data(), message.blobs_[0].size());
            }

            frontend_state_.pending_activation_responses_.push_back(std::move(webapp::ActivationResponse{
                .running_module_index_ = source_channel.module_id_,
                .success_ = true,
                .response_ = response,
                .data_blob_ = std::move(data_blob)
            }));
        }
        else
        {
            frontend_state_.pending_activation_responses_.push_back(std::move(webapp::ActivationResponse{
                .running_module_index_ = source_channel.module_id_,
                .success_ = false,
            }));
        }
    }
    else if (request_consumer_id == frontend_state_.scene_visualization_handler_->getSceneRequestChannelId())
    {
        std::lock_guard lock(frontend_state_.mutex_);
        frontend_state_.scene_visualization_handler_->processVisualizationResponse(request_consumer_id, source_channel, message);
    }
    else if (request_consumer_id == frontend_state_.program_tree_state_.usecase_tree_->getUsecaseRequestChannelId())
    {
        std::lock_guard lock(frontend_state_.mutex_);
        if (w_server_)
        {
            std::vector<uint8_t> message_copy;
            if (message.data_ && message.data_len_ > 0)
            {
                message_copy.resize(message.data_len_);
                std::memcpy(message_copy.data(), message.data_, message.data_len_);
            }
            std::vector<aergo::module::message::SharedDataBlob> blobs_copy;
            if (message.blobs_)
            {
                for (size_t i = 0; i < message.blob_count_; ++i)
                {
                    blobs_copy.push_back(message.blobs_[i]); // blob is just a lightweight reference, can be copied directly
                }
            }
            
            uint64_t message_id = message.id_; // capture message ID for logging inside lambda if needed
            uint64_t timestamp_ns = message.timestamp_ns_; // capture timestamp for logging inside lambda if needed
            bool success = message.success_; // capture success flag for logging inside lambda if needed

            if (!frontend_state_.active_app_)
            {
                log(aergo::module::logging::LogType::WARNING, "No active frontend app to process usecase tree response.");
                return;
            }

            // Process the response in the Wt server thread - with frontend_state_ lock and UI lock held
            w_server_->post(frontend_state_.active_app_->sessionId(), [this, source_channel, message_copy, blobs_copy, message_id, timestamp_ns, success]() {
                std::unique_lock<std::mutex> lock(frontend_state_.mutex_);

                frontend_state_.program_tree_state_.usecase_tree_->handleResponse(
                    source_channel,
                    message::MessageHeader
                    {
                        .data_ = message_copy.empty() ? nullptr : const_cast<uint8_t*>(message_copy.data()),
                        .data_len_ = static_cast<uint64_t>(message_copy.size()),
                        .blobs_ = blobs_copy.empty() ? nullptr : const_cast<aergo::module::message::SharedDataBlob*>(blobs_copy.data()),
                        .blob_count_ = static_cast<uint64_t>(blobs_copy.size()),
                        .id_ = message_id,
                        .timestamp_ns_ = timestamp_ns,
                        .success_ = success
                    }
                );
            });
        }
    }
}



// Helper: trim whitespace from both ends
static inline std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}



bool FrontendModule::parseConfigFile() {
    try {
        auto& data_path = getDataPath();

        if (data_path.empty()) 
        {
            log(aergo::module::logging::LogType::ERROR, "Data path is empty - data path is required to locate config files.");
            return false;
        }

        namespace fs = std::filesystem;
        fs::path cfg = fs::path(data_path) / "config.txt";
        if (!fs::exists(cfg)) 
        {
            log(aergo::module::logging::LogType::ERROR, "Config file not found: " + cfg.string());
            return false;
        }

        std::ifstream in(cfg);
        if (!in) 
        {
            log(aergo::module::logging::LogType::ERROR, "Failed to open config file: " + cfg.string());
            return false;
        }

        std::string line;
        std::map<std::string, std::string> kv;
        while (std::getline(in, line)) {
            auto comment_pos = line.find('#');
            if (comment_pos == 0) continue; // skip comment lines

            auto pos = line.find('=');
            if (pos == std::string::npos) 
            {
                log(aergo::module::logging::LogType::WARNING, "Ignoring malformed line in config file: \"" + line + "\"");
                continue;
            }
            std::string name = trim(line.substr(0, pos));
            std::string value = trim(line.substr(pos + 1));
            if (!name.empty()) kv[name] = value;
        }

        // Required keys
        const std::vector<std::string> keys = {
            "DOCROOT",
            "PORT_HTTP",
            "WT_CONFIG",
            "ENABLE_HTTPS"
        };

        const std::vector<std::string> keys_https = {
            "PORT_HTTPS",
            "SSL_CERTIFICATE_PATH",
            "SSL_PRIVATE_KEY_PATH",
            "SSL_TMP_DH_PATH"
        };

        for (const auto &k : keys) {
            if (kv.find(k) == kv.end()) 
            {
                log(aergo::module::logging::LogType::ERROR, "Missing required config key: " + k);
                return false;
            }
        }

        server_parameters_.enable_https = (kv["ENABLE_HTTPS"] == "1");

        if (server_parameters_.enable_https)
        {
            for (const auto &k : keys_https) {
                if (kv.find(k) == kv.end()) 
                {
                    log(aergo::module::logging::LogType::ERROR, "Missing required config key for HTTPS: " + k);
                    return false;
                }
            }
        }

        try {
            int p = std::stoi(kv["PORT_HTTP"]);
            if (p < 1 || p > 65535) 
            {
                log(aergo::module::logging::LogType::ERROR, "Invalid PORT_HTTP value: " + kv["PORT_HTTP"]);
                return false;
            }
            server_parameters_.port_http = static_cast<uint16_t>(p);

            if (server_parameters_.enable_https)
            {
                p = std::stoi(kv["PORT_HTTPS"]);
                if (p < 1 || p > 65535) 
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid PORT_HTTPS value: " + kv["PORT_HTTPS"]);
                    return false;
                }
                server_parameters_.port_https = static_cast<uint16_t>(p);
                if (server_parameters_.port_http == server_parameters_.port_https)
                {
                    log(aergo::module::logging::LogType::ERROR, "PORT_HTTP and PORT_HTTPS must be different");
                    return false;
                }
            }
        } catch (...)
        { 
            log(aergo::module::logging::LogType::ERROR, "Invalid port number in config file");
            return false;
        }

        auto mkpath = [&](const std::string &v)->std::string{
            fs::path p = v;
            if (p.is_relative()) p = fs::path(data_path) / p;
            return p.string();
        };

        server_parameters_.docroot = mkpath(kv["DOCROOT"]);
        server_parameters_.wt_config_path = mkpath(kv["WT_CONFIG"]);

        if (server_parameters_.enable_https)
        {
            server_parameters_.ssl_certificate_path = mkpath(kv["SSL_CERTIFICATE_PATH"]);
            server_parameters_.ssl_private_key_path = mkpath(kv["SSL_PRIVATE_KEY_PATH"]);
            server_parameters_.ssl_tmp_dh_path = mkpath(kv["SSL_TMP_DH_PATH"]);
        }

        // Validate that referenced files exist
        if (!fs::exists(server_parameters_.docroot))
        {
            log(aergo::module::logging::LogType::ERROR, "Resource path not found: " + server_parameters_.docroot);
            return false;
        }
        if (!fs::exists(server_parameters_.wt_config_path))
        {
            log(aergo::module::logging::LogType::ERROR, "WT config path not found: " + server_parameters_.wt_config_path);
            return false;
        }

        if (server_parameters_.enable_https)
        {
            if (!fs::exists(server_parameters_.ssl_certificate_path))
            {
                log(aergo::module::logging::LogType::ERROR, "SSL certificate path not found: " + server_parameters_.ssl_certificate_path);
                return false;
            }
            if (!fs::exists(server_parameters_.ssl_private_key_path))
            {
                log(aergo::module::logging::LogType::ERROR, "SSL private key path not found: " + server_parameters_.ssl_private_key_path);
                return false;
            }
            if (!fs::exists(server_parameters_.ssl_tmp_dh_path))
            {
                log(aergo::module::logging::LogType::ERROR, "SSL tmp DH path not found: " + server_parameters_.ssl_tmp_dh_path);
                return false;
            }
        }

        return true;
    } catch (...) {
        log(aergo::module::logging::LogType::ERROR, "Exception occurred while parsing config file");
        return false;
    }
}