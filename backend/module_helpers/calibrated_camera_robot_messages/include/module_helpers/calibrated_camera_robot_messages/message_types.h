#pragma once

#include <opencv2/core.hpp>
#include <cstring>

#include "module_common/module_interface_.h"
#include "module_helpers/camera_messages/messages.h"
#include "module_helpers/robot_interface/features/robot_control/structs.h"

namespace aergo::module::helpers::calibrated_camera_robot_messages
{
    namespace rc = aergo::module::helpers::robot_interface::robot_control;
    namespace cm = aergo::module::helpers::camera_messages;

    struct CalibratedCameraSet
    {
        cm::CameraMessage camera_header;   // original camera header (blob stays external)
        uint32_t calibrated_count{0};      // number of filled calibration entries (max 4)
        rc::Pose camera_pose[4]{};         // world <- cam_i
        double K[4][9]{};                  // row-major 3x3 for each entry
        double D[4][5]{};                  // first 5 distortion coeffs for each entry
    };

    inline constexpr aergo::module::communication_channel::Producer calibrated_camera_publish_producer{
        .channel_type_identifier_ =
            "calibrated_camera_set/v1:struct{camera_header:struct{version:uint64,timestamp_us:int64},calibrated_count:uint32,camera_pose:struct{position:struct{x:double,y:double,z:double},orientation:struct{x:double,y:double,z:double,w:double}}[4],K:double[4][9],D:double[4][5]}+blob{BlobHeader+image_count*ImageHeader+image_data}",
        .display_name_ = "Calibrated camera set",
        .display_description_ = "Camera message with up to 4 calibrated poses/intrinsics attached.",
        .prioritized_ = false,
        .message_queue_capacity_ = 4
    };

    inline constexpr aergo::module::communication_channel::Consumer calibrated_camera_subscribe_consumer{
        .count_ = aergo::module::communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = calibrated_camera_publish_producer.channel_type_identifier_,
        .display_name_ = "Calibrated camera set",
        .display_description_ = "Camera message with up to 4 calibrated poses/intrinsics attached.",
        .prioritized_ = false,
        .message_queue_capacity_ = 2
    };

    inline bool addCalibData(CalibratedCameraSet& msg_out,
                             uint32_t index,
                             const rc::Pose& cam_world,
                             const cv::Mat& K,
                             const cv::Mat& D)
    {
        if (index >= 4) return false;
        if (K.rows != 3 || K.cols != 3 || K.type() != CV_64F) return false;
        if (D.total() < 5 || D.type() != CV_64F) return false;
        if (index >= msg_out.calibrated_count || msg_out.calibrated_count > 4) return false;

        msg_out.camera_pose[index] = cam_world;
        std::memcpy(msg_out.K[index], K.ptr<double>(), 9 * sizeof(double));
        std::memcpy(msg_out.D[index], D.ptr<double>(), 5 * sizeof(double));
        
        return true;
    }

    inline bool parseCalibData(const CalibratedCameraSet& msg,
                               uint32_t index,
                               rc::Pose& cam_world_out,
                               cv::Mat& K_out,
                               cv::Mat& D_out)
    {
        if (index >= msg.calibrated_count || index >= 4)
        {
            return false;
        }

        cam_world_out = msg.camera_pose[index];
        K_out = cv::Mat(3, 3, CV_64F);
        D_out = cv::Mat(1, 5, CV_64F);
        std::memcpy(K_out.ptr<double>(), msg.K[index], 9 * sizeof(double));
        std::memcpy(D_out.ptr<double>(), msg.D[index], 5 * sizeof(double));
        return true;
    }
}
