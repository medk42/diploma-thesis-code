#pragma once

#include <cstdint>

namespace aergo::default_modules::camera_calibration_module
{
    /// @brief Simple structure to hold image data. Expects data to be continuous (no gaps between rows) and in BGR format.
    struct ImageHeader
    {
        uint16_t width_;
        uint16_t height_;
    };

    struct CalibratedImageHeader
    {
        ImageHeader image_header_; // header of the image
        double camera_matrix_[9]; // 3x3 row-major camera matrix
        double distortion_coefficients_[5]; // distortion coefficients: k1, k2, p1, p2, k3
    };
}