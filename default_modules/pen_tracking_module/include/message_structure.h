#pragma once

#include <cstdint>

#include "module_common/module_interface_.h"

namespace aergo::default_modules::pen_tracking_module
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

    struct Pen3DPose
    {
        double tvec_[3]; // x, y, z in meters
        double rvec_[3]; // quaternion x, y, z, w
    };

    constexpr aergo::module::communication_channel::Producer pen_3d_pose_publish_producer { 
        .channel_type_identifier_ = "pen_3d_pose/v1:struct{tvec:double[3],rvec:double[3]}",
        .display_name_ = "Calibrated Camera Frame", 
        .display_description_ = "Output channel for calibrated camera frames in 8-bit BGR format with 3 channels, extended with camera calibration data (camera matrix and distortion coefficients) for further OpenCV processing.",
    };

    constexpr const char* image_bgr_calib_data_channel_type = "image_bgr+calib_data/v1:struct{image_header:struct{width:uint16,height:uint16},camera_matrix:double[9],distortion_coefficients:double[5]}+blob{width*height*3}";
}