#pragma once

#include <opencv2/objdetect/aruco_detector.hpp>

namespace aergo::default_modules::robot_stereo_camera_calibration_module::defaults::charucoboard
{
    inline const int ROW_COUNT = 8;
    inline const int COL_COUNT = 12;
    inline const float SQUARE_LENGTH = 0.024f;
    inline const float MARKER_LENGTH = 0.018f;
    inline const cv::aruco::Dictionary DICTIONARY = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_100);
    inline const bool LEGACY_PATTERN = true;
}
