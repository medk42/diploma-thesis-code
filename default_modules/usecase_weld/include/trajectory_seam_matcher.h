#pragma once
#include <opencv2/core.hpp>
#include <algorithm>
#include <vector>
#include <limits>
#include <cmath>

// #define DEBUG_LOGGING_TRAJECTORY_SEAM_MATCHER

#ifdef DEBUG_LOGGING_TRAJECTORY_SEAM_MATCHER
#include <iostream>
#include <sstream>
#include <iomanip>
#endif

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
        double select_angle_eps_deg   = 60.0;     // pen direction vs seam torch_dir_world
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
        const int trajectory_point_count = (int)pen_trajectory.size();
        std::vector<cv::Vec3d> pen_positions(trajectory_point_count);
        std::vector<cv::Vec3d> pen_neg_z_directions(trajectory_point_count);
        std::vector<double> trajectory_distances(trajectory_point_count, 0.0);

        for (int i = 0; i < trajectory_point_count; ++i)
        {
            const pm::Pose& pen_pose = pen_trajectory[i];

            // Pen tip position (world)
            pen_positions[i] = cv::Vec3d(pen_pose.x, pen_pose.y, pen_pose.z);

            // Build an SE3 that contains only the pen orientation.
            // IMPORTANT: translation must be zero, because we are transforming a *direction* (not a point):
            //            direction_world = R * direction_local
            const cv::Vec4d q(pen_pose.qw, pen_pose.qx, pen_pose.qy, pen_pose.qz);
            const pu::SE3 T_rot = pu::SE3::fromQuatTvec(q, cv::Vec3d(0,0,0), /*reorthonormalize=*/true);

            // Torch/pen local +Z points toward the seam; we want the opposite ("away") direction => local -Z.
            cv::Vec3d neg_z_dir = T_rot * cv::Vec3d(0,0,-1);

            if (!normalize3(neg_z_dir)) neg_z_dir = cv::Vec3d(0,0,1);
            pen_neg_z_directions[i] = neg_z_dir;

            if (i > 0)
            {
                trajectory_distances[i] = trajectory_distances[i-1] + norm3(pen_positions[i] - pen_positions[i-1]);
            }
        }
        const double total_length = trajectory_distances.back();
        if (total_length < 1e-6) return false;

        // middle window indices
        const double s0 = params.window_start_frac * total_length;
        const double s1 = params.window_end_frac   * total_length;

        std::vector<int> window_indexes;
        window_indexes.reserve(trajectory_point_count);
        for (int i = 0; i < trajectory_point_count; ++i)
        {
            if (trajectory_distances[i] >= s0 && trajectory_distances[i] <= s1) window_indexes.push_back(i);
        }
        if ((int)window_indexes.size() < 3)
        {
            // fallback: use everything
            window_indexes.clear();
            for (int i = 0; i < trajectory_point_count; ++i) window_indexes.push_back(i);
        }

        // --- Stage 1: score seams ---
        const double ang_eps_rad = params.select_angle_eps_deg * (CV_PI / 180.0);
        const double cos_thr = std::cos(ang_eps_rad);

        #ifdef DEBUG_LOGGING_TRAJECTORY_SEAM_MATCHER
        std::stringstream ss;
        ss << std::fixed << "Match results:\n";
        ss << "\tTotal length: " << std::setprecision(1) << total_length * 1000 << "mm\n";
        ss << "\tUsed points: " << window_indexes.size() << "/" << trajectory_point_count << "\n";
        ss << "\tAngle eps: " << std::setprecision(1) << std::setw(3) << params.select_angle_eps_deg << "deg (cos=" << std::setprecision(3) << std::setw(5) << cos_thr << ")\n";
        ss << "\tSeams:\n";
        #endif

        int best = -1;
        double best_score = std::numeric_limits<double>::infinity();

        for (int si = 0; si < (int)seams.size(); ++si)
        {
            const Seam& seam = seams[si];
            cv::Vec3d torch_dir_world = seam.torch_dir_world;
            if (!normalize3(torch_dir_world)) continue;

            std::vector<double> dists;
            std::vector<double> angErrs;
            dists.reserve(window_indexes.size());
            angErrs.reserve(window_indexes.size());

            for (int idx : window_indexes)
            {
                const double d = distPointSegment3D(pen_positions[idx], seam.p0_world, seam.p1_world);
                dists.push_back(d);

                const double a = dot3(pen_neg_z_directions[idx], torch_dir_world);     // 1 good (parallel), 0 bad (perpendicular), -1 bad (anti-parallel)
                angErrs.push_back(a);
            }

            const double median_dists = median(dists);
            const double median_angle_errors = median(angErrs);

            #ifdef DEBUG_LOGGING_TRAJECTORY_SEAM_MATCHER
            ss << "\t\t" << std::setw(2) << si << ": dist=" << std::setprecision(2) << std::setw(5) << median_dists * 1000 << "mm (max " << std::setprecision(0) << std::setw(3) << params.select_max_median_dist * 1000 << "mm, " << (median_dists > params.select_max_median_dist ? "FAIL" : "pass") << "), ang: " << std::setprecision(3) << std::setw(5) << median_angle_errors << " (" << std::setprecision(1) << std::setw(5) << std::acos(std::clamp(median_angle_errors, -1.0, 1.0)) * 180 / CV_PI << "deg, [" << std::setprecision(3) << std::setw(5) << torch_dir_world[0] << "," << std::setw(5) << torch_dir_world[1] << "," << std::setw(5) << torch_dir_world[2] << "] to [" << std::setw(5) << pen_neg_z_directions[window_indexes.front()][0] << "," << std::setw(5) << pen_neg_z_directions[window_indexes.front()][1] << "," << std::setw(5) << pen_neg_z_directions[window_indexes.front()][2] << "]; min " << std::setprecision(3) << std::setw(5) << cos_thr << ", " << (median_angle_errors < cos_thr ? "FAIL" : "pass") << "), score: " << std::setprecision(3) << std::setw(5) << (params.w_dist * (median_dists * median_dists) - params.w_ang * median_angle_errors) << ((median_dists <= params.select_max_median_dist && median_angle_errors >= cos_thr && params.w_dist * (median_dists * median_dists) - params.w_ang * median_angle_errors < best_score) ? " <-- best so far" : "") << "\n";
            #endif

            // Hard gates (fail fast)
            if (median_dists > params.select_max_median_dist) continue;
            if (median_angle_errors < cos_thr) continue; // median |dot| too small => angle too big

            // Soft score
            const double score = params.w_dist * (median_dists * median_dists) - params.w_ang * median_angle_errors;
            if (score < best_score)
            {
                best_score = score;
                best = si;
            }
        }

        #ifdef DEBUG_LOGGING_TRAJECTORY_SEAM_MATCHER
        ss << "\tBest seam index: " << best << (best >= 0 ? " <-- selected" : " (no good seam)") << "\n";
        std::cout << ss.str();
        #endif

        if (best < 0) return false;
        if (out_seam_index) *out_seam_index = best;

        // --- Stage 2: project all points to infinite seam line, discard far points, clamp to seam segment ---
        const Seam& seam_best = seams[best];

        cv::Vec3d seam_best_start = seam_best.p0_world;
        cv::Vec3d seam_best_end = seam_best.p1_world;

        cv::Vec3d seam_tangent = seam_best_end - seam_best_start;
        double L = norm3(seam_tangent);
        if (L < 1e-9) return false;
        seam_tangent *= (1.0 / L);

        // Choose seam direction sign to match user's stroke direction
        // Use window start and end positions to determine the direction of the trajectory
        cv::Vec3d trajectory_tangent = pen_positions[window_indexes.back()] - pen_positions[window_indexes.front()];
        if (!normalize3(trajectory_tangent)) trajectory_tangent = seam_tangent;

        if (dot3(trajectory_tangent, seam_tangent) < 0.0)
        {
            std::swap(seam_best_start, seam_best_end);
            seam_tangent = -seam_tangent;
        }

        // Collect u params
        double u_min =  std::numeric_limits<double>::infinity();
        double u_max = -std::numeric_limits<double>::infinity();
        int kept = 0;

        for (int i = 0; i < trajectory_point_count; ++i)
        {
            // projection to infinite line
            const double u = dot3(pen_positions[i] - seam_best_start, seam_tangent);
            const cv::Vec3d proj = seam_best_start + seam_tangent * u;
            const double d = norm3(pen_positions[i] - proj);

            if (d > params.accept_point_max_dist) continue; // discard outliers

            // clamp to segment [0..L]
            double uc = u;
            if (uc < 0.0) uc = 0.0;
            if (uc > L)   uc = L;

            u_min = std::min(u_min, uc);
            u_max = std::max(u_max, uc);
            kept++;
        }

        if (kept < params.min_points_kept) return false;
        if ((u_max - u_min) < params.min_weld_len) return false;

        // Build start/end poses
        const cv::Vec3d result_start = seam_best_start + seam_tangent * u_min;
        const cv::Vec3d result_end   = seam_best_start + seam_tangent * u_max;

        // Tool Z points toward seam: -torch_dir_world
        cv::Vec3d tool_z = -seam_best.torch_dir_world;
        if (!normalize3(tool_z)) return false;

        const cv::Matx33d R = buildFrameXZ(seam_tangent, tool_z);

        out_start = pu::SE3{R, result_start};
        out_end   = pu::SE3{R, result_end};
        return true;
    }
};

}
