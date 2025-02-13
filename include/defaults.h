#ifndef AERGO_DEFAULTS_H
#define AERGO_DEFAULTS_H

#include <opencv2/objdetect/aruco_detector.hpp>

namespace aergo::defaults
{
    namespace charucoboard
    {
        const int ROW_COUNT = 8;
        const int COL_COUNT = 12;
        const float SQUARE_LENGTH = 0.024f;
        const float MARKER_LENGTH = 0.018f;
        const cv::aruco::Dictionary DICTIONARY = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_100);
        const bool LEGACY_PATTERN = true;
    };

    namespace pen
    {
        const cv::aruco::Dictionary DICTIONARY = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_100);
        const int PEN_FIXED_MARKER = 92;
    };
};

#endif // AERGO_DEFAULTS_H