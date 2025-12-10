#include "calib/pose_utils.h"
#include "calib/types.h"
#include "calib/charuco_defaults.h"

#include <iostream>

using namespace aergo::default_modules::robot_stereo_camera_calibration_module;
using namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib;

int main()
{
    Pose base_from_flange{
        {0.1, 0.2, 0.3},
        {0.0, 0.0, 0.0, 1.0}
    };

    const SE3 T = pose_utils::toSE3(base_from_flange);
    const SE3 T_inv = pose_utils::invert(T);
    const SE3 identity = pose_utils::compose(T, T_inv);
    const Pose round_trip = pose_utils::toPose(T);

    std::cout << "Round-trip position: (" << round_trip.position.x << ", "
              << round_trip.position.y << ", " << round_trip.position.z << ")\n";
    std::cout << "Identity R(0,0): " << identity.R(0, 0) << "\n";

    std::cout << "Charuco defaults: rows=" << defaults::charucoboard::ROW_COUNT
              << ", cols=" << defaults::charucoboard::COL_COUNT
              << ", square=" << defaults::charucoboard::SQUARE_LENGTH
              << ", marker=" << defaults::charucoboard::MARKER_LENGTH << "\n";

    return 0;
}
