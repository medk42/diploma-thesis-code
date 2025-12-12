#pragma once

#include <opencv2/core.hpp>
#include <cstring>

#include "module_common/module_interface_.h"
#include "module_helpers/camera_pose_helper/message_structure.h"
#include "module_helpers/robot_interface/features/robot_control/structs.h"

namespace aergo::module::helpers::calibrated_stereo_robot_messages
{
    namespace cph = aergo::module::helpers::camera_pose_helper;
    namespace rc = aergo::module::helpers::robot_interface::robot_control;

    struct CalibratedStereoRobotMountedCamera
    {
        cph::CameraWithFlangePose camera_with_flange_pose; // incoming camera+flange info
        rc::Pose camera_pose_left;                         // world <- left
        rc::Pose camera_pose_right;                        // world <- right
        double K_left[9]{};
        double D_left[5]{};
        double K_right[9]{};
        double D_right[5]{};
    };

    inline constexpr aergo::module::communication_channel::Producer calibrated_stereo_publish_producer{
        .channel_type_identifier_ =
            "calibrated_stereo_robot_camera/v1:struct{camera_with_flange_pose:struct{camera_header:struct{version:uint64,timestamp_us:int64},flange_pose:struct{position:struct{x:double,y:double,z:double},orientation:struct{x:double,y:double,z:double,w:double}}},camera_pose_left:struct{position:struct{x:double,y:double,z:double},orientation:struct{x:double,y:double,z:double,w:double}},camera_pose_right:struct{position:struct{x:double,y:double,z:double},orientation:struct{x:double,y:double,z:double,w:double}},K_left:double[9],D_left:double[5],K_right:double[9],D_right:double[5]}+blob{BlobHeader+image_count*ImageHeader+image_data}",
        .display_name_ = "Calibrated stereo rig",
        .display_description_ = "Calibrated stereo camera poses (world<-camL/camR) with original stereo blob.",
        .prioritized_ = false,
        .message_queue_capacity_ = 4
    };

    inline constexpr aergo::module::communication_channel::Consumer calibrated_stereo_subscribe_consumer{
        .count_ = aergo::module::communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = calibrated_stereo_publish_producer.channel_type_identifier_,
        .display_name_ = "Calibrated stereo rig",
        .display_description_ = "Calibrated stereo camera poses (world<-camL/camR) with original stereo blob.",
        .prioritized_ = false,
        .message_queue_capacity_ = 2
    };

    inline bool makeCalibratedStereoRobotMountedCamera(
        const cph::CameraWithFlangePose& src_header,
        const rc::Pose& cam_left_world,
        const rc::Pose& cam_right_world,
        const cv::Mat& K_left,
        const cv::Mat& D_left,
        const cv::Mat& K_right,
        const cv::Mat& D_right,
        CalibratedStereoRobotMountedCamera& msg_out)
    {
        if (K_left.rows != 3 || K_left.cols != 3 || K_left.type() != CV_64F) return false;
        if (K_right.rows != 3 || K_right.cols != 3 || K_right.type() != CV_64F) return false;
        if (D_left.total() < 5 || D_left.type() != CV_64F) return false;
        if (D_right.total() < 5 || D_right.type() != CV_64F) return false;

        msg_out.camera_with_flange_pose = src_header;
        msg_out.camera_pose_left = cam_left_world;
        msg_out.camera_pose_right = cam_right_world;

        std::memcpy(msg_out.K_left, K_left.ptr<double>(), 9 * sizeof(double));
        std::memcpy(msg_out.K_right, K_right.ptr<double>(), 9 * sizeof(double));
        std::memcpy(msg_out.D_left, D_left.ptr<double>(), 5 * sizeof(double));
        std::memcpy(msg_out.D_right, D_right.ptr<double>(), 5 * sizeof(double));
        return true;
    }

    inline bool parseCalibratedStereoRobotMountedCamera(
        const CalibratedStereoRobotMountedCamera& msg,
        cv::Mat& K_left_out,
        cv::Mat& D_left_out,
        cv::Mat& K_right_out,
        cv::Mat& D_right_out)
    {
        K_left_out = cv::Mat(3, 3, CV_64F);
        K_right_out = cv::Mat(3, 3, CV_64F);
        D_left_out = cv::Mat(1, 5, CV_64F);
        D_right_out = cv::Mat(1, 5, CV_64F);

        std::memcpy(K_left_out.ptr<double>(), msg.K_left, 9 * sizeof(double));
        std::memcpy(K_right_out.ptr<double>(), msg.K_right, 9 * sizeof(double));
        std::memcpy(D_left_out.ptr<double>(), msg.D_left, 5 * sizeof(double));
        std::memcpy(D_right_out.ptr<double>(), msg.D_right, 5 * sizeof(double));
        return true;
    }
}
