#pragma once

#include <cstdint>

namespace aergo::default_modules::frontend_module::message_types
{
    /// @brief Simple structure to hold image data. Expects data to be continuous (no gaps between rows) and in BGR format.
    struct ImageHeader
    {
        uint16_t width_;
        uint16_t height_;
    };

    constexpr const char* image_bgr_channel_type = "image_bgr/v1:struct{width:uint16,height:uint16}+blob{width*height*3}";
}