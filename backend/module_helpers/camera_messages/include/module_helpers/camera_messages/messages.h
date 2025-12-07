#pragma once

#include <cstdint>
#include <chrono>

#include "module_common/module_interface_.h"

// Camera message structures and helpers
// Defines message format for camera images
// Camera message will be sent as a CameraMessage struct followed by one or more image blobs
// Each image blob will contain one or more images, preceded by a BlobHeader and ImageHeaders
// This allows sending multiple images in a single message, e.g. for stereo camera setups
// The structure of each blob is:
//    [BlobHeader][image_count_ * ImageHeader][Binary image data...]   <== single binary blob per image blob
// Multiple binary blobs can be sent as separate blobs in the message

namespace aergo::module::helpers::camera_messages
{
    uint64_t constexpr CAMERA_MESSAGE_VERSION = 1;


    inline int64_t microsSteady()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }


    struct CameraMessage
    {
        CameraMessage()
            : version_(CAMERA_MESSAGE_VERSION), timestamp_us_(microsSteady())
        {}

        uint64_t version_;      // Version of the camera message format
        int64_t timestamp_us_; // Timestamp in microseconds (steady clock)
    };


    enum class ImageFormat : uint32_t
    {
        BGR8 = 0,    // 3-channel 8-bit BGR format
        BGRA8 = 1    // 4-channel 8-bit BGRA format
    };


    /// @brief Header for image blob data, preceding one or more images.
    /// Blob structure will be:
    ///    [BlobHeader][image_count_ * ImageHeader][Binary image data...]
    /// This allows packing multiple images into a single blob, e.g., for stereo camera setups.
    struct BlobHeader
    {
        uint32_t stride_;      // Stride (bytes per row)
        ImageFormat format_;   // Image format
        uint32_t image_count_; // Number of images in the blob (for multi-image blobs)
    };


    struct ImageHeader
    {
        uint32_t width_;       // Width of the image in pixels
        uint32_t height_;      // Height of the image in pixels
        uint64_t data_offset_; // Offset to the image data within the blob
    };
    
    
    inline size_t bytesPerPixel(ImageFormat format) noexcept
    {
        switch (format)
        {
            case ImageFormat::BGR8:
                return 3;
            case ImageFormat::BGRA8:
                return 4;
            default:
                return 0;
        }
    }


    /// @brief Calculate header size for given number of images in the blob.
    /// @param image_count Number of images in the blob.
    /// @return Required size in bytes.
    inline size_t headerSizeForImages(size_t image_count) noexcept
    {
        return sizeof(BlobHeader) + image_count * sizeof(ImageHeader);
    }


    /// @brief Write blob header into the given data buffer. Expects the buffer to contain data in structure defined by BlobHeader.
    /// @return true on success, false if buffer is too small.
    inline bool writeBlobHeader(std::byte* data, size_t size, const BlobHeader& blob_header) noexcept
    {
        if (size < sizeof(BlobHeader))
        {
            return false;
        }

        std::memcpy(data, &blob_header, sizeof(BlobHeader));
        return true;
    }


    /// @brief Write image header into the given data buffer. Expects the buffer to contain data in structure defined by BlobHeader.
    /// Header will be written at position defined by header_id (0 .. image_count - 1). Pass pointer to BlobHeader at start of data.
    /// @return true on success, false if buffer is too small or header_id is out of range.
    inline bool writeImageHeader(std::byte* data, size_t size, size_t header_id, const ImageHeader& image_header) noexcept
    {
        if (size < sizeof(BlobHeader) + (header_id + 1) * sizeof(ImageHeader))
        {
            return false;
        }

        std::memcpy(data + sizeof(BlobHeader) + header_id * sizeof(ImageHeader), &image_header, sizeof(ImageHeader));
        return true;
    }


    /// @brief Read blob header from the given data buffer. Expects the buffer to contain data in structure defined by BlobHeader.
    /// @return true on success, false if buffer is too small.
    inline bool readBlobHeader(const std::byte* data, size_t size, BlobHeader& out_blob_header) noexcept
    {
        if (size < sizeof(BlobHeader))
        {
            return false;
        }

        std::memcpy(&out_blob_header, data, sizeof(BlobHeader));
        return true;
    }


    /// @brief Read image header from the given data buffer. Expects the buffer to contain data in structure defined by BlobHeader.
    /// Header will be read at position defined by header_id (0 .. image_count - 1). Pass pointer to BlobHeader at start of data.
    /// @return true on success, false if buffer is too small or header_id is out of range.
    inline bool readImageHeader(const std::byte* data, size_t size, size_t header_id, ImageHeader& out_image_header) noexcept
    {
        BlobHeader blob_header;
        if (!readBlobHeader(data, size, blob_header))
        {
            return false;
        }
        if (header_id >= blob_header.image_count_)
        {
            return false;
        }
        if (size < sizeof(BlobHeader) + (header_id + 1) * sizeof(ImageHeader))
        {
            return false;
        }
        
        std::memcpy(&out_image_header, data + sizeof(BlobHeader) + header_id * sizeof(ImageHeader), sizeof(ImageHeader));
        return true;
    }


    /// @brief Validate that the blob data contains valid headers and image data fits in blob size.
    /// If true, all headers and images can be safely read.
    /// @return true if valid, false otherwise.
    inline bool isBlobValid(std::byte* data, size_t size)
    {
        BlobHeader blob_header;
        if (!readBlobHeader(data, size, blob_header))
        {
            return false;
        }

        for (size_t i = 0; i < blob_header.image_count_; ++i)
        {
            ImageHeader image_header;
            if (!readImageHeader(data, size, i, image_header))
            {
                return false;
            }
            // check that image data fits in blob
            if (image_header.data_offset_ + ((image_header.height_ - 1) * blob_header.stride_) + image_header.width_ * bytesPerPixel(blob_header.format_) > size)
            {
                return false;
            }
        }

        return true;
    }


    static constexpr aergo::module::communication_channel::Producer camera_image_producer
    {
        .channel_type_identifier_ = "camera_image/v1:struct{version:uint64,timestamp_us:int64}+blob{BlobHeader+image_count*ImageHeader+image_data}",
        .display_name_ = "Camera Image",
        .display_description_ = "Camera image output, provides raw image data captured from the camera in BGR or BGRA format. May contain multiple images in a single blob for multi-camera setups."
    };
    
    static constexpr aergo::module::communication_channel::Consumer camera_image_consumer
    {
        .count_ = aergo::module::communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = camera_image_producer.channel_type_identifier_,
        .display_name_ = camera_image_producer.display_name_,
        .display_description_ = "Camera image input, receives raw image data captured from the camera in BGR or BGRA format. May contain multiple images in a single blob for multi-camera setups.",
        .prioritized_ = false,
        .message_queue_capacity_ = 1 // we only need the latest image
    };
}