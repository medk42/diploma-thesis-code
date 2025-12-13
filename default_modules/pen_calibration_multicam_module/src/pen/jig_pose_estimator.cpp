#include "pen/jig_pose_estimator.h"

#include "calib/pose_utils.h"

#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

using namespace aergo::default_modules::pen_calibration_multicam_module;
using namespace aergo::default_modules::pen_calibration_multicam_module::pen;
namespace pose_utils = aergo::default_modules::robot_stereo_camera_calibration_module::calib::pose_utils;

JigPoseEstimator::JigPoseEstimator(int dict_id, double tag_size_m, int tag_id)
: tag_id_(tag_id)
, tag_size_m_(tag_size_m)
, dict_id_(dict_id),
dict_(cv::makePtr<cv::aruco::Dictionary>(cv::aruco::getPredefinedDictionary(dict_id)))
{
    params_ = cv::makePtr<cv::aruco::DetectorParameters>();
    params_->cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
    params_->cornerRefinementWinSize = 5;          
    params_->cornerRefinementMaxIterations = 30;
    params_->cornerRefinementMinAccuracy = 0.01;
}

bool JigPoseEstimator::estimate(const cv::Mat& gray,
                                const calib::CameraIntrinsics& K,
                                calib::SE3& T_C_W_out) const
{
    if (gray.empty() || K.K.empty())
        return false;

    std::vector<std::vector<cv::Point2f>> corners;
    std::vector<int> ids;
    cv::aruco::detectMarkers(gray, dict_, corners, ids, params_);

    if (ids.empty())
        return false;

    // Marker object points in marker frame (Z=0 plane). Origin at marker center.
    const double s = tag_size_m_;
    const float hs = static_cast<float>(0.5 * s);
    const std::vector<cv::Point3f> objPts = {
        {-hs,  hs, 0.0f},  // top-left
        { hs,  hs, 0.0f},  // top-right
        { hs, -hs, 0.0f},  // bottom-right
        {-hs, -hs, 0.0f}   // bottom-left
    };

    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (ids[i] != tag_id_)
            continue;

        const auto& imgPts = corners[i];
        if (imgPts.size() != 4)
            return false;

        cv::Vec3d rvec, tvec;

        // Robust: try RANSAC (helps if a corner is slightly off).
        bool ok = cv::solvePnPRansac(
            objPts, imgPts,
            K.K, K.D,
            rvec, tvec,
            false,               // useExtrinsicGuess
            100,                 // iterations
            3.0,                 // reproj error threshold (px)
            0.99,                // confidence
            cv::noArray(),
            cv::SOLVEPNP_ITERATIVE
        );

        // Fallback: plain solvePnP
        if (!ok)
        {
            ok = cv::solvePnP(
                objPts, imgPts,
                K.K, K.D,
                rvec, tvec,
                false,
                cv::SOLVEPNP_ITERATIVE
            );
        }

        if (!ok)
            return false;

        // This is T_cam_from_marker (camera <- marker): Xc = R*Xm + t
        T_C_W_out = pose_utils::rtToSE3(rvec, tvec);
        return true;
    }

    return false;
}

