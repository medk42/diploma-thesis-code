#include "calib/rig_refiner_ceres.h"

#include <ceres/ceres.h>
#include <ceres/rotation.h>
#include <ceres/manifold.h>
#include <ceres/solver.h>
#include <ceres/types.h>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <limits>

#include "calib/pose_utils.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    namespace
    {
        template <typename T>
        inline void quatProduct(const T* a, const T* b, T* out)
        {
            // Both are [w,x,y,z]
            out[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
            out[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
            out[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
            out[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
        }

        template <typename T>
        inline void composePose(const T* qa, const T* ta, const T* qb, const T* tb, T* q_out, T* t_out)
        {
            quatProduct(qa, qb, q_out);
            T tb_rot[3];
            ceres::QuaternionRotatePoint(qa, tb, tb_rot);
            t_out[0] = ta[0] + tb_rot[0];
            t_out[1] = ta[1] + tb_rot[1];
            t_out[2] = ta[2] + tb_rot[2];
        }

        inline void matToQuatWxyz(const cv::Matx33d& R, double q[4])
        {
            const auto qcv = pose_utils::rToQuat(R); // x,y,z,w
            q[0] = qcv.w;
            q[1] = qcv.x;
            q[2] = qcv.y;
            q[3] = qcv.z;
        }

        inline cv::Matx33d quatToMat(const double q[4])
        {
            Quaternion qc{ q[1], q[2], q[3], q[0] };
            return pose_utils::quatToR(qc);
        }

        struct Observation
        {
            cv::Point2f uv;
            cv::Point3f Pw; // board point in board frame
            size_t frame_idx;
            bool is_right{false};
            double q_fw[4]; // flange <- world (quaternion wxyz)
            double t_fw[3]; // flange <- world
        };

        template <bool HasWorldBoard>
        struct ReprojResidual
        {
            ReprojResidual(const cv::Point2f& uv,
                           const cv::Point3f& Pw,
                           const double q_fw[4],
                           const double t_fw[3],
                           bool is_right,
                           const std::vector<double>& dist,
                           double fx, double fy, double cx, double cy)
                : uv_(uv),
                  Pw_(Pw),
                  is_right_(is_right),
                  dist_(dist),
                  fx_(fx), fy_(fy), cx_(cx), cy_(cy)
            {
                std::copy(q_fw, q_fw + 4, q_fw_);
                std::copy(t_fw, t_fw + 3, t_fw_);
            }

            template <typename T>
            bool operator()(const T* const q_LF, const T* const t_LF,
                            const T* const q_RL, const T* const t_RL,
                            const T* const q_WB, const T* const t_WB,
                            T* residuals) const
            {
                // World <- board
                T q_WB_cur[4];
                T t_WB_cur[3];
                if constexpr (HasWorldBoard)
                {
                    q_WB_cur[0] = q_WB[0]; q_WB_cur[1] = q_WB[1]; q_WB_cur[2] = q_WB[2]; q_WB_cur[3] = q_WB[3];
                    t_WB_cur[0] = t_WB[0]; t_WB_cur[1] = t_WB[1]; t_WB_cur[2] = t_WB[2];
                }
                else
                {
                    q_WB_cur[0] = T(1); q_WB_cur[1] = T(0); q_WB_cur[2] = T(0); q_WB_cur[3] = T(0);
                    t_WB_cur[0] = T(0); t_WB_cur[1] = T(0); t_WB_cur[2] = T(0);
                }

                // flange <- world (constant per observation)
                T q_FW[4] = { T(q_fw_[0]), T(q_fw_[1]), T(q_fw_[2]), T(q_fw_[3]) };
                T t_FW[3] = { T(t_fw_[0]), T(t_fw_[1]), T(t_fw_[2]) };

                // camL <- world
                T q_LW[4];
                T t_LW[3];
                composePose(q_LF, t_LF, q_FW, t_FW, q_LW, t_LW);

                // cam? <- world
                T q_CW[4];
                T t_CW[3];
                if (is_right_)
                {
                    composePose(q_RL, t_RL, q_LW, t_LW, q_CW, t_CW);
                }
                else
                {
                    q_CW[0] = q_LW[0]; q_CW[1] = q_LW[1]; q_CW[2] = q_LW[2]; q_CW[3] = q_LW[3];
                    t_CW[0] = t_LW[0]; t_CW[1] = t_LW[1]; t_CW[2] = t_LW[2];
                }

                // World point
                T Pw[3] = { T(Pw_.x), T(Pw_.y), T(Pw_.z) };
                T Pw_w[3];
                ceres::QuaternionRotatePoint(q_WB_cur, Pw, Pw_w);
                Pw_w[0] += t_WB_cur[0];
                Pw_w[1] += t_WB_cur[1];
                Pw_w[2] += t_WB_cur[2];

                // Camera point
                T Pc[3];
                ceres::QuaternionRotatePoint(q_CW, Pw_w, Pc);
                Pc[0] += t_CW[0];
                Pc[1] += t_CW[1];
                Pc[2] += t_CW[2];

                if (Pc[2] <= T(0))
                {
                    residuals[0] = T(1000);
                    residuals[1] = T(1000);
                    return true;
                }

                T x = Pc[0] / Pc[2];
                T y = Pc[1] / Pc[2];
                T r2 = x * x + y * y;
                const int dlen = static_cast<int>(dist_.size());
                T k1 = dlen > 0 ? T(dist_[0]) : T(0);
                T k2 = dlen > 1 ? T(dist_[1]) : T(0);
                T p1 = dlen > 2 ? T(dist_[2]) : T(0);
                T p2 = dlen > 3 ? T(dist_[3]) : T(0);
                T k3 = dlen > 4 ? T(dist_[4]) : T(0);
                T k4 = dlen > 5 ? T(dist_[5]) : T(0);
                T k5 = dlen > 6 ? T(dist_[6]) : T(0);
                T k6 = dlen > 7 ? T(dist_[7]) : T(0);

                T r4 = r2 * r2;
                T r6 = r4 * r2;
                T r8 = r4 * r4;
                T r10 = r8 * r2;
                T r12 = r6 * r6;
                T radial = T(1.0) + k1 * r2 + k2 * r4 + k3 * r6 + k4 * r8 + k5 * r10 + k6 * r12;

                T x_t = x * radial + T(2.0) * p1 * x * y + p2 * (r2 + T(2.0) * x * x);
                T y_t = y * radial + T(2.0) * p2 * x * y + p1 * (r2 + T(2.0) * y * y);

                T u_hat = T(fx_) * x_t + T(cx_);
                T v_hat = T(fy_) * y_t + T(cy_);

                residuals[0] = T(uv_.x) - u_hat;
                residuals[1] = T(uv_.y) - v_hat;
                return true;
            }

            cv::Point2f uv_;
            cv::Point3f Pw_;
            bool is_right_{false};
            std::vector<double> dist_;
            double fx_{0}, fy_{0}, cx_{0}, cy_{0};
            double q_fw_[4]{};
            double t_fw_[3]{};
        };

        inline std::vector<double> matToDist(const cv::Mat& D)
        {
            std::vector<double> out;
            if (D.empty())
            {
                return out;
            }
            cv::Mat d64;
            D.convertTo(d64, CV_64F);
            d64 = d64.reshape(1, 1);
            out.assign(reinterpret_cast<double*>(d64.data), reinterpret_cast<double*>(d64.data) + d64.total());
            return out;
        }

        inline double reprojRmse(const std::vector<double>& errs)
        {
            if (errs.empty()) return -1.0;
            double s = 0.0;
            for (double e : errs) s += e * e;
            return std::sqrt(s / static_cast<double>(errs.size()));
        }
    } // namespace

    RigRefinerCeres::RigRefinerCeres(const Options& o)
        : opt_(o)
    {
    }

    RigRefinerCeres::Result RigRefinerCeres::refine(const Input& in) const
    {
        Result res;

        if (in.viewsL.size() != in.viewsR.size() || in.viewsL.size() != in.base_from_flange.size())
        {
            res.message = "RigRefinerCeres: input sizes mismatch.";
            return res;
        }

        const size_t N = in.viewsL.size();
        std::vector<Observation> obs;
        obs.reserve(N * 2 * 50);

        // Precompute flange<-world
        for (size_t i = 0; i < N; ++i)
        {
            SE3 Twf = pose_utils::toSE3(in.base_from_flange[i]); // world <- flange
            SE3 Tfw = pose_utils::invert(Twf);                   // flange <- world
            double q_fw[4];
            matToQuatWxyz(Tfw.R, q_fw);
            double t_fw[3] = { Tfw.t[0], Tfw.t[1], Tfw.t[2] };

            const auto add_cam = [&](const CharucoDetection& det, bool is_right)
            {
                if (!det.ok || det.ids.empty()) return;
                for (size_t k = 0; k < det.ids.size(); ++k)
                {
                    const int id = det.ids[k];
                    auto pts = in.board.boardPointsFromIds({id});
                    if (pts.empty()) continue;
                    Observation o;
                    o.uv = det.corners2d[k];
                    o.Pw = pts.front();
                    o.frame_idx = i;
                    o.is_right = is_right;
                    std::copy(q_fw, q_fw + 4, o.q_fw);
                    std::copy(t_fw, t_fw + 3, o.t_fw);
                    obs.push_back(o);
                }
            };

            add_cam(in.viewsL[i], false);
            add_cam(in.viewsR[i], true);
        }

        if (obs.size() < 200)
        {
            res.message = "RigRefinerCeres: not enough observations.";
            return res;
        }

        // Params
        double q_LF[4]; matToQuatWxyz(in.camL_from_flange.R, q_LF);
        double t_LF[3] = { in.camL_from_flange.t[0], in.camL_from_flange.t[1], in.camL_from_flange.t[2] };

        double q_RL[4]; matToQuatWxyz(in.RL.R_RL, q_RL);
        double t_RL[3] = { in.RL.t_RL[0], in.RL.t_RL[1], in.RL.t_RL[2] };

        double q_WB[4] = {1,0,0,0};
        double t_WB[3] = {0,0,0};

        auto distL = matToDist(in.KL.D);
        auto distR = matToDist(in.KR.D);

        ceres::Problem problem;
        problem.AddParameterBlock(q_LF, 4);
        problem.AddParameterBlock(t_LF, 3);
        problem.AddParameterBlock(q_RL, 4);
        problem.AddParameterBlock(t_RL, 3);
        problem.AddParameterBlock(q_WB, 4);
        problem.AddParameterBlock(t_WB, 3);
        problem.SetManifold(q_LF, new ceres::QuaternionManifold);
        problem.SetManifold(q_RL, new ceres::QuaternionManifold);
        problem.SetManifold(q_WB, new ceres::QuaternionManifold);

        if (!opt_.refineHandEye)
        {
            problem.SetParameterBlockConstant(q_LF);
            problem.SetParameterBlockConstant(t_LF);
        }
        if (!opt_.refineStereo)
        {
            problem.SetParameterBlockConstant(q_RL);
            problem.SetParameterBlockConstant(t_RL);
        }
        if (!opt_.estimateBoardInWorld)
        {
            problem.SetParameterBlockConstant(q_WB);
            problem.SetParameterBlockConstant(t_WB);
        }

        const double fxL = in.KL.K.at<double>(0,0);
        const double fyL = in.KL.K.at<double>(1,1);
        const double cxL = in.KL.K.at<double>(0,2);
        const double cyL = in.KL.K.at<double>(1,2);
        const double fxR = in.KR.K.at<double>(0,0);
        const double fyR = in.KR.K.at<double>(1,1);
        const double cxR = in.KR.K.at<double>(0,2);
        const double cyR = in.KR.K.at<double>(1,2);

        ceres::LossFunction* loss = new ceres::HuberLoss(opt_.huberDelta);

        for (const auto& o : obs)
        {
            const bool is_right = o.is_right;
            const auto& dist = is_right ? distR : distL;
            const double fx = is_right ? fxR : fxL;
            const double fy = is_right ? fyR : fyL;
            const double cx = is_right ? cxR : cxL;
            const double cy = is_right ? cyR : cyL;

            if (opt_.estimateBoardInWorld)
            {
                auto* cost = new ceres::AutoDiffCostFunction<ReprojResidual<true>, 2, 4,3, 4,3, 4,3>(
                    new ReprojResidual<true>(o.uv, o.Pw, o.q_fw, o.t_fw, is_right, dist, fx, fy, cx, cy));
                problem.AddResidualBlock(cost, loss, q_LF, t_LF, q_RL, t_RL, q_WB, t_WB);
            }
            else
            {
                auto* cost = new ceres::AutoDiffCostFunction<ReprojResidual<false>, 2, 4,3, 4,3, 4,3>(
                    new ReprojResidual<false>(o.uv, o.Pw, o.q_fw, o.t_fw, is_right, dist, fx, fy, cx, cy));
                problem.AddResidualBlock(cost, loss, q_LF, t_LF, q_RL, t_RL, q_WB, t_WB);
            }
        }

        ceres::Solver::Options options;
        options.linear_solver_type = ceres::DENSE_SCHUR;
        options.max_num_iterations = opt_.maxIters;
        options.num_threads = std::max(1u, std::thread::hardware_concurrency());
        options.function_tolerance = 1e-12;
        options.parameter_tolerance = 1e-12;

        ceres::Solver::Summary summary;
        ceres::Solve(options, &problem, &summary);

        res.KL = in.KL;
        res.KR = in.KR;
        res.RL.R_RL = quatToMat(q_RL);
        res.RL.t_RL = cv::Vec3d(t_RL[0], t_RL[1], t_RL[2]);
        res.camL_from_flange.R = quatToMat(q_LF);
        res.camL_from_flange.t = cv::Vec3d(t_LF[0], t_LF[1], t_LF[2]);
        res.camR_from_flange = pose_utils::compose(SE3{res.RL.R_RL, res.RL.t_RL}, res.camL_from_flange);
        res.world_from_board.R = quatToMat(q_WB);
        res.world_from_board.t = cv::Vec3d(t_WB[0], t_WB[1], t_WB[2]);

        // Recompute reprojection RMSE
        std::vector<double> errsL, errsR;
        errsL.reserve(obs.size());
        errsR.reserve(obs.size());

        auto project = [&](const Observation& o)
        {
            const bool is_right = o.is_right;
            const auto& K = is_right ? res.KR : res.KL;
            const auto& D = is_right ? distR : distL;
            // flange<-world
            double q_FW[4] = { o.q_fw[0], o.q_fw[1], o.q_fw[2], o.q_fw[3] };
            double t_FW[3] = { o.t_fw[0], o.t_fw[1], o.t_fw[2] };
            double q_LW[4];
            double t_LW[3];
            double q_LF_cur[4] = { q_LF[0], q_LF[1], q_LF[2], q_LF[3] };
            double t_LF_cur[3] = { t_LF[0], t_LF[1], t_LF[2] };
            composePose(q_LF_cur, t_LF_cur, q_FW, t_FW, q_LW, t_LW);

            double q_CW[4];
            double t_CW[3];
            if (is_right)
            {
                double q_RL_cur[4] = { q_RL[0], q_RL[1], q_RL[2], q_RL[3] };
                double t_RL_cur[3] = { t_RL[0], t_RL[1], t_RL[2] };
                composePose(q_RL_cur, t_RL_cur, q_LW, t_LW, q_CW, t_CW);
            }
            else
            {
                q_CW[0] = q_LW[0]; q_CW[1] = q_LW[1]; q_CW[2] = q_LW[2]; q_CW[3] = q_LW[3];
                t_CW[0] = t_LW[0]; t_CW[1] = t_LW[1]; t_CW[2] = t_LW[2];
            }

            double Pw[3] = { o.Pw.x, o.Pw.y, o.Pw.z };
            double Pw_w[3];
            double q_WB_cur[4] = { q_WB[0], q_WB[1], q_WB[2], q_WB[3] };
            double t_WB_cur[3] = { t_WB[0], t_WB[1], t_WB[2] };
            ceres::QuaternionRotatePoint(q_WB_cur, Pw, Pw_w);
            Pw_w[0] += t_WB_cur[0];
            Pw_w[1] += t_WB_cur[1];
            Pw_w[2] += t_WB_cur[2];

            double Pc[3];
            ceres::QuaternionRotatePoint(q_CW, Pw_w, Pc);
            Pc[0] += t_CW[0];
            Pc[1] += t_CW[1];
            Pc[2] += t_CW[2];
            if (Pc[2] <= 0) return std::numeric_limits<double>::quiet_NaN();

            double x = Pc[0] / Pc[2];
            double y = Pc[1] / Pc[2];
            double r2 = x * x + y * y;
            auto dvec = matToDist(K.D);
            int dlen = static_cast<int>(dvec.size());
            double k1 = dlen > 0 ? dvec[0] : 0.0;
            double k2 = dlen > 1 ? dvec[1] : 0.0;
            double p1 = dlen > 2 ? dvec[2] : 0.0;
            double p2 = dlen > 3 ? dvec[3] : 0.0;
            double k3 = dlen > 4 ? dvec[4] : 0.0;
            double k4 = dlen > 5 ? dvec[5] : 0.0;
            double k5 = dlen > 6 ? dvec[6] : 0.0;
            double k6 = dlen > 7 ? dvec[7] : 0.0;
            double r4 = r2 * r2;
            double r6 = r4 * r2;
            double r8 = r4 * r4;
            double r10 = r8 * r2;
            double r12 = r6 * r6;
            double radial = 1.0 + k1 * r2 + k2 * r4 + k3 * r6 + k4 * r8 + k5 * r10 + k6 * r12;
            double x_t = x * radial + 2.0 * p1 * x * y + p2 * (r2 + 2.0 * x * x);
            double y_t = y * radial + 2.0 * p2 * x * y + p1 * (r2 + 2.0 * y * y);
            double u_hat = K.K.at<double>(0,0) * x_t + K.K.at<double>(0,2);
            double v_hat = K.K.at<double>(1,1) * y_t + K.K.at<double>(1,2);
            double du = o.uv.x - u_hat;
            double dv = o.uv.y - v_hat;
            return std::sqrt(du * du + dv * dv);
        };

        for (const auto& o : obs)
        {
            double e = project(o);
            if (std::isnan(e)) continue;
            if (o.is_right) errsR.push_back(e); else errsL.push_back(e);
        }

        res.finalReprojRmseL = reprojRmse(errsL);
        res.finalReprojRmseR = reprojRmse(errsR);

        // Stereo Sampson median
        {
            // Build maps of left points per frame/id
            struct Key { size_t f; int id; bool operator==(const Key& o) const { return f==o.f && id==o.id; } };
            struct KeyHash { size_t operator()(const Key& k) const { return std::hash<size_t>()((k.f<<32) ^ static_cast<size_t>(k.id)); } };
            std::unordered_map<Key, cv::Point2f, KeyHash> left_map;
            for (size_t i = 0; i < in.viewsL.size(); ++i)
            {
                const auto& v = in.viewsL[i];
                for (size_t k = 0; k < v.ids.size(); ++k)
                {
                    left_map.insert({Key{i, v.ids[k]}, v.corners2d[k]});
                }
            }

            cv::Matx33d KLi(res.KL.K), KRi(res.KR.K);
            cv::Matx33d R = res.RL.R_RL;
            cv::Vec3d t = res.RL.t_RL;
            cv::Matx33d tx = {
                0, -t[2], t[1],
                t[2], 0, -t[0],
                -t[1], t[0], 0
            };
            cv::Matx33d E = tx * R;
            cv::Matx33d F = KRi.t().inv() * E * KLi.inv();

            std::vector<double> samps;
            for (size_t i = 0; i < in.viewsR.size(); ++i)
            {
                const auto& vr = in.viewsR[i];
                for (size_t k = 0; k < vr.ids.size(); ++k)
                {
                    Key key{i, vr.ids[k]};
                    auto it = left_map.find(key);
                    if (it == left_map.end()) continue;
                    const auto& pl = it->second;
                    const auto& pr = vr.corners2d[k];
                    cv::Vec3d xl(pl.x, pl.y, 1.0);
                    cv::Vec3d xr(pr.x, pr.y, 1.0);
                    cv::Vec3d Fx1 = F * xl;
                    cv::Vec3d Ftx2 = F.t() * xr;
                    double x2tFx1 = xr.dot(Fx1);
                    double denom = Fx1[0]*Fx1[0] + Fx1[1]*Fx1[1] + Ftx2[0]*Ftx2[0] + Ftx2[1]*Ftx2[1];
                    if (denom <= 0) continue;
                    double d = std::sqrt((x2tFx1 * x2tFx1) / denom);
                    samps.push_back(d);
                }
            }
            if (!samps.empty())
            {
                std::nth_element(samps.begin(), samps.begin() + samps.size()/2, samps.end());
                res.medianSampson = samps[samps.size()/2];
            }
        }

        res.ok = summary.termination_type == ceres::CONVERGENCE || summary.termination_type == ceres::USER_SUCCESS;
        res.message = summary.BriefReport();
        return res;
    }
}
