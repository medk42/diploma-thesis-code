#include "calib/handeye_calibrator.h"
#include "calib/pose_utils.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    HandEyeCalibrator::HandEyeCalibrator(const Params& p)
        : prm_(p)
    {
    }

    HandEyeCalibrator::Result HandEyeCalibrator::run(const std::vector<Pose>& robotBase_from_flange, // robot base <- robot flange
                                                     const std::vector<cv::Vec3d>& rvec_cam_from_board, // CB: camera <- board
                                                     const std::vector<cv::Vec3d>& tvec_cam_from_board) const
    {
        Result res;

        if (robotBase_from_flange.size() != rvec_cam_from_board.size() ||
            robotBase_from_flange.size() != tvec_cam_from_board.size())
        {
            res.message = "HandEyeCalibrator: input sizes do not match.";
            return res;
        }

        if (static_cast<int>(robotBase_from_flange.size()) < prm_.minPairs)
        {
            res.message = "HandEyeCalibrator: not enough pairs.";
            return res;
        }

        std::vector<cv::Mat> R_gripper2base; // rotation: gripper -> base (robot flange->world frame, WF)
        std::vector<cv::Mat> t_gripper2base;
        std::vector<cv::Mat> R_target2cam; // rotation: target -> camera (board -> camera, CB)
        std::vector<cv::Mat> t_target2cam;

        const size_t N = robotBase_from_flange.size();
        R_gripper2base.reserve(N);
        t_gripper2base.reserve(N);
        R_target2cam.reserve(N);
        t_target2cam.reserve(N);

        for (size_t i = 0; i < N; ++i)
        {
            const SE3 T_WF = pose_utils::toSE3(robotBase_from_flange[i]); // WF: world <- flange (gripper->base)
            cv::Mat R_WF = cv::Mat(T_WF.R);
            cv::Mat t_WF = (cv::Mat_<double>(3, 1) << T_WF.t[0], T_WF.t[1], T_WF.t[2]);

            cv::Mat R_CB; // CB: camera <- board
            cv::Rodrigues(rvec_cam_from_board[i], R_CB);
            cv::Mat t_CB = (cv::Mat_<double>(3, 1) << tvec_cam_from_board[i][0], tvec_cam_from_board[i][1], tvec_cam_from_board[i][2]);

            R_gripper2base.push_back(R_WF);
            t_gripper2base.push_back(t_WF);
            R_target2cam.push_back(R_CB);
            t_target2cam.push_back(t_CB);
        }

        cv::Mat R_cam2gripper, t_cam2gripper; // output: camera -> gripper (flange<-cam, flange_from_cam, FC)

        cv::calibrateHandEye(
            R_gripper2base, t_gripper2base,
            R_target2cam, t_target2cam,
            R_cam2gripper, t_cam2gripper,
            prm_.method
        );

        double eps = 1e-6;
        const cv::Mat R_eye = cv::Mat::eye(3, 3, CV_64F);
        const cv::Mat zero_t = cv::Mat::zeros(3, 1, CV_64F);

        if (R_cam2gripper.size() != cv::Size(3, 3) ||
            t_cam2gripper.size() != cv::Size(1, 3) ||
            cv::norm(R_cam2gripper - R_eye) < eps &&
            cv::norm(t_cam2gripper - zero_t) < eps)
        {
            res.message = "HandEyeCalibrator: unexpected output from calibrateHandEye.";
            return res;
        }

        res.T_FC.R = cv::Matx33d(R_cam2gripper);
        res.T_FC.t = cv::Vec3d(t_cam2gripper);
        res.ok = true;
        return res;
    }

    SE3 HandEyeCalibrator::composeRightFromLeft(const StereoExtrinsics& right_from_left,
                                                const HandEyeCalibrator::Result& hand_eye_left)
    {
        SE3 camR_from_camL;
        camR_from_camL.R = right_from_left.R_RL;
        camR_from_camL.t = right_from_left.t_RL;
        return pose_utils::compose(camR_from_camL, pose_utils::invert(hand_eye_left.T_FC));
    }
}
