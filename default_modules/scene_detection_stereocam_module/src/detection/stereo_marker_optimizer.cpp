#include "detection/stereo_marker_optimizer.h"
#include "detection/camera_projection.h"

#include <ceres/ceres.h>
#include <ceres/rotation.h>
#include <ceres/manifold.h>
#include <ceres/solver.h>
#include <ceres/types.h>
#include <ceres/autodiff_cost_function.h>

using namespace aergo::default_modules::scene_detection_stereocam_module;



struct CornerReprojectionResidual
{
    CornerReprojectionResidual(
        const cv::Vec3f& pt_marker_pose,
        const cv::Point2f& pt_uv_observed,
        CameraParameters cam_params,
        const SE3& T_cam_ref,
        double weight_z_distance
    )
    : cam_params_(cam_params),
      u_obs_(pt_uv_observed.x),
      v_obs_(pt_uv_observed.y)
    {
        // fixed point in marker frame
        Pw_[0] = pt_marker_pose[0];
        Pw_[1] = pt_marker_pose[1];
        Pw_[2] = pt_marker_pose[2];

        // fixed T_cam_ref (world/ref -> cam)
        // Convert Matx33d + Vec3d to plain doubles
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                R_cam_ref_[3*r + c] = T_cam_ref.R(r, c);

        t_cam_ref_[0] = T_cam_ref.t[0];
        t_cam_ref_[1] = T_cam_ref.t[1];
        t_cam_ref_[2] = T_cam_ref.t[2];

        scale_ = weight_z_distance;
    }


    template <typename T>
    bool operator()(
        const T* const Q_ref_marker,  // [qw,qx,qy,qz] (Ceres expects w first)
        const T* const t_ref_marker,  // [tx,ty,tz]
        T* res
    ) const
    {
        // 1) p_ref = T_ref_marker * p_obj
        //    p_ref = R(Q)*Pw + t
        T p_ref[3];
        {
            const T Pw[3] = { T(Pw_[0]), T(Pw_[1]), T(Pw_[2]) };

            T p_rot[3];
            ceres::QuaternionRotatePoint(Q_ref_marker, Pw, p_rot);  // Q is [w,x,y,z]

            p_ref[0] = p_rot[0] + t_ref_marker[0];
            p_ref[1] = p_rot[1] + t_ref_marker[1];
            p_ref[2] = p_rot[2] + t_ref_marker[2];
        }

        // 2) p_cam = T_cam_ref * p_ref  (fixed extrinsic)
        T Xc, Yc, Zc;
        {
            const T Rcr0 = T(R_cam_ref_[0]), Rcr1 = T(R_cam_ref_[1]), Rcr2 = T(R_cam_ref_[2]);
            const T Rcr3 = T(R_cam_ref_[3]), Rcr4 = T(R_cam_ref_[4]), Rcr5 = T(R_cam_ref_[5]);
            const T Rcr6 = T(R_cam_ref_[6]), Rcr7 = T(R_cam_ref_[7]), Rcr8 = T(R_cam_ref_[8]);

            const T tc0 = T(t_cam_ref_[0]), tc1 = T(t_cam_ref_[1]), tc2 = T(t_cam_ref_[2]);

            Xc = Rcr0*p_ref[0] + Rcr1*p_ref[1] + Rcr2*p_ref[2] + tc0;
            Yc = Rcr3*p_ref[0] + Rcr4*p_ref[1] + Rcr5*p_ref[2] + tc1;
            Zc = Rcr6*p_ref[0] + Rcr7*p_ref[1] + Rcr8*p_ref[2] + tc2;
        }

        // 3) project
        T u, v;
        cam_params_.project(Xc, Yc, Zc, u, v);

        // 4) residuals
        T dx = u - T(u_obs_);
        T dy = v - T(v_obs_);
        const T scale = T(scale_);
        res[0] = scale * dx;
        res[1] = scale * dy;

        return true;
    }


