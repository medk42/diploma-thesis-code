#pragma once

#include "module_common/module_interface_.h"
#include "module_helpers/serialization_helper/serialization_helper.h"
#include <span>
#include <vector>

namespace aergo::module::helpers::scene_detection_helper
{
    uint64_t constexpr SCENE_DETECTION_MESSAGE_VERSION = 1;

    struct Pose
    {
        double x{0.0};              // X position in world coordinate system, in meters
        double y{0.0};              // Y position in world coordinate system, in meters
        double z{0.0};              // Z position in world coordinate system, in meters

        double qx{0.0};             // X component of orientation as a quaternion
        double qy{0.0};             // Y component of orientation as a quaternion
        double qz{0.0};             // Z component of orientation as a quaternion
        double qw{1.0};             // W component of orientation as a quaternion
    };

    struct RegisteredBox
    {
        uint64_t id{0};             // Box identifier
        double size_x{0.0};         // Box dimension in X direction, in meters
        double size_y{0.0};         // Box dimension in Y direction, in meters
        double size_z{0.0};         // Box dimension in Z direction, in meters
    };

    struct DetectedBox
    {
        uint64_t id{0};             // Box identifier (matches registered box ID)
        Pose pose{};                // Detected box pose in world coordinates (world <- box)
    };

    enum class ReqType : uint8_t
    {
        READ_REGISTRY,  // Request to read registered objects (returns list of RegisteredBox)
        READ_SCENE      // Request to perform scene detection (returns list of DetectedBox)
    };

    namespace serialization
    {
        namespace ser = aergo::module::helpers::serialization_helper::serialization;

        /// @brief Push pose (x, y, z, qx, qy, qz, qw) as 7 doubles into buffer
        inline void pushPose(std::vector<std::byte>& buf, const Pose& pose)
        {
            ser::push<double>(buf, pose.x);
            ser::push<double>(buf, pose.y);
            ser::push<double>(buf, pose.z);
            ser::push<double>(buf, pose.qx);
            ser::push<double>(buf, pose.qy);
            ser::push<double>(buf, pose.qz);
            ser::push<double>(buf, pose.qw);
        }

        /// @brief Push RegisteredBox (id, size_x, size_y, size_z) into buffer
        inline void pushRegisteredBox(std::vector<std::byte>& buf, const RegisteredBox& box)
        {
            ser::push<uint64_t>(buf, box.id);
            ser::push<double>(buf, box.size_x);
            ser::push<double>(buf, box.size_y);
            ser::push<double>(buf, box.size_z);
        }

        /// @brief Push DetectedBox (id, pose) into buffer
        inline void pushDetectedBox(std::vector<std::byte>& buf, const DetectedBox& box)
        {
            ser::push<uint64_t>(buf, box.id);
            pushPose(buf, box.pose);
        }
    }

    namespace deserialization
    {
        namespace des = aergo::module::helpers::serialization_helper::deserialization;

        /// @brief Read pose (x, y, z, qx, qy, qz, qw) as 7 doubles from buffer
        inline bool readPose(des::BufferReader& reader, Pose& pose)
        {
            if (!reader.read<double>(pose.x)) return false;
            if (!reader.read<double>(pose.y)) return false;
            if (!reader.read<double>(pose.z)) return false;
            if (!reader.read<double>(pose.qx)) return false;
            if (!reader.read<double>(pose.qy)) return false;
            if (!reader.read<double>(pose.qz)) return false;
            if (!reader.read<double>(pose.qw)) return false;
            return true;
        }

        /// @brief Read RegisteredBox (id, size_x, size_y, size_z) from buffer
        inline bool readRegisteredBox(des::BufferReader& reader, RegisteredBox& box)
        {
            if (!reader.read<uint64_t>(box.id)) return false;
            if (!reader.read<double>(box.size_x)) return false;
            if (!reader.read<double>(box.size_y)) return false;
            if (!reader.read<double>(box.size_z)) return false;
            return true;
        }

        /// @brief Read DetectedBox (id, pose) from buffer
        inline bool readDetectedBox(des::BufferReader& reader, DetectedBox& box)
        {
            if (!reader.read<uint64_t>(box.id)) return false;
            if (!readPose(reader, box.pose)) return false;
            return true;
        }
    }

    struct Request
    {
        uint64_t version{SCENE_DETECTION_MESSAGE_VERSION};  // Version of the request format
        ReqType req_type{};         // Request type (READ_REGISTRY or READ_SCENE)

        static Request readRegistry()
        {
            Request req;
            req.version = SCENE_DETECTION_MESSAGE_VERSION;
            req.req_type = ReqType::READ_REGISTRY;
            return req;
        }

