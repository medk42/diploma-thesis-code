#include "detection/multicam_pose_optimizer.h"
#include "detection/camera_projection.h"

#include <ceres/ceres.h>
#include <ceres/rotation.h>
#include <ceres/manifold.h>
#include <ceres/solver.h>
#include <ceres/types.h>
#include <ceres/autodiff_cost_function.h>


using namespace aergo::default_modules::pen_tracking_multicam_module;

struct CornerReprojectionResidual
{
    CornerReprojectionResidual(
        const cv::Vec3d& pt_object_pose,
        const cv::Point2f& pt_uv_observed,
        CameraParameters cam_params,
        const pu::SE3& T_cam_ref,
        double weight_quality,
        double weight_z_distance
    )
    : cam_params_(cam_params),
      u_obs_(pt_uv_observed.x),
      v_obs_(pt_uv_observed.y)
    {
        // fixed point in object/pose frame
        Pw_[0] = pt_object_pose[0];
        Pw_[1] = pt_object_pose[1];
        Pw_[2] = pt_object_pose[2];

        // fixed T_cam_ref (world/ref -> cam)
        // Convert Matx33d + Vec3d to plain doubles
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                R_cam_ref_[3*r + c] = T_cam_ref.R(r, c);

        t_cam_ref_[0] = T_cam_ref.t[0];
        t_cam_ref_[1] = T_cam_ref.t[1];
        t_cam_ref_[2] = T_cam_ref.t[2];

        scale_ = std::sqrt(weight_quality) * weight_z_distance;
    }


