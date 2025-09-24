#pragma once

#include "module_common/base_module.h"
#include "webapp/frontend_state.h"

#include <Wt/WServer.h>

namespace aergo::default_modules::frontend_module
{
    class FrontendModule : public aergo::module::BaseModule
    {
    public:
        // Constructor
        // - env requirements: there must be a config file located at
        //   <data_path>/config.txt. The file must contain lines in the
        //   form NAME=VALUE. Required names are:
        //     RESOURCE_PATH - path to document root (docroot)
        //     PORT_HTTP - numeric http port (1..65535)
        //     PORT_HTTPS - numeric https port (1..65535)
        //     SSL_CERTIFICATE_PATH - path to certificate file (PEM)
        //     SSL_PRIVATE_KEY_PATH - path to private key file (PEM)
        //     SSL_TMP_DH_PATH - path to temporary DH params file
        //     WT_CONFIG - path to Wt configuration file
        //   All paths may be absolute or relative to `data_dir`.
        // - The constructor will call parseConfigFile(); parsing and
        //   validation must succeed for the object to be considered valid
        //   (see `valid()`). Validation includes numeric port parsing and
        //   existence checks for file paths.
        FrontendModule(const char* data_path, aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id);
        
        /// @brief Ignore all messages, web visualization module only communicates via requests/responses.
        virtual void processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;

        /// @brief Ignore all requests, web visualization module only communicates via requests/responses.
        virtual aergo::module::ResponseData processRequest(uint32_t response_producer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override { return { .success_ = false }; }

        /// @brief Process responses on request consumer channel 0 from activation wrapper to control module activation and parameters.
        virtual void processResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;

        /// @brief Validity depends on successful parsing of config file in constructor. 
        virtual bool valid() noexcept override { return valid_; }
         
        virtual void* query_capability(const std::type_info& id) noexcept override;

        /// @brief Accept only responses on request consumer channel 0 (from activation wrapper), drop all others.
        virtual IngressDecision onIngress(ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src, const aergo::module::message::MessageHeader& msg, QueueStatus queue_status) noexcept override;

        /// @brief Start worker threads for web server.
        virtual bool threadStart(uint32_t timeout_ms) noexcept override;

        /// @brief Stop worker threads for web server.
        virtual bool threadStop(uint32_t timeout_ms) noexcept override;

        virtual ISerializableModule::SaveData save() noexcept override
        {
            return ISerializableModule::SaveData { .supports_saving_ = false };
        }

        virtual bool load(ISerializableModule::SaveData data) noexcept override
        {
            return true;
        }

    private:
        // Read and parse data_dir/config.txt into server_parameters_.
        // Returns true on success (all required keys present, numeric
        // conversion successful and referenced files exist). Does not
        // throw; failures result in returning false.
        bool parseConfigFile();
        std::vector<std::string> makeArgs();

        struct ServerParameters {
            std::string docroot;
            uint16_t port_http{0};
            uint16_t port_https{0};
            std::string ssl_certificate_path;
            std::string ssl_private_key_path;
            std::string ssl_tmp_dh_path;
            std::string wt_config_path;
        };

        bool valid_;

        ServerParameters server_parameters_;
        std::unique_ptr<Wt::WServer> w_server_;
        
        webapp::FrontendState frontend_state_;
    };
}