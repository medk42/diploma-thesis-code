#include "demo_usecase_2.h"
#include "module_helpers/usecase_wrapper/serialization_helper.h"
#include "module_helpers/camera_messages/messages.h"

#include "websocketpp/base64.hpp"

#include <vector>
#include <string_view>

using namespace aergo::default_modules::demo_usecase_2;
using namespace aergo::module;
using namespace aergo::module::helpers::usecase_wrapper;

using json = nlohmann::json;
namespace cm = aergo::module::helpers::camera_messages;


std::expected<void, uw::helper::ErrorInfo> DemoUsecase2::createCommandFromParameters(
    const uw::p_desc::ParameterList& auto_parameters,
    const uw::p_desc::ParameterList& required_parameters,
    const uw::p_desc::ParameterList& advanced_parameters,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& auto_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& required_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
    nlohmann::json& out_command_json
)
{
    std::vector<cm::CameraMessage> camera_messages;
    std::vector<std::vector<uint8_t>> image_blobs;

    for (const auto& value : auto_parameter_values[0])
    {
        cm::CameraMessage camera_message;
        std::vector<std::vector<uint8_t>> blobs;

        if (!readMessageDataAs(value.value_, camera_message, &blobs))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "DemoUsecase2: Failed to read camera image from auto parameters."));
        }

        if (blobs.size() < 1)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "DemoUsecase2: Expected at least one blob per image."));
        }

        camera_messages.push_back(camera_message);
        image_blobs.emplace_back(std::move(blobs[0])); // only one blob per image
    }

    json command_json;
    
    command_json["image_count"] = static_cast<int64_t>(camera_messages.size());
    command_json["images"] = json::array();
    for (size_t i = 0; i < camera_messages.size(); ++i)
    {
        const auto& data = image_blobs[i];

        json image_json;
        image_json["version"] = camera_messages[i].version_;
        image_json["timestamp_us"] = camera_messages[i].timestamp_us_;
        image_json["image_data"] = websocketpp::base64_encode(reinterpret_cast<unsigned char const*>(data.data()), data.size());

        command_json["images"].push_back(image_json);
    }

    out_command_json = command_json;
    
    return std::expected<void, uw::helper::ErrorInfo>{}; // no parameters to set up in this usecase
}



