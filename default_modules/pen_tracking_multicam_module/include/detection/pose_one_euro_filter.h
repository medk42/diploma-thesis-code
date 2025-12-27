#pragma once

#include <opencv2/core.hpp>
#include <chrono>

#include "module_helpers/pose_utils/pose_utils.h"

namespace aergo::default_modules::pen_tracking_multicam_module
{

    namespace pu = aergo::module::helpers::pose_utils;

    class PoseOneEuroFilter
    {
    public:
        struct Params
        {
            // --- translation 1€ ---
            double trans_mincutoff_hz = 2.0;
            double trans_beta         = 30.0;
            double trans_dcutoff_hz   = 3.0;

            // --- rotation - 1€ adaptive slerp ---
            double rot_mincutoff_hz = 3.0;
            double rot_beta         = 3.0;
            double rot_dcutoff_hz   = 6.0;

            // --- dt handling (seconds) ---
            // Clamp dt to avoid weirdness if your loop stalls for a moment.
            double dt_min_sec = 1.0 / 240.0;
            double dt_max_sec = 1.0 / 15.0;

            // --- tracking loss handling ---
            int    max_consecutive_missed = 8;    // at 60Hz ~133ms
            double reset_timeout_sec      = 0.6;  // OR time-based reset
        };

    public:
        explicit PoseOneEuroFilter(const Params& p = Params{});

        void reset();                 // hard reset (forgets internal state; next update() re-inits)
        bool isInitialized() const;

        // Call when you have a measurement this frame.
        pu::SE3 update(const pu::SE3& measured_pose);

        // Call once per frame when you have NO measurement (pen not detected).
        // Holds last output; after long loss, internal filter state resets.
        pu::SE3 updateLost();

        int  consecutiveMissed() const;

    private:
        // --- internals ---
        using Clock = std::chrono::steady_clock;

        double computeDt_(Clock::time_point now);
        void   initFromMeasurement_(const pu::SE3& m);
        void   markStaleAfterLongLoss_();

        // quaternion is [w, x, y, z]
        static cv::Vec4d quatNormalize_(const cv::Vec4d& q);
        static cv::Vec4d quatConj_(const cv::Vec4d& q);
        static cv::Vec4d quatMul_(const cv::Vec4d& a, const cv::Vec4d& b);
        static double    quatDot_(const cv::Vec4d& a, const cv::Vec4d& b);

        static cv::Vec4d quatFromR_(const cv::Matx33d& R);
        static cv::Matx33d RFromQuat_(const cv::Vec4d& q);

        // SO(3) log/exp on unit quaternions:
        // log(q) -> rotation vector (axis * angle), exp(rotvec) -> unit quaternion
        static cv::Vec3d quatLogSO3_(const cv::Vec4d& q_unit);
        static cv::Vec4d quatExpSO3_(const cv::Vec3d& rotvec);

        static double quatAngleRad_(const cv::Vec4d& q_unit_in);
        static cv::Vec4d quatSlerp_(cv::Vec4d q0, cv::Vec4d q1, double t);

    private:
        struct LowPass
        {
            bool first = true;
            double prev = 0.0;
            double filter(double x, double a);
            void reset();
        };

        struct OneEuroScalar
        {
            double mincutoff_hz = 1.0;
            double beta         = 0.0;
            double dcutoff_hz   = 1.0;

            bool first = true;

            LowPass x_lp;
            LowPass dx_lp;

            double filter(double x, double dt);
            void reset();
        };

        struct OneEuroVec3
        {
            OneEuroScalar fx, fy, fz;
            cv::Vec3d filter(const cv::Vec3d& v, double dt);
            void reset();
        };

    private:
        Params params_;

        bool initialized_ = false;
        bool stale_after_loss_ = false;

        pu::SE3 last_out_;          // last filtered output (held during loss)
        cv::Vec4d q_out_{1.0, 0.0, 0.0, 0.0}; // filtered orientation as quaternion

        OneEuroVec3 trans_filter_;
        LowPass rot_omega_lp_; // low-pass angular speed (rad/s)

        Clock::time_point last_tick_{};
        Clock::time_point last_meas_time_{};

        int missed_ = 0;
    };
} // namespace
