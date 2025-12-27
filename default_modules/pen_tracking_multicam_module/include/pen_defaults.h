#pragma once

#include <array>
#include <opencv2/aruco.hpp>
#include <map>

#include "detection/marker_detection.h"
#include "detection/multicam_pose_estimator.h"

namespace aergo::default_modules::pen_tracking_multicam_module::defaults
{
    inline constexpr std::array<int, 5> markerIdsToDetect = { 
        17, 38, 47, 56, 58 
    };

    inline constexpr MarkerDetector::RefineMode refineMode = 
        MarkerDetector::RefineMode::SUBPIXEL;

    inline constexpr int windowSizeMultiple = 9;

    inline const char* SERVICE_UUID = "2bfae565-df4e-45b6-b1fa-a6f75c1be2b3";
    inline const char* CHARACTERISTIC_UUID = "e76d106d-a549-4b3a-afbd-8879582943fe";

    inline const cv::aruco::Dictionary& dictionary() {
        static const cv::aruco::Dictionary dict = 
            cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_100);
        return dict;
    }

    inline std::map<int, MulticamPoseEstimator::MarkerData> markersData() {
        double markerSizeM = 0.036; // 36 mm markers

        return std::map<int, MulticamPoseEstimator::MarkerData> {
            {17, MulticamPoseEstimator::MarkerData(markerSizeM, pu::SE3{
                .R = cv::Matx33d(
                    -1, 0, 0,
                    0, 0,-1,
                    0,-1, 0
                ),
                .t = cv::Vec3d(0, 0, -0.024)
            })},

            {38, MulticamPoseEstimator::MarkerData(markerSizeM, pu::SE3{
                .R = cv::Matx33d(
                    1, 0, 0,
                    0, 0,-1,
                    0, 1, 0
                ),
                .t = cv::Vec3d(0, 0, -0.024)
            })},

            {47, MulticamPoseEstimator::MarkerData(markerSizeM, pu::SE3{
                .R = cv::Matx33d(
                    0,-1, 0,
                    -1, 0, 0,
                    0, 0,-1
                ),
                .t = cv::Vec3d(0, 0, -0.024)
            })},

            {56, MulticamPoseEstimator::MarkerData(markerSizeM, pu::SE3{
                .R = cv::Matx33d(
                    0, 1, 0,
                    0, 0,-1,
                    -1, 0, 0
                ),
                .t = cv::Vec3d(0, 0, -0.024)
            })},

            {58, MulticamPoseEstimator::MarkerData(markerSizeM, pu::SE3{
                .R = cv::Matx33d(
                    0,-1, 0,
                    0, 0,-1,
                    1, 0, 0
                ),
                .t = cv::Vec3d(0, 0, -0.024)
            })},
        };
    }

    inline pu::SE3 T_pen_tip()
    {
        return pu::SE3{
            .R = cv::Matx33d::eye(),
            .t = cv::Vec3d(0, 0, 0.154)
        };
    }
}