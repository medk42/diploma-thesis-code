#include "frontend_module.h"


#include "webapp/frontend_app.h"


#include <filesystem>
#include <fstream>


#define APP_NAME "aergo_frontend"


using namespace aergo::default_modules::frontend_module;
using namespace aergo::module;



FrontendModule::FrontendModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, const logging::ILogger* logger, uint64_t module_id)
: BaseModule(data_path, core, channel_map_info, logger, module_id), valid_(false)
{
    if (!parseConfigFile()) {
        log(aergo::module::logging::LogType::ERROR, "Failed to parse configuration file.");
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
    // accept only responses on request consumer channel 0 (from activation wrapper), drop all others
    if (kind == ProcessingType::RESPONSE && local_channel_id == 0)
    {
        // TODO if queue is full, we should notify the visualizer that the message was dropped
        return IngressDecision::ACCEPT; // accept all, responses will be dropped automatically if queue is full
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
            return std::make_unique<webapp::FrontendApp>(env, w_server_.get(), &frontend_state_, this);
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



void FrontendModule::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    // process responses on request consumer channel 0 from activation wrapper to control module activation and parameters
    if (request_consumer_id == 0)
    {
        std::lock_guard lock(frontend_state_.mutex_);
        webapp::ActivationResponse resp {
            .running_module_index_ = source_channel.producer_module_id_,
            .success_ = message.success_ && message.data_ && message.data_len_ == sizeof(aergo::module::helpers::activation_wrapper::message_types::Response),
        };
        if (resp.success_)
        {
            resp.response_ = *reinterpret_cast<aergo::module::helpers::activation_wrapper::message_types::Response*>(message.data_);
            if (message.blob_count_ > 0 && message.blobs_ && message.blobs_[0].valid())
            {
                resp.data_blob_.resize(message.blobs_[0].size());
                std::memcpy(resp.data_blob_.data(), message.blobs_[0].data(), message.blobs_[0].size());
            }
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
            auto pos = line.find('=');
            if (pos == std::string::npos) 
            {
                log(aergo::module::logging::LogType::WARNING, "Ignoring malformed line in config file: " + line);
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
            "PORT_HTTPS",
            "SSL_CERTIFICATE_PATH",
            "SSL_PRIVATE_KEY_PATH",
            "SSL_TMP_DH_PATH",
            "WT_CONFIG"
        };
        for (const auto &k : keys) {
            if (kv.find(k) == kv.end()) 
            {
                log(aergo::module::logging::LogType::ERROR, "Missing required config key: " + k);
                return false;
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
        server_parameters_.ssl_certificate_path = mkpath(kv["SSL_CERTIFICATE_PATH"]);
        server_parameters_.ssl_private_key_path = mkpath(kv["SSL_PRIVATE_KEY_PATH"]);
        server_parameters_.ssl_tmp_dh_path = mkpath(kv["SSL_TMP_DH_PATH"]);
        server_parameters_.wt_config_path = mkpath(kv["WT_CONFIG"]);

        // Validate that referenced files exist
        if (!fs::exists(server_parameters_.docroot))
        {
            log(aergo::module::logging::LogType::ERROR, "Resource path not found: " + server_parameters_.docroot);
            return false;
        }
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
        if (!fs::exists(server_parameters_.wt_config_path))
        {
            log(aergo::module::logging::LogType::ERROR, "WT config path not found: " + server_parameters_.wt_config_path);
            return false;
        }

        return true;
    } catch (...) {
        log(aergo::module::logging::LogType::ERROR, "Exception occurred while parsing config file");
        return false;
    }
}