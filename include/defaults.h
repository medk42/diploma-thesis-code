#ifndef AERGO_DEFAULTS_H
#define AERGO_DEFAULTS_H

#include <set>

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
        const int PEN_FIXED_MARKER_ID = 92;
        const std::set<int> USED_MARKER_IDS = {92, 93, 94, 95, 96, 97, 98, 99};
        const float MARKER_SIZE = 0.013f;
        const float IGNORE_MARKERS_ABOVE_ANGLE_DEG = 40;
    };
};

#endif // AERGO_DEFAULTS_H