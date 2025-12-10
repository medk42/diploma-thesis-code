#include "calib/handeye_calibrator.h"
#include "calib/pose_utils.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    HandEyeCalibrator::HandEyeCalibrator(const Params& p)
        : prm_(p)
    {
    }

    HandEyeCalibrator::Result HandEyeCalibrator::run(const std::vector<Pose>& robotBase_from_flange,
                                                     const std::vector<cv::Vec3d>& rvec_target_from_cam,
                                                     const std::vector<cv::Vec3d>& tvec_target_from_cam) const
    {
        Result res;

        if (robotBase_from_flange.size() != rvec_target_from_cam.size() ||
            robotBase_from_flange.size() != tvec_target_from_cam.size())
        {
            res.message = "HandEyeCalibrator: input sizes do not match.";
            return res;
        }

        if (static_cast<int>(robotBase_from_flange.size()) < prm_.minPairs)
        {
            res.message = "HandEyeCalibrator: not enough pairs.";
            return res;
        }

        std::vector<cv::Mat> R_gripper2base;
        std::vector<cv::Mat> t_gripper2base;
        std::vector<cv::Mat> R_target2cam;
        std::vector<cv::Mat> t_target2cam;

        const size_t N = robotBase_from_flange.size();
        R_gripper2base.reserve(N);
        t_gripper2base.reserve(N);
        R_target2cam.reserve(N);
        t_target2cam.reserve(N);

        for (size_t i = 0; i < N; ++i)
        {
            const SE3 Twg = pose_utils::toSE3(robotBase_from_flange[i]); // base <- flange (gripper->base)
            cv::Mat Rg = cv::Mat(Twg.R);
            cv::Mat tg = (cv::Mat_<double>(3, 1) << Twg.t[0], Twg.t[1], Twg.t[2]);

            cv::Mat Rt; // target -> camera
            cv::Rodrigues(rvec_target_from_cam[i], Rt);
            cv::Mat tt = (cv::Mat_<double>(3, 1) << tvec_target_from_cam[i][0], tvec_target_from_cam[i][1], tvec_target_from_cam[i][2]);

            R_gripper2base.push_back(Rg);
            t_gripper2base.push_back(tg);
            R_target2cam.push_back(Rt);
            t_target2cam.push_back(tt);
        }

        cv::Mat R_cam2gripper, t_cam2gripper;
        try
        {
            cv::calibrateHandEye(
                R_gripper2base,
                t_gripper2base,
                R_target2cam,
                t_target2cam,
                R_cam2gripper,
                t_cam2gripper,
                prm_.method);
        }
        catch (const std::exception& e)
        {
            res.message = std::string("HandEyeCalibrator: exception during calibrateHandEye: ") + e.what();
            return res;
        }

        res.cam_from_flange.R = cv::Matx33d(R_cam2gripper);
        res.cam_from_flange.t = cv::Vec3d(t_cam2gripper);
        res.ok = true;
        return res;
    }

    SE3 HandEyeCalibrator::composeRightFromLeft(const StereoExtrinsics& right_from_left,
                                                const SE3& camL_from_flange)
    {
        SE3 camR_from_camL;
        camR_from_camL.R = right_from_left.R_RL;
        camR_from_camL.t = right_from_left.t_RL;
        return pose_utils::compose(camR_from_camL, camL_from_flange);
    }
}
