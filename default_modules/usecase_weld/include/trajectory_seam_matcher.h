#pragma once
#include <opencv2/core.hpp>
#include <algorithm>
#include <vector>
#include <limits>
#include <cmath>

// include your SE3
#include "module_helpers/pose_utils/pose_utils.h"   // <- adjust include to where your SE3 lives
#include "module_helpers/pen_messages/message_types.h"
#include "seam_detector.h"      // <- provides Seam struct

namespace aergo::default_modules::usecase_weld
{

    namespace pu = aergo::module::helpers::pose_utils;
    namespace pm = aergo::module::helpers::pen_messages;

// ---------- small vec helpers ----------
inline double median(std::vector<double> v)
{
    if (v.empty()) return std::numeric_limits<double>::infinity();
    const size_t n = v.size();
    const size_t mid = n/2;
    std::nth_element(v.begin(), v.begin()+mid, v.end());
    double m = v[mid];
    if ((n % 2) == 0)
    {
        std::nth_element(v.begin(), v.begin()+mid-1, v.end());
        m = 0.5 * (m + v[mid-1]);
    }
    return m;
}

inline double distPointSegment3D(const cv::Vec3d& p, const cv::Vec3d& a, const cv::Vec3d& b)
{
    return norm3(p - closestPointOnSegment3D(a,b,p));
}

// Pose (quat) -> SE3 (your SE3 wants (qw,qx,qy,qz))
inline pu::SE3 poseToSE3(const pm::Pose& p)
{
    const cv::Vec4d q(p.qw, p.qx, p.qy, p.qz);
    const cv::Vec3d t(p.x, p.y, p.z);
    return pu::SE3::fromQuatTvec(q, t, /*reorthonormalize=*/true);
}

// Build a stable frame where:
//  X = seam tangent (unit)
//  Z = tool forward (unit) (we use -torch_dir_world)
//  Y = Z x X
inline cv::Matx33d buildFrameXZ(const cv::Vec3d& x_in, const cv::Vec3d& z_in)
{
    cv::Vec3d x = x_in;
    cv::Vec3d z = z_in;
    normalize3(x);
    normalize3(z);

    // If nearly parallel due to noise, pick a fallback z
    if (std::abs(dot3(x,z)) > 0.98)
    {
        cv::Vec3d tmp = (std::abs(x[2]) < 0.9) ? cv::Vec3d(0,0,1) : cv::Vec3d(0,1,0);
        z = cross3(x, tmp);
        normalize3(z);
    }

    cv::Vec3d y = cross3(z, x);
    if (!normalize3(y))
    {
        // worst-case fallback
        cv::Vec3d tmp = (std::abs(z[2]) < 0.9) ? cv::Vec3d(0,0,1) : cv::Vec3d(0,1,0);
        y = cross3(z, tmp);
        normalize3(y);
    }

    // Re-orthonormalize x to ensure perfect right-handed basis
    x = cross3(y, z);
    normalize3(x);

    return cv::Matx33d(
        x[0], y[0], z[0],
        x[1], y[1], z[1],
        x[2], y[2], z[2]
    );
}

struct TrajectorySeamMatcher
{
    struct Params
    {
        // Stage 1 (selection)
        double select_max_median_dist = 0.010;     // 10mm median distance to seam segment
        double select_angle_eps_deg   = 25.0;     // pen dir vs seam tangent
        double w_dist = 1.0;
        double w_ang  = 0.5;

        // Use middle window [0.25 .. 0.75] of arc length
        double window_start_frac = 0.25;
        double window_end_frac   = 0.75;

        // Stage 2 (projection)
        double accept_point_max_dist = 0.020;     // 20mm: discard crazy points
        int    min_points_kept = 6;              // must keep at least this many
        double min_weld_len = 0.010;             // 10mm minimum returned segment
    };

    Params params;

