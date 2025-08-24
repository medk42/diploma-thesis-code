#pragma once

#include <vector>
#include <string>
#include <array>
#include <sstream>
#include "module_interface_.h"

namespace aergo::module
{

    class Metrics {
    public:
        struct ChannelStats {
            uint64_t received_count = 0;
            uint64_t dropped_module_count = 0;
            uint64_t dropped_full_count = 0;
            uint64_t deleted_drop_queue_first_count = 0;
            uint64_t deleted_replace_queue_count = 0;
            std::array<uint64_t, 5> ingress_decision_counts{}; // One for each IngressDecision
            uint64_t queue_empty_count = 0;
            uint64_t queue_one_count = 0;
            uint64_t queue_multi_count = 0;
            std::string channel_name;
            std::string channel_type;
        };

        Metrics(const aergo::module::ModuleInfo* module_info)
        {
            // Build stats vector for all channels (messages, requests, responses)
            size_t total_channels = module_info->subscribe_consumer_count_ +
                                module_info->response_producer_count_ +
                                module_info->request_consumer_count_;
            stats_.resize(total_channels);

            // Fill channel names/types
            size_t idx = 0;
            for (uint32_t i = 0; i < module_info->subscribe_consumer_count_; ++i, ++idx) {
                stats_[idx].channel_name = module_info->subscribe_consumers_[i].display_name_;
                stats_[idx].channel_type = "MESSAGE";
            }
            for (uint32_t i = 0; i < module_info->response_producer_count_; ++i, ++idx) {
                stats_[idx].channel_name = module_info->response_producers_[i].display_name_;
                stats_[idx].channel_type = "REQUEST";
            }
            for (uint32_t i = 0; i < module_info->request_consumer_count_; ++i, ++idx) {
                stats_[idx].channel_name = module_info->request_consumers_[i].display_name_;
                stats_[idx].channel_type = "RESPONSE";
            }
        }

        void printLogs(const aergo::module::logging::ILogger* log) {
            std::ostringstream oss;
            oss << "=== DLLModuleWrapper Metrics ===\n";
            for (size_t i = 0; i < stats_.size(); ++i) {
                const auto& s = stats_[i];
                oss << "[" << s.channel_type << "] " << s.channel_name << ":\n";
                oss << "  Received: " << s.received_count << "\n";
                oss << "  Dropped (module): " << s.dropped_module_count << "\n";
                oss << "  Dropped (full): " << s.dropped_full_count << "\n";
                oss << "  Deleted by DROP_QUEUE_FIRST: " << s.deleted_drop_queue_first_count << "\n";
                oss << "  Deleted by REPLACE_QUEUE: " << s.deleted_replace_queue_count << "\n";
                oss << "  Ingress Decisions: ";
                for (size_t j = 0; j < s.ingress_decision_counts.size(); ++j)
                    oss << s.ingress_decision_counts[j] << " ";
                oss << "\n";
                oss << "  Queue empty: " << s.queue_empty_count << "\n";
                oss << "  Queue one: " << s.queue_one_count << "\n";
                oss << "  Queue multi: " << s.queue_multi_count << "\n";
            }
            log->log(aergo::module::logging::LogType::INFO, oss.str().c_str());
        }
        
        void record(size_t idx, size_t queue_size, aergo::module::IModule::IngressDecision decision, bool queue_full) {
            
            if (queue_size == 0) stats_[idx].queue_empty_count++;
            else if (queue_size == 1) stats_[idx].queue_one_count++;
            else stats_[idx].queue_multi_count++;

            stats_[idx].received_count++;
            stats_[idx].ingress_decision_counts[(size_t)decision]++;
            if (!queue_full && decision == aergo::module::IModule::IngressDecision::DROP) stats_[idx].dropped_module_count++;
            if (queue_full && (decision == aergo::module::IModule::IngressDecision::DROP || decision == aergo::module::IModule::IngressDecision::ACCEPT)) stats_[idx].dropped_full_count++;
            if (decision == aergo::module::IModule::IngressDecision::ACCEPT_DROP_QUEUE_FIRST) stats_[idx].deleted_drop_queue_first_count++;
            if (decision == aergo::module::IModule::IngressDecision::ACCEPT_REPLACE_QUEUE) stats_[idx].deleted_replace_queue_count++;
        }

    private:
        std::vector<ChannelStats> stats_;
    };
}