    CameraParameters cam_params_;
    double Pw_[3]{};            // fixed point in object/pose frame
    double R_cam_ref_[9]{};     // fixed rotation matrix cam <- ref
    double t_cam_ref_[3]{};     // fixed translation vector cam <- ref
    double u_obs_{}, v_obs_{};  // observed 2D point
    double scale_{1.0};          // combined weight scale factor
};



void StereoMarkerOptimizer::optimizeMarker(
    const SceneMarkerDetector::CameraData& leftCamera,
    const SceneMarkerDetector::CameraData& rightCamera,
    const StereoMarkerMatcher::MatchResult::MatchedMarkerPair& matchedPair,
    Result& outResult
)
{
    cv::Vec4d Q_ref_marker_init; // qw,qx,qy,qz
    cv::Vec3d t_ref_marker_init;
    matchedPair.T_ref_marker_estimate.toQuatTvec(Q_ref_marker_init, t_ref_marker_init, true);

    double Q_ref_marker[4] = { Q_ref_marker_init[0], Q_ref_marker_init[1], Q_ref_marker_init[2], Q_ref_marker_init[3] };
    double t_ref_marker[3] = { t_ref_marker_init[0], t_ref_marker_init[1], t_ref_marker_init[2] };

    ceres::Problem problem;
    problem.AddParameterBlock(Q_ref_marker, 4, new ceres::QuaternionManifold());
    problem.AddParameterBlock(t_ref_marker, 3);

    ceres::LossFunction* loss = new ceres::CauchyLoss(cauchyLoss_);

    CameraParameters cam_params_left = CameraParameters::fromOpenCV(leftCamera.K, leftCamera.D);
    CameraParameters cam_params_right = CameraParameters::fromOpenCV(rightCamera.K, rightCamera.D);

    double Z_ref = 0.6;
    double Z = (leftCamera.T_cam_ref * matchedPair.T_ref_marker_estimate).t[2];
    if (!std::isfinite(Z)) Z = Z_ref;
    double weight_z_distance = std::clamp(Z / Z_ref, 0.8, 2.0);

    for (size_t corner_idx = 0; corner_idx < 4; ++corner_idx)
    {
        cv::Point2f pt_uv_left = matchedPair.corners_left[corner_idx];
        cv::Point2f pt_uv_right = matchedPair.corners_right[corner_idx];
        cv::Vec3f pt_object = matchedPair.marker_points_3d[corner_idx];

        problem.AddResidualBlock(
            new ceres::AutoDiffCostFunction<CornerReprojectionResidual, 2, 4, 3>(
                new CornerReprojectionResidual(
                    pt_object,
                    pt_uv_left,
                    cam_params_left,
                    leftCamera.T_cam_ref,
                    weight_z_distance
                )
            ),
            loss,
            Q_ref_marker,
            t_ref_marker
        );

        problem.AddResidualBlock(
            new ceres::AutoDiffCostFunction<CornerReprojectionResidual, 2, 4, 3>(
                new CornerReprojectionResidual(
                    pt_object,
                    pt_uv_right,
                    cam_params_right,
                    rightCamera.T_cam_ref,
                    weight_z_distance
                )
            ),
            loss,
            Q_ref_marker,
            t_ref_marker
        );
    }

    // Solve
    ceres::Solver::Options options;
    options.minimizer_type = ceres::TRUST_REGION;
    options.linear_solver_type = ceres::DENSE_QR;
    options.trust_region_strategy_type = ceres::LEVENBERG_MARQUARDT;
    options.function_tolerance = 1e-6;
    options.parameter_tolerance = 1e-6;
    options.minimizer_progress_to_stdout = false;
    options.max_num_iterations = 15;

    ceres::Solve(options, &problem, &outResult.summary);

    outResult.success = outResult.summary.termination_type == ceres::CONVERGENCE || outResult.summary.termination_type == ceres::NO_CONVERGENCE;
    outResult.T_ref_marker_optimized = SE3::fromQuatTvec(
        cv::Vec4d(Q_ref_marker[0], Q_ref_marker[1], Q_ref_marker[2], Q_ref_marker[3]),
        cv::Vec3d(t_ref_marker[0], t_ref_marker[1], t_ref_marker[2]),
        true
    );
}