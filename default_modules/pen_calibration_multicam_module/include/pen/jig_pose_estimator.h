#pragma once
#include "calib/types.h"
#include <opencv2/aruco.hpp>

namespace aergo::default_modules::pen_calibration_multicam_module::pen {

    // Estimates camera <- world per frame from a single jig ArUco tag.
    // World is the jig tag frame (W = J).
    class JigPoseEstimator {
    public:
        JigPoseEstimator(int dict_id, double tag_size_m, int tag_id);

        // Returns true if jig detected; fills T_C_W
        bool estimate(const cv::Mat& gray, const calib::CameraIntrinsics& K, calib::SE3& T_C_W_out) const;

    private:
        int tag_id_;
        double tag_size_m_;
        int dict_id_;
        cv::Ptr<cv::aruco::Dictionary> dict_;
        cv::Ptr<cv::aruco::DetectorParameters> params_;
    };

} // namespace pen