        static Request readScene()
        {
            Request req;
            req.version = SCENE_DETECTION_MESSAGE_VERSION;
            req.req_type = ReqType::READ_SCENE;
            return req;
        }
    };

    struct Response
    {
        uint64_t version{SCENE_DETECTION_MESSAGE_VERSION};  // Version of the response format
        ReqType req_type{};         // Request type (READ_REGISTRY or READ_SCENE)
        uint32_t count{0};          // Number of registered/detected objects

        static Response registryResponse(std::span<const RegisteredBox> boxes, std::vector<std::byte>& blob_data_out)
        {
            Response resp;
            resp.version = SCENE_DETECTION_MESSAGE_VERSION;
            resp.req_type = ReqType::READ_REGISTRY;
            resp.count = static_cast<uint32_t>(boxes.size());

            serialization::ser::push<uint32_t>(blob_data_out, resp.count);
            for (const auto& box : boxes)
            {
                serialization::pushRegisteredBox(blob_data_out, box);
            }

            return resp;
        }

        static Response sceneResponse(std::span<const DetectedBox> boxes, std::vector<std::byte>& blob_data_out)
        {
            Response resp;
            resp.version = SCENE_DETECTION_MESSAGE_VERSION;
            resp.req_type = ReqType::READ_SCENE;
            resp.count = static_cast<uint32_t>(boxes.size());

            serialization::ser::push<uint32_t>(blob_data_out, resp.count);
            for (const auto& box : boxes)
            {
                serialization::pushDetectedBox(blob_data_out, box);
            }

            return resp;
        }

        bool parseRegistry(const void* blob_data, size_t blob_size, std::vector<RegisteredBox>& out_boxes) const
        {
            // Check request type
            if (req_type != ReqType::READ_REGISTRY)
            {
                return false;
            }

            out_boxes.clear();

            deserialization::des::BufferReader reader(blob_data, blob_size);

            // Read count
            uint32_t read_count = 0;
            if (!reader.read<uint32_t>(read_count))
            {
                return false; // data too short
            }

            // Verify count matches
            if (read_count != count)
            {
                return false;
            }

            // Read all boxes
            out_boxes.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                RegisteredBox box;
                if (!deserialization::readRegisteredBox(reader, box))
                {
                    return false; // data too short
                }
                out_boxes.push_back(box);
            }

            // Check if all data was consumed
            if (reader.remaining() != 0)
            {
                return false; // data remains
            }

            return true;
        }

        bool parseScene(const void* blob_data, size_t blob_size, std::vector<DetectedBox>& out_boxes) const
        {
            // Check request type
            if (req_type != ReqType::READ_SCENE)
            {
                return false;
            }

            out_boxes.clear();

            deserialization::des::BufferReader reader(blob_data, blob_size);

            // Read count
            uint32_t read_count = 0;
            if (!reader.read<uint32_t>(read_count))
            {
                return false; // data too short
            }

            // Verify count matches
            if (read_count != count)
            {
                return false;
            }

            // Read all boxes
            out_boxes.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                DetectedBox box;
                if (!deserialization::readDetectedBox(reader, box))
                {
                    return false; // data too short
                }
                out_boxes.push_back(box);
            }

            // Check if all data was consumed
            if (reader.remaining() != 0)
            {
                return false; // data remains
            }

            return true;
        }
    };

    struct Message
    {
        uint64_t version{SCENE_DETECTION_MESSAGE_VERSION};  // Version of the message format
        uint32_t count{0};         // Number of detected objects
    };

    // Request: message contains ReqType as data (1 byte), no blobs
    // Response: message contains ReqType as data (1 byte, copied from request), 1 blob with data
    //    ReqType::READ_REGISTRY: blob contains array of RegisteredBox (count + RegisteredBox[])
    //    ReqType::READ_SCENE: blob contains array of DetectedBox (count + DetectedBox[])

    constexpr aergo::module::communication_channel::Consumer scene_detection_request_consumer = {
        .count_ = aergo::module::communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = "scene_detection_req/v1:struct{req_type:uint8}",
        .display_name_ = "Scene Detection Request",
        .display_description_ = "Request channel for scene detection messages (read registry or perform scene detection).",
        .prioritized_ = false,
        .message_queue_capacity_ = 4
    };

    constexpr aergo::module::communication_channel::Producer scene_detection_response_producer = {
        .channel_type_identifier_ = "scene_detection_resp/v1:struct{req_type:uint8}+blob{count+data[]}",
        .display_name_ = "Scene Detection Response",
        .display_description_ = "Response channel for scene detection requests (registry list or detected objects list).",
        .prioritized_ = false,
        .message_queue_capacity_ = 4
    };
}