    // returns false if no seam matches or projection fails
    bool matchTrajectoryToSeam(
        const std::vector<Seam>& seams,
        const std::vector<pm::Pose>& pen_trajectory,
        pu::SE3& out_start,
        pu::SE3& out_end,
        int* out_seam_index = nullptr) const
    {
        if (seams.empty() || pen_trajectory.size() < 2) return false;

        // --- Build positions + pen directions + arc-length ---
        const int N = (int)pen_trajectory.size();
        std::vector<cv::Vec3d> P(N);
        std::vector<cv::Vec3d> penDir(N);
        std::vector<double> s(N, 0.0);

        for (int i = 0; i < N; ++i)
        {
            const pm::Pose& pp = pen_trajectory[i];
            P[i] = cv::Vec3d(pp.x, pp.y, pp.z);

            const pu::SE3 T = poseToSE3(pp);
            cv::Vec3d d = T * cv::Vec3d(0,0,-1); // torch positive Z is facing towards the seam, we need the away direction instead
            if (!normalize3(d)) d = cv::Vec3d(0,0,1);
            penDir[i] = d;

            if (i > 0)
            {
                s[i] = s[i-1] + norm3(P[i] - P[i-1]);
            }
        }
        const double totalLen = s.back();
        if (totalLen < 1e-6) return false;

        // middle window indices
        const double s0 = params.window_start_frac * totalLen;
        const double s1 = params.window_end_frac   * totalLen;

        std::vector<int> windowIdx;
        windowIdx.reserve(N);
        for (int i = 0; i < N; ++i)
        {
            if (s[i] >= s0 && s[i] <= s1) windowIdx.push_back(i);
        }
        if ((int)windowIdx.size() < 3)
        {
            // fallback: use everything
            windowIdx.clear();
            for (int i = 0; i < N; ++i) windowIdx.push_back(i);
        }

        // --- Stage 1: score seams ---
        const double ang_eps_rad = params.select_angle_eps_deg * (CV_PI / 180.0);
        const double cos_thr = std::cos(ang_eps_rad);

        int best = -1;
        double bestScore = std::numeric_limits<double>::infinity();

        for (int si = 0; si < (int)seams.size(); ++si)
        {
            const Seam& S = seams[si];
            cv::Vec3d tS = S.torch_dir_world;
            if (!normalize3(tS)) continue;

            std::vector<double> dists;
            std::vector<double> angErrs;
            dists.reserve(windowIdx.size());
            angErrs.reserve(windowIdx.size());

            for (int idx : windowIdx)
            {
                const double d = distPointSegment3D(P[idx], S.p0_world, S.p1_world);
                dists.push_back(d);

                const double a = dot3(penDir[idx], tS);     // 1 good (parallel), 0 bad (perpendicular), -1 bad (anti-parallel)
                angErrs.push_back(a);
            }

            const double medD = median(dists);
            const double medA = median(angErrs);

            // Hard gates (fail fast)
            if (medD > params.select_max_median_dist) continue;
            if (medA < cos_thr) continue; // median |dot| too small => angle too big

            // Soft score
            const double score = params.w_dist * (medD * medD) + params.w_ang * medA;
            if (score < bestScore)
            {
                bestScore = score;
                best = si;
            }
        }

        if (best < 0) return false;
        if (out_seam_index) *out_seam_index = best;

        // --- Stage 2: project all points to infinite seam line, discard far points, clamp to seam segment ---
        const Seam& Sbest = seams[best];

        cv::Vec3d sA = Sbest.p0_world;
        cv::Vec3d sB = Sbest.p1_world;

        cv::Vec3d tS = sB - sA;
        double L = norm3(tS);
        if (L < 1e-9) return false;
        tS *= (1.0 / L);

        // Choose seam direction sign to match user's stroke direction
        // Use window start and end positions to determine the direction of the trajectory
        cv::Vec3d trajDir = P[windowIdx.back()] - P[windowIdx.front()];
        if (!normalize3(trajDir)) trajDir = tS;

        if (dot3(trajDir, tS) < 0.0)
        {
            std::swap(sA, sB);
            tS = -tS;
        }

        // Collect u params
        double uMin =  std::numeric_limits<double>::infinity();
        double uMax = -std::numeric_limits<double>::infinity();
        int kept = 0;

        for (int i = 0; i < N; ++i)
        {
            // projection to infinite line
            const double u = dot3(P[i] - sA, tS);
            const cv::Vec3d proj = sA + tS * u;
            const double d = norm3(P[i] - proj);

            if (d > params.accept_point_max_dist) continue; // discard outliers

            // clamp to segment [0..L]
            double uc = u;
            if (uc < 0.0) uc = 0.0;
            if (uc > L)   uc = L;

            uMin = std::min(uMin, uc);
            uMax = std::max(uMax, uc);
            kept++;
        }

        if (kept < params.min_points_kept) return false;
        if ((uMax - uMin) < params.min_weld_len) return false;

        // Build start/end poses
        const cv::Vec3d pStart = sA + tS * uMin;
        const cv::Vec3d pEnd   = sA + tS * uMax;

        // Tool Z points toward seam: -torch_dir_world
        cv::Vec3d zTool = -Sbest.torch_dir_world;
        if (!normalize3(zTool)) return false;

        const cv::Matx33d R = buildFrameXZ(tS, zTool);

        out_start = pu::SE3{R, pStart};
        out_end   = pu::SE3{R, pEnd};
        return true;
    }
};

} // namespace aergo::module::helpers::seam_match
