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
        const float ORIGIN_TO_TIP_DISTANCE = 0.125746f;
        const std::string SERVICE_UUID = "2bfae565-df4e-45b6-b1fa-a6f75c1be2b3";
        const std::string CHARACTERISTIC_UUID = "e76d106d-a549-4b3a-afbd-8879582943fe";
        const int ACCEL_RANGE = 4;
        const int GYRO_RANGE = 500;
        
        
        inline std::vector<cv::Point3f> getMarkerPoints3d()
        {
            std::vector<cv::Point3f> marker_points = 
            {
                cv::Point3f(-MARKER_SIZE / 2,  MARKER_SIZE / 2, 0),
                cv::Point3f( MARKER_SIZE / 2,  MARKER_SIZE / 2, 0),
                cv::Point3f( MARKER_SIZE / 2, -MARKER_SIZE / 2, 0),
                cv::Point3f(-MARKER_SIZE / 2, -MARKER_SIZE / 2, 0)
            };

            return std::move(marker_points);
        }

        inline cv::aruco::ArucoDetector getArucoDetector()
        {
            cv::aruco::DetectorParameters detector_parameters;
            detector_parameters.cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;

            cv::aruco::ArucoDetector aruco_detector(
                defaults::pen::DICTIONARY, 
                detector_parameters
            );

            return std::move(aruco_detector);
        }
    };
};

#endif // AERGO_DEFAULTS_H