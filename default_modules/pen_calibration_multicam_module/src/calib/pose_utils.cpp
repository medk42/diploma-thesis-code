#include "calib/pose_utils.h"

#include <opencv2/calib3d.hpp>
#include <cmath>

namespace aergo::default_modules::pen_calibration_multicam_module::calib::pose_utils
{
    namespace
    {
        inline double sq(double v) { return v * v; }
    }

    Quaternion normalize(const Quaternion& q)
    {
        const double n = std::sqrt(sq(q.x) + sq(q.y) + sq(q.z) + sq(q.w));
        if (n == 0.0)
        {
            return Quaternion{};
        }
        return Quaternion{ q.x / n, q.y / n, q.z / n, q.w / n };
    }

    cv::Matx33d quatToR(const Quaternion& q_in)
    {
        const auto q = normalize(q_in);
        const double x = q.x;
        const double y = q.y;
        const double z = q.z;
        const double w = q.w;

        return cv::Matx33d{
            1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
            2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
            2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)
        };
    }

    Quaternion rToQuat(const cv::Matx33d& R)
    {
        const double trace = R(0, 0) + R(1, 1) + R(2, 2);
        Quaternion q{};

        if (trace > 0.0)
        {
            const double s = std::sqrt(trace + 1.0) * 2.0;
            q.w = 0.25 * s;
            q.x = (R(2, 1) - R(1, 2)) / s;
            q.y = (R(0, 2) - R(2, 0)) / s;
            q.z = (R(1, 0) - R(0, 1)) / s;
        }
        else if (R(0, 0) > R(1, 1) && R(0, 0) > R(2, 2))
        {
            const double s = std::sqrt(1.0 + R(0, 0) - R(1, 1) - R(2, 2)) * 2.0;
            q.w = (R(2, 1) - R(1, 2)) / s;
            q.x = 0.25 * s;
            q.y = (R(0, 1) + R(1, 0)) / s;
            q.z = (R(0, 2) + R(2, 0)) / s;
        }
        else if (R(1, 1) > R(2, 2))
        {
            const double s = std::sqrt(1.0 - R(0, 0) + R(1, 1) - R(2, 2)) * 2.0;
            q.w = (R(0, 2) - R(2, 0)) / s;
            q.x = (R(0, 1) + R(1, 0)) / s;
            q.y = 0.25 * s;
            q.z = (R(1, 2) + R(2, 1)) / s;
        }
        else
        {
            const double s = std::sqrt(1.0 - R(0, 0) - R(1, 1) + R(2, 2)) * 2.0;
            q.w = (R(1, 0) - R(0, 1)) / s;
            q.x = (R(0, 2) + R(2, 0)) / s;
            q.y = (R(1, 2) + R(2, 1)) / s;
            q.z = 0.25 * s;
        }

        return normalize(q);
    }

    SE3 toSE3(const Pose& p)
    {
        SE3 T;
        T.R = quatToR(p.orientation);
        T.t = { p.position.x, p.position.y, p.position.z };
        return T;
    }

    Pose toPose(const SE3& T)
    {
        Pose p;
        p.orientation = rToQuat(T.R);
        p.position = { T.t[0], T.t[1], T.t[2] };
        return p;
    }

    SE3 invert(const SE3& T)
    {
        SE3 inv;
        inv.R = T.R.t();
        inv.t = -(inv.R * T.t);
        return inv;
    }

    SE3 compose(const SE3& A, const SE3& B)
    {
        SE3 C;
        C.R = A.R * B.R;
        C.t = A.R * B.t + A.t;
        return C;
    }

    SE3 rtToSE3(const cv::Vec3d& rvec, const cv::Vec3d& tvec)
    {
        cv::Matx33d R;
        cv::Rodrigues(rvec, R);
        return SE3{ R, cv::Vec3d(tvec) };
    }

    void se3ToRt(const SE3& T, cv::Vec3d& rvec, cv::Vec3d& tvec)
    {
        cv::Mat R = cv::Mat(T.R);
        cv::Rodrigues(R, rvec);
        tvec = cv::Vec3d(T.t);
    }
}