std::expected<void, uw::helper::ErrorInfo> DemoUsecase2::validateParameters(const nlohmann::json& command_json)
{
    if (!command_json.contains("image_count") || !command_json["image_count"].is_number_integer())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "DemoUsecase2: command JSON missing 'image_count' integer."));
    }

    if (!command_json.contains("images") || !command_json["images"].is_array())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "DemoUsecase2: command JSON missing 'images' array."));
    }

    const int64_t image_count = command_json["image_count"].get<int64_t>();
    if (command_json["images"].size() != static_cast<size_t>(image_count))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "DemoUsecase2: 'images' array size does not match 'image_count'."));
    }

    for (const auto& image_json : command_json["images"])
    {
        if (!image_json.contains("version") || !image_json["version"].is_number_integer())
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "DemoUsecase2: each image must contain 'width' integer."));
        }

        if (!image_json.contains("timestamp_us") || !image_json["timestamp_us"].is_number_integer())
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "DemoUsecase2: each image must contain 'height' integer."));
        }

        if (!image_json.contains("image_data") || !image_json["image_data"].is_string())
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "DemoUsecase2: each image must contain 'image_data' string."));
        }
    }

    // all checks passed
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> DemoUsecase2::runProgram(
    const nlohmann::json& command_json, 
    bool simulated
)
{
    const int64_t image_count = command_json["image_count"].get<int64_t>();

    log(logging::LogType::INFO, "DemoUsecase2: Running program with " + std::to_string(image_count) + " images. Simulated: " + (simulated ? "true" : "false"));

    if (image_count > 3)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "DemoUsecase2: This demo usecase only supports up to 3 images."));
    }

    for (const auto& image_json : command_json["images"])
    {
        const int64_t version = image_json["version"].get<int64_t>();
        const int64_t timestamp_us = image_json["timestamp_us"].get<int64_t>();
        const std::string image_data_base64 = image_json["image_data"].get<std::string>();

        log(logging::LogType::INFO, "    DemoUsecase2: Processing image of version " + std::to_string(version) + ", from timestamp " + std::to_string(timestamp_us) + "us, base64 size: " + std::to_string(image_data_base64.size()));
        
        if (!simulated)
        {
            // In a real usecase, here we would process the image data, e.g., run computer vision algorithms.
            log(logging::LogType::INFO, "        DemoUsecase2: (Real mode) Processing image...");
            
            // Decode base64 (not used further in this demo)
            std::string image_data_decoded = websocketpp::base64_decode(image_data_base64);
            log(logging::LogType::INFO, "        DemoUsecase2: Decoded image data size: " + std::to_string(image_data_decoded.size()));

            cm::BlobHeader blob_header;
            cm::ImageHeader image_header;
            if (!cm::readBlobHeader(reinterpret_cast<std::byte*>(image_data_decoded.data()), image_data_decoded.size(), blob_header))
            {
                return std::unexpected(uw::helper::ErrorInfo::WithDetails(9, "DemoUsecase2: Failed to read BlobHeader from decoded image data."));
            }
            if (!cm::readImageHeader(reinterpret_cast<std::byte*>(image_data_decoded.data()), image_data_decoded.size(), 0, image_header))
            {
                return std::unexpected(uw::helper::ErrorInfo::WithDetails(10, "DemoUsecase2: Failed to read ImageHeader from decoded image data."));
            }

            log(logging::LogType::INFO, "        DemoUsecase2: Image size: " + std::to_string(image_header.width_) + "x" + std::to_string(image_header.height_) + ", data offset: " + std::to_string(image_header.data_offset_) + ", stride: " + std::to_string(blob_header.stride_) + ", format: " + std::to_string(static_cast<uint32_t>(blob_header.format_)));
            log(logging::LogType::INFO, "        DemoUsecase2: Calculating mean color...");

            double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0;
            for (int64_t h = 0; h < image_header.height_; ++h)
            {
                for (int64_t w = 0; w < image_header.width_; ++w)
                {
                    size_t index = h * image_header.width_ + w;
                    size_t str_index = image_header.data_offset_ + index * 3; // assuming 3 bytes per pixel (RGB)
                    if (str_index + 2 < image_data_decoded.size())
                    {
                        sum_b += static_cast<unsigned char>(image_data_decoded[str_index]);
                        sum_g += static_cast<unsigned char>(image_data_decoded[str_index + 1]);
                        sum_r += static_cast<unsigned char>(image_data_decoded[str_index + 2]);
                    }
                    else
                    {
                        return std::unexpected(uw::helper::ErrorInfo::WithDetails(9, "DemoUsecase2: Decoded image data size is smaller than expected, calculated " + std::to_string(image_header.width_ * image_header.height_ * 3 + image_header.data_offset_) + ", actual " + std::to_string(image_data_decoded.size()) + "."));
                    }
                }
            }
            double mean_r = sum_r / (image_header.width_ * image_header.height_);
            double mean_g = sum_g / (image_header.width_ * image_header.height_);
            double mean_b = sum_b / (image_header.width_ * image_header.height_);
            log(logging::LogType::INFO, "        DemoUsecase2: Mean color - R: " + std::to_string(mean_r) + ", G: " + std::to_string(mean_g) + ", B: " + std::to_string(mean_b));
        }
        else
        {
            log(logging::LogType::INFO, "        DemoUsecase2: (Simulated mode) Skipping image processing.");
        }
    }

    log(logging::LogType::INFO, "DemoUsecase2: Program completed.");

    return std::expected<void, uw::helper::ErrorInfo>{};
}