#pragma once

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>

namespace aergo::module::helpers::pose_utils
{
    struct SE3
    {   
        inline static SE3 unit()
        {
            return SE3{cv::Matx33d::eye(), cv::Vec3d(0,0,0)};
        }

        inline static SE3 fromRvecTvec(const cv::Vec3d& rvec, const cv::Vec3d& tvec)
        {
            cv::Matx33d R;
            cv::Rodrigues(rvec, R);
            return SE3{R, tvec};
        }

        inline void toRvecTvec(cv::Vec3d& rvec, cv::Vec3d& tvec) const
        {
            cv::Rodrigues(R, rvec);
            tvec = cv::Vec3d(t);
        }

        inline SE3 operator*(const SE3& other) const
        {
            cv::Matx33d R_new = R * other.R;
            cv::Vec3d t_new = R * other.t + t;
            return SE3{R_new, t_new};
        }

        inline cv::Vec3d operator*(const cv::Vec3d& point) const
        {
            cv::Vec3d p_transformed = R * point + t;
            return p_transformed;
        }

        inline cv::Vec3f operator*(const cv::Vec3f& p) const
        {
            cv::Vec3d v(p[0], p[1], p[2]);
            cv::Vec3d out = R * v + t;
            return cv::Vec3f(
                static_cast<float>(out[0]),
                static_cast<float>(out[1]),
                static_cast<float>(out[2])
            );
        }

        inline SE3 inverse() const
        {
            cv::Matx33d R_inv = R.t();
            cv::Vec3d t_inv = -(R_inv * t);
            return SE3{R_inv, t_inv};
        }

        inline static cv::Matx33d reorthonormalizeR(const cv::Matx33d& R_in)
        {
            // Project to nearest proper rotation: R = U * V^T, with det(R)=+1
            cv::Mat Rm = cv::Mat(R_in);
            cv::Mat w, Um, Vtm;

            cv::SVD::compute(Rm, w, Um, Vtm);

            cv::Matx33d Rproj;
            cv::Mat(Um * Vtm).copyTo(cv::Mat(3,3,CV_64F, Rproj.val));

            // Fix possible reflection (det = -1)
            const double detR =
                Rproj(0,0) * (Rproj(1,1)*Rproj(2,2) - Rproj(1,2)*Rproj(2,1)) -
                Rproj(0,1) * (Rproj(1,0)*Rproj(2,2) - Rproj(1,2)*Rproj(2,0)) +
                Rproj(0,2) * (Rproj(1,0)*Rproj(2,1) - Rproj(1,1)*Rproj(2,0));

            if (detR < 0.0)
            {
                // Flip the last column of U (equivalently last singular vector) and recompute
                Um.at<double>(0,2) *= -1.0;
                Um.at<double>(1,2) *= -1.0;
                Um.at<double>(2,2) *= -1.0;
                cv::Mat(Um * Vtm).copyTo(cv::Mat(3,3,CV_64F, Rproj.val));
            }

            return Rproj;
        }

        /// @brief creates SE3 from quaternion + translation vector in format expected by Ceres ([qw,qx,qy,qz], [tx,ty,tz]) 
        inline static SE3 fromQuatTvec(const cv::Vec4d& q, const cv::Vec3d& tvec, bool reorthonormalize)
        {
            const double w = q[0], x = q[1], y = q[2], z = q[3];

            // Normalize quaternion (important!)
            const double n = std::sqrt(x*x + y*y + z*z + w*w);
            CV_Assert(n > 0.0);

            const double xn = x / n;
            const double yn = y / n;
            const double zn = z / n;
            const double wn = w / n;

            const cv::Matx33d R(
                1 - 2*(yn*yn + zn*zn),  2*(xn*yn - zn*wn),      2*(xn*zn + yn*wn),
                2*(xn*yn + zn*wn),      1 - 2*(xn*xn + zn*zn),  2*(yn*zn - xn*wn),
                2*(xn*zn - yn*wn),      2*(yn*zn + xn*wn),      1 - 2*(xn*xn + yn*yn)
            );

            if (reorthonormalize)
            {
                return {reorthonormalizeR(R), tvec};
            }
            else
            {
                return SE3{R, tvec};
            }
        }


        /// @brief returns the pose as quaternion + translation vector in format expected by Ceres ([qw,qx,qy,qz], [tx,ty,tz])
        inline void toQuatTvec(cv::Vec4d& quat, cv::Vec3d& tvec, bool reorthonormalize) const
        {
            cv::Matx33d Ruse = reorthonormalize ? reorthonormalizeR(R) : R;

            const double m00 = Ruse(0,0), m01 = Ruse(0,1), m02 = Ruse(0,2);
            const double m10 = Ruse(1,0), m11 = Ruse(1,1), m12 = Ruse(1,2);
            const double m20 = Ruse(2,0), m21 = Ruse(2,1), m22 = Ruse(2,2);

            double qw, qx, qy, qz;
            const double trace = m00 + m11 + m22;

            if (trace > 0.0)
            {
                const double s = std::sqrt(trace + 1.0) * 2.0;
                qw = 0.25 * s;
                qx = (m21 - m12) / s;
                qy = (m02 - m20) / s;
                qz = (m10 - m01) / s;
            }
            else if (m00 > m11 && m00 > m22)
            {
                const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
                qw = (m21 - m12) / s;
                qx = 0.25 * s;
                qy = (m01 + m10) / s;
                qz = (m02 + m20) / s;
            }
            else if (m11 > m22)
            {
                const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
                qw = (m02 - m20) / s;
                qx = (m01 + m10) / s;
                qy = 0.25 * s;
                qz = (m12 + m21) / s;
            }
            else
            {
                const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
                qw = (m10 - m01) / s;
                qx = (m02 + m20) / s;
                qy = (m12 + m21) / s;
                qz = 0.25 * s;
            }

            quat = cv::Vec4d(qw, qx, qy, qz); // (w,x,y,z)
            tvec = t;
        }


        inline void toProjection(cv::Matx34d& P) const
        {
            // [ R | t ]
            P(0,0) = R(0,0);  P(0,1) = R(0,1);  P(0,2) = R(0,2);  P(0,3) = t[0];
            P(1,0) = R(1,0);  P(1,1) = R(1,1);  P(1,2) = R(1,2);  P(1,3) = t[1];
            P(2,0) = R(2,0);  P(2,1) = R(2,1);  P(2,2) = R(2,2);  P(2,3) = t[2];
        }




        cv::Matx33d R; // 3x3 rotation matrix
        cv::Vec3d t; // 3x1 translation vector
    };
}