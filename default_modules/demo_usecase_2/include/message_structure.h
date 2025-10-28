#pragma once

#include <cstdint>

namespace aergo::default_modules::demo_usecase_2
{
    struct ImageHeader
    {
        uint16_t width_;
        uint16_t height_;
    };

    inline constexpr char image_publish_producer_channel_type[] = "image_bgr/v1:struct{width:uint16,height:uint16}+blob{width*height*3}";
}