    template <typename T>
    bool operator()(
        const T* const Q_ref_pose,  // [qw,qx,qy,qz] (Ceres expects w first)
        const T* const t_ref_pose,  // [tx,ty,tz]
        T* res
    ) const
    {
        // 1) p_ref = T_ref_pose * p_obj
        //    p_ref = R(Q)*Pw + t
        T p_ref[3];
        {
            const T Pw[3] = { T(Pw_[0]), T(Pw_[1]), T(Pw_[2]) };

            T p_rot[3];
            ceres::QuaternionRotatePoint(Q_ref_pose, Pw, p_rot);  // Q is [w,x,y,z]

            p_ref[0] = p_rot[0] + t_ref_pose[0];
            p_ref[1] = p_rot[1] + t_ref_pose[1];
            p_ref[2] = p_rot[2] + t_ref_pose[2];
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



static double markerShapeQuality01(const std::array<cv::Point2f,4>& c)
{
    // side lengths
    double s[4];
    for (int i=0;i<4;++i) s[i] = cv::norm(c[i] - c[(i+1)%4]);
    double mean_s = 0.25*(s[0]+s[1]+s[2]+s[3]);
    if (mean_s < 1e-6) return 0.0;

    double var=0.0;
    for(int i=0;i<4;++i){ double e=s[i]-mean_s; var += e*e; }
    var *= 0.25;
    double cv_sides = std::sqrt(var)/mean_s;

    // diagonal mismatch
    double d0 = cv::norm(c[0]-c[2]);
    double d1 = cv::norm(c[1]-c[3]);
    double diag_error = std::abs(d0-d1) / (0.5*(d0+d1) + 1e-6);

    // angle deviation (90° -> cos = 0)
    cv::Point2f e[4];
    for(int i=0;i<4;++i) e[i] = c[(i+1)%4] - c[i];
    auto safe_cos = [](const cv::Point2f& a, const cv::Point2f& b){
        double na = std::hypot(a.x,a.y), nb = std::hypot(b.x,b.y);
        if (na < 1e-6 || nb < 1e-6) return 1.0;
        return (a.x*b.x + a.y*b.y) / (na*nb);
    };
    double angle_dev = 0.0;
    for(int i=0;i<4;++i){
        double cang = safe_cos(e[i], e[(i+1)%4]);
        angle_dev += std::abs(cang); // 0 is best
    }
    angle_dev *= 0.25;

    // combine
    const double alpha = 4.0, beta = 4.0, gamma = 2.0;
    double q = std::exp(-(alpha*cv_sides + beta*diag_error + gamma*angle_dev));
    return std::clamp(q, 0.0, 1.0);
}

static double markerQualityWeight(const std::array<cv::Point2f,4>& c)
{
    double q = markerShapeQuality01(c);
    // separate a bit more, and bound
    double w = std::pow(q, 2.0);
    return std::clamp(w, 0.25, 1.0);
}





MulticamPoseOptimizer::MulticamPoseOptimizer(
    std::map<int, MulticamPoseEstimator::MarkerData> markersData, 
    double cauchyLoss
)
: markersData_(std::move(markersData)),
  cauchyLoss_(cauchyLoss)
{
    if (markersData_.empty())
    {
        throw std::invalid_argument("markersData cannot be empty");
    }
}


void validateCamerasData(const std::vector<MulticamPoseEstimator::CameraData>& camerasData)
{
    if (camerasData.empty())
    {
        throw std::invalid_argument("camerasData cannot be empty");
    }
    for (const auto& camData : camerasData)
    {
        if (camData.K.empty() || camData.D.empty())
        {
            throw std::invalid_argument("Camera intrinsics (K, D) cannot be empty");
        }
        if (camData.K.rows != 3 || camData.K.cols != 3 || camData.K.type() != CV_64F)
        {
            throw std::invalid_argument("Camera intrinsic matrix K must be 3x3 of type CV_64F");
        }
        if (camData.D.rows != 1 || camData.D.cols != 5 || camData.D.type() != CV_64F)
        {
            throw std::invalid_argument("Camera distortion coefficients D must be 1x5 of type CV_64F");
        }
    }
}


void MulticamPoseOptimizer::optimizePoses(
    const std::vector<MulticamPoseEstimator::CameraData>& camerasData,
    pu::SE3 T_ref_pose_initial,
    std::span<MarkerDetector::DetectionResult> detectedMarkersPerCamera,
    Result& outResult,
    bool use_wz_from_first_camera
)
{
    cv::Vec4d Q_ref_pose_init; // qw,qx,qy,qz
    cv::Vec3d t_ref_pose_init;
    T_ref_pose_initial.toQuatTvec(Q_ref_pose_init, t_ref_pose_init, true);

    double Q_ref_pose[4] = { Q_ref_pose_init[0], Q_ref_pose_init[1], Q_ref_pose_init[2], Q_ref_pose_init[3] };
    double t_ref_pose[3] = { t_ref_pose_init[0], t_ref_pose_init[1], t_ref_pose_init[2] };

    ceres::Problem problem;
    problem.AddParameterBlock(Q_ref_pose, 4, new ceres::QuaternionManifold());
    problem.AddParameterBlock(t_ref_pose, 3);

    ceres::LossFunction* loss = new ceres::CauchyLoss(cauchyLoss_);

    // Add reprojection error terms for all detected corners in all cameras
    for (size_t camIdx = 0; camIdx < camerasData.size(); ++camIdx)
    {
        const auto& camData = camerasData[camIdx];
        const auto& detectionResult = detectedMarkersPerCamera[camIdx];

        CameraParameters cam_params = CameraParameters::fromOpenCV(camData.K, camData.D);
        const pu::SE3 T_cam_ref = camData.T_cam_ref;


        const pu::SE3 T_wzcam_pose = camerasData[use_wz_from_first_camera ? 0 : camIdx].T_cam_ref * T_ref_pose_initial;

        double Zref = 0.6; // meters; normalized to 60cm detection distance
        double Z = T_wzcam_pose.t[2];
        if (!std::isfinite(Z)) Z = Zref;
        Z = std::clamp(Z, 0.15, 3.0);
        double wZ = std::clamp(Z / Zref, 0.8, 2.0); // weight factor based on distance
        
        for (const auto& detectedMarker : detectionResult.markers)
        {
            const int markerId = detectedMarker.id;
            auto markerIt = markersData_.find(markerId);
            if (markerIt == markersData_.end())
            {
                continue; // unknown marker
            }
            const auto& markerData = markerIt->second;
            double wQ = markerQualityWeight(detectedMarker.corners);

            const pu::SE3 T_pose_marker = markerData.T_marker_pose().inverse(); // pose <- marker
            // For each corner
            for (size_t cornerIdx = 0; cornerIdx < 4; ++cornerIdx)
            {
                const cv::Point2f& pt_uv_observed = detectedMarker.corners[cornerIdx];
                const cv::Vec3f& pt_object_marker = markerData.objectPoints()[cornerIdx];
                const cv::Vec3d pt_object_pose = cv::Vec3d(T_pose_marker * pt_object_marker); // point in pose frame

                ceres::CostFunction* cost_function =
                    new ceres::AutoDiffCostFunction<CornerReprojectionResidual, 2, 4, 3>(
                        new CornerReprojectionResidual(
                            pt_object_pose,
                            pt_uv_observed,
                            cam_params,
                            T_cam_ref,
                            wQ,
                            wZ
                        )
                    );
                problem.AddResidualBlock(cost_function, loss, Q_ref_pose, t_ref_pose);
            }
        }
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

    // Output
    outResult.success = outResult.summary.termination_type == ceres::CONVERGENCE || outResult.summary.termination_type == ceres::NO_CONVERGENCE;
    outResult.T_ref_pose_optimized = pu::SE3::fromQuatTvec(
        cv::Vec4d(Q_ref_pose[0], Q_ref_pose[1], Q_ref_pose[2], Q_ref_pose[3]),
        cv::Vec3d(t_ref_pose[0], t_ref_pose[1], t_ref_pose[2]),
        true
    );
}