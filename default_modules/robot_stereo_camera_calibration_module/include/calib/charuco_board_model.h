#pragma once

#include <opencv2/aruco.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <vector>
#include <stdexcept>

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    class CharucoBoardModel
    {
    public:
        struct Params
        {
            int rows{8};
            int cols{12};
            float squareLength{0.024f};
            float markerLength{0.018f};
            int dictionary{cv::aruco::DICT_4X4_100};
            bool useLegacyPattern{true};
        };

        CharucoBoardModel() : CharucoBoardModel(Params{}) {}

        static CharucoBoardModel Create(const Params& p);

        const cv::aruco::Dictionary& dictionary() const;
        const cv::Ptr<cv::aruco::CharucoBoard>& board() const;

        std::vector<cv::Point3f> boardPointsFromIds(const std::vector<int>& ids) const;

        int rows() const;
        int cols() const;
        float squareLength() const;
        float markerLength() const;

    private:
        explicit CharucoBoardModel(const Params& p);

        Params params_;
        cv::aruco::Dictionary dict_;
        cv::Ptr<cv::aruco::CharucoBoard> board_;
    };
}
