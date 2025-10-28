#pragma once

#include <vector>
#include <string>
#include <random>
#include <cstdint>
#include <tuple>

#include "module_helpers/parameter_description/parameter_description.h"

using namespace aergo::module::helpers::parameter_description;

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{

    // assume your enums/structs are already visible here

    static uint32_t rnd_u32()
    {
        static thread_local std::mt19937 rng{std::random_device{}()};
        return std::uniform_int_distribution<uint32_t>{1, 0xFFFFFFFFu}(rng);
    }

    static ParameterDescription customParam(std::string name,
                                            std::string desc,
                                            CustomChannelType ctype,
                                            bool as_list = false,
                                            uint16_t lmin = 0,
                                            uint16_t lmax = 0)
    {
        ParameterDescription p;
        p.type_ = ParameterType::CUSTOM;
        p.param_name_ = std::move(name);
        p.param_desc_ = std::move(desc);
        p.custom_channel_type_ = ctype;
        p.custom_channel_id_ = rnd_u32();
        p.as_list_ = as_list;
        p.list_size_min_ = lmin;
        p.list_size_max_ = lmax;
        return p;
    }

    std::tuple<std::vector<ParameterDescription>, std::vector<ParameterDescription>, std::vector<ParameterDescription>> generateParams()
    {
        // 1) CUSTOM-only list: some singletons, some lists.
        //    One list has min 0, max 3; another has exactly 4.
        std::vector<ParameterDescription> params_custom {
            customParam(
                "Sensor event stream",
                "Binary sensor payloads subscribed from the field bus.",
                CustomChannelType::SUBSCRIBE, /*as_list*/false),

            customParam(
                "RPC command uplink",
                "Requests to the device control plane, one payload per call.",
                CustomChannelType::REQUEST, /*as_list*/false),

            customParam(
                "Probe topics",
                "Optional set of topic names for diagnostic sampling.",
                CustomChannelType::SUBSCRIBE,
                /*as_list*/true, /*min*/0, /*max*/3),

            customParam(
                "Batch command frames",
                "Fixed batch of control frames issued atomically.",
                CustomChannelType::REQUEST,
                /*as_list*/true, /*min*/4, /*max*/4)
        };

        // 2) Non-CUSTOM only. Mixed lists/non-lists. Include ENUMs, sliders and non-sliders.
        //    Defaults are optional here; included on some, omitted on others.
        std::vector<ParameterDescription> params_standard_a;

        {
            // BOOL (non-list) with default
            ParameterDescription p;
            p.type_ = ParameterType::BOOL;
            p.param_name_ = "Enable logging";
            p.param_desc_ = "Turn on diagnostic logging for the subsystem.";
            p.default_value_ = "1"; // true
            params_standard_a.push_back(std::move(p));
        }
        {
            // LONG (non-list) with limits, no default
            ParameterDescription p;
            p.type_ = ParameterType::LONG;
            p.param_name_ = "Max retries";
            p.param_desc_ = "Upper bound on consecutive retry attempts.";
            p.limit_min_ = true;  p.min_value_long_ = 0;
            p.limit_max_ = true;  p.max_value_long_ = 10;
            // default omitted intentionally
            params_standard_a.push_back(std::move(p));
        }
        {
            // DOUBLE (non-list) slider with limits, default provided
            ParameterDescription p;
            p.type_ = ParameterType::DOUBLE;
            p.param_name_ = "Learning rate";
            p.param_desc_ = "Gradient step size for optimizer.";
            p.limit_min_ = true;  p.min_value_double_ = 0.0;
            p.limit_max_ = true;  p.max_value_double_ = 1.0;
            p.as_slider_ = true;
            p.default_value_ = "0.1";
            params_standard_a.push_back(std::move(p));
        }
        {
            // STRING (non-list) with default
            ParameterDescription p;
            p.type_ = ParameterType::STRING;
            p.param_name_ = "Output path";
            p.param_desc_ = "Filesystem path for generated artifacts.";
            p.default_value_ = "/tmp/out";
            params_standard_a.push_back(std::move(p));
        }
        {
            // ENUM (non-list) with default index
            ParameterDescription p;
            p.type_ = ParameterType::ENUM;
            p.param_name_ = "Color scheme";
            p.param_desc_ = "UI color preference.";
            p.enum_values_ = {"Light", "Dark", "System"};
            p.default_value_ = "2"; // "System"
            params_standard_a.push_back(std::move(p));
        }
        {
            // BOOL (list) with defaults for each entry
            ParameterDescription p;
            p.type_ = ParameterType::BOOL;
            p.param_name_ = "Feature flags";
            p.param_desc_ = "Per-flag enablement toggles.";
            p.as_list_ = true; p.list_size_min_ = 2; p.list_size_max_ = 5;
            p.default_value_ = "0"; // each entry defaults false
            params_standard_a.push_back(std::move(p));
        }
        {
            // LONG (list) with limits; no default
            ParameterDescription p;
            p.type_ = ParameterType::LONG;
            p.param_name_ = "Shard sizes";
            p.param_desc_ = "Partition sizes in megabytes.";
            p.as_list_ = true; p.list_size_min_ = 1; p.list_size_max_ = 3;
            p.limit_min_ = true; p.min_value_long_ = 1;
            p.limit_max_ = true; p.max_value_long_ = 2048;
            // default omitted intentionally
            params_standard_a.push_back(std::move(p));
        }
        {
            // DOUBLE (list) slider, with limits; default omitted
            ParameterDescription p;
            p.type_ = ParameterType::DOUBLE;
            p.param_name_ = "Alarm thresholds";
            p.param_desc_ = "Upper bounds per monitored metric.";
            p.as_list_ = true; p.list_size_min_ = 0; p.list_size_max_ = 5;
            p.limit_min_ = true; p.min_value_double_ = 0.0;
            p.limit_max_ = true; p.max_value_double_ = 100.0;
            p.as_slider_ = true;
            // default omitted intentionally
            params_standard_a.push_back(std::move(p));
        }
        {
            // ENUM (list) with default index
            ParameterDescription p;
            p.type_ = ParameterType::ENUM;
            p.param_name_ = "Channel mode";
            p.param_desc_ = "Per-channel access mode.";
            p.enum_values_ = {"Off", "Read", "Write"};
            p.as_list_ = true; p.list_size_min_ = 3; p.list_size_max_ = 3;
            p.default_value_ = "1"; // "Read" for every entry
            params_standard_a.push_back(std::move(p));
        }

        // 3) Non-CUSTOM only. Larger set. Must all have defaults.
        //    Together with set (2), this covers all types in both list and non-list forms.
        std::vector<ParameterDescription> params_standard_b;

        {
            // BOOL non-list
            ParameterDescription p;
            p.type_ = ParameterType::BOOL;
            p.param_name_ = "Dry run";
            p.param_desc_ = "Execute without side-effects.";
            p.default_value_ = "0";
            params_standard_b.push_back(std::move(p));
        }
        {
            // BOOL list
            ParameterDescription p;
            p.type_ = ParameterType::BOOL;
            p.param_name_ = "Module toggles";
            p.param_desc_ = "Toggle specific modules on startup.";
            p.as_list_ = true; p.list_size_min_ = 1; p.list_size_max_ = 4;
            p.default_value_ = "1";
            params_standard_b.push_back(std::move(p));
        }
        {
            // LONG non-list with limits
            ParameterDescription p;
            p.type_ = ParameterType::LONG;
            p.param_name_ = "Worker threads";
            p.param_desc_ = "Number of worker threads to spawn.";
            p.limit_min_ = true; p.min_value_long_ = 1;
            p.limit_max_ = true; p.max_value_long_ = 128;
            p.default_value_ = "8";
            params_standard_b.push_back(std::move(p));
        }
        {
            // LONG list with limits
            ParameterDescription p;
            p.type_ = ParameterType::LONG;
            p.param_name_ = "Affinity CPU set";
            p.param_desc_ = "Pinned CPU indices.";
            p.as_list_ = true; p.list_size_min_ = 0; p.list_size_max_ = 16;
            p.limit_min_ = true; p.min_value_long_ = 0;
            p.limit_max_ = true; p.max_value_long_ = 255;
            p.default_value_ = "2";
            params_standard_b.push_back(std::move(p));
        }
        {
            // DOUBLE non-list, slider
            ParameterDescription p;
            p.type_ = ParameterType::DOUBLE;
            p.param_name_ = "Momentum";
            p.param_desc_ = "Accelerates gradient descent in relevant directions.";
            p.limit_min_ = true; p.min_value_double_ = 0.0;
            p.limit_max_ = true; p.max_value_double_ = 0.999;
            p.as_slider_ = true;
            p.default_value_ = "0.75";
            params_standard_b.push_back(std::move(p));
        }
        {
            // DOUBLE list, non-slider
            ParameterDescription p;
            p.type_ = ParameterType::DOUBLE;
            p.param_name_ = "Calibration gains";
            p.param_desc_ = "Gain factors applied per channel.";
            p.as_list_ = true; p.list_size_min_ = 2; p.list_size_max_ = 8;
            p.limit_min_ = true; p.min_value_double_ = 0.0;
            p.limit_max_ = true; p.max_value_double_ = 10.0;
            p.as_slider_ = false;
            p.default_value_ = "1.0";
            params_standard_b.push_back(std::move(p));
        }
        {
            // STRING non-list
            ParameterDescription p;
            p.type_ = ParameterType::STRING;
            p.param_name_ = "Username";
            p.param_desc_ = "Account name used for authentication.";
            p.default_value_ = "operator";
            params_standard_b.push_back(std::move(p));
        }
        {
            // STRING list with fixed size
            ParameterDescription p;
            p.type_ = ParameterType::STRING;
            p.param_name_ = "DNS servers";
            p.param_desc_ = "Primary and secondary DNS servers.";
            p.as_list_ = true; p.list_size_min_ = 2; p.list_size_max_ = 2;
            p.default_value_ = "0.0.0.0";
            params_standard_b.push_back(std::move(p));
        }
        {
            // ENUM non-list
            ParameterDescription p;
            p.type_ = ParameterType::ENUM;
            p.param_name_ = "Compression";
            p.param_desc_ = "On-disk compression algorithm.";
            p.enum_values_ = {"None", "LZ4", "Zstd"};
            p.default_value_ = "2"; // Zstd
            params_standard_b.push_back(std::move(p));
        }
        {
            // ENUM list fixed size
            ParameterDescription p;
            p.type_ = ParameterType::ENUM;
            p.param_name_ = "Privilege level per role";
            p.param_desc_ = "Assigned privilege for each role slot.";
            p.enum_values_ = {"Guest", "User", "Admin"};
            p.as_list_ = true; p.list_size_min_ = 3; p.list_size_max_ = 3;
            p.default_value_ = "1"; // User
            params_standard_b.push_back(std::move(p));
        }

        return std::make_tuple(params_custom, params_standard_a, params_standard_b);
    }
}