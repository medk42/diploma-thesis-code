#include "calib/stereo_calibrator.h"

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>
#include <algorithm>
#include <numeric>

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    namespace
    {
        // Compute Sampson distance for a single correspondence (p1 in left, p2 in right)
        double sampsonDistance(const cv::Point2f& p1, const cv::Point2f& p2, const cv::Mat& F)
        {
            cv::Matx33d Fm;
            F.convertTo(Fm, CV_64F);

            const cv::Vec3d x1(p1.x, p1.y, 1.0);
            const cv::Vec3d x2(p2.x, p2.y, 1.0);

            const cv::Vec3d Fx1 = Fm * x1;
            const cv::Vec3d Ftx2 = Fm.t() * x2;
            const double x2tFx1 = x2[0] * Fx1[0] + x2[1] * Fx1[1] + x2[2] * Fx1[2];

            const double denom = Fx1[0] * Fx1[0] + Fx1[1] * Fx1[1] + Ftx2[0] * Ftx2[0] + Ftx2[1] * Ftx2[1];
            if (denom <= 0.0)
            {
                return -1.0;
            }
            return (x2tFx1 * x2tFx1) / denom;
        }
    }

    StereoCalibrator::StereoCalibrator(const Params& p)
        : prm_(p)
    {
    }

    std::vector<StereoCalibrator::Pair> StereoCalibrator::buildPairs(const std::vector<CharucoDetection>& viewsL,
                                                                     const std::vector<CharucoDetection>& viewsR,
                                                                     const CharucoBoardModel& board) const
    {
        std::vector<Pair> pairs;
        const size_t count = std::min(viewsL.size(), viewsR.size());
        pairs.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            const auto& l = viewsL[i];
            const auto& r = viewsR[i];
            if (!l.ok || !r.ok)
            {
                continue;
            }

            std::vector<int> shared;
            shared.reserve(std::min(l.ids.size(), r.ids.size()));

            // Build lookup for right ids
            std::vector<int> idsR = r.ids;
            std::sort(idsR.begin(), idsR.end());

            for (int id : l.ids)
            {
                if (std::binary_search(idsR.begin(), idsR.end(), id))
                {
                    shared.push_back(id);
                }
            }

            if (static_cast<int>(shared.size()) < prm_.minSharedCharucoCorners)
            {
                continue;
            }

            std::sort(shared.begin(), shared.end());

            Pair p;
            p.index = static_cast<int>(i);
            p.ids = shared;

            const auto obj = board.boardPointsFromIds(shared);

            for (size_t k = 0; k < shared.size(); ++k)
            {
                const int id = shared[k];

                auto itL = std::find(l.ids.begin(), l.ids.end(), id);
                auto itR = std::find(r.ids.begin(), r.ids.end(), id);
                if (itL == l.ids.end() || itR == r.ids.end())
                {
                    continue;
                }

                const size_t idxL = static_cast<size_t>(std::distance(l.ids.begin(), itL));
                const size_t idxR = static_cast<size_t>(std::distance(r.ids.begin(), itR));

                p.imgL.push_back(l.corners2d[idxL]);
                p.imgR.push_back(r.corners2d[idxR]);
                p.objPts.push_back(obj[k]);
            }

            if (static_cast<int>(p.objPts.size()) >= prm_.minSharedCharucoCorners)
            {
                pairs.push_back(std::move(p));
            }
        }

        return pairs;
    }

    StereoCalibrator::Result StereoCalibrator::calibrate(const std::vector<CharucoDetection>& viewsL,
                                                         const std::vector<CharucoDetection>& viewsR,
                                                         const CharucoBoardModel& board,
                                                         const CameraIntrinsics& KL,
                                                         const CameraIntrinsics& KR) const
    {
        Result res;

        if (KL.imageSize.width <= 0 || KL.imageSize.height <= 0 || KR.imageSize != KL.imageSize)
        {
            res.message = "StereoCalibrator: invalid or mismatched image sizes.";
            return res;
        }

        const auto pairs = buildPairs(viewsL, viewsR, board);
        if (static_cast<int>(pairs.size()) < prm_.minPairs)
        {
            res.message = "StereoCalibrator: not enough usable pairs.";
            return res;
        }

        std::vector<std::vector<cv::Point3f>> objectPoints;
        std::vector<std::vector<cv::Point2f>> imagePointsL;
        std::vector<std::vector<cv::Point2f>> imagePointsR;
        objectPoints.reserve(pairs.size());
        imagePointsL.reserve(pairs.size());
        imagePointsR.reserve(pairs.size());

        for (const auto& p : pairs)
        {
            objectPoints.push_back(p.objPts);
            imagePointsL.push_back(p.imgL);
            imagePointsR.push_back(p.imgR);
            res.usedPairIndices.push_back(p.index);
        }

        int flags = 0;
        if (prm_.fixIntrinsics)
        {
            flags |= cv::CALIB_FIX_INTRINSIC;
        }

        cv::Mat R, T, E, F;
        const double rms = cv::stereoCalibrate(
            objectPoints,
            imagePointsL,
            imagePointsR,
            KL.K,
            KL.D,
            KR.K,
            KR.D,
            KL.imageSize,
            R,
            T,
            E,
            F,
            flags,
            cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, 1e-9));

        res.extr.R_RL = R;
        res.extr.t_RL = cv::Vec3d(T);
        res.E = E;
        res.F = F;
        res.rms = rms;

        const auto stats = epipolarStats(pairs, F);
        res.meanSampson = stats.first;
        res.medianSampson = stats.second;

        if (prm_.computeRectification)
        {
            rectify(KL, KR, res.extr, KL.imageSize, prm_.rectifyAlpha, res.RL_rect, res.RR_rect, res.PL, res.PR, res.Q);
        }

        res.ok = true;
        return res;
    }

    std::pair<double, double> StereoCalibrator::epipolarStats(const std::vector<Pair>& pairs,
                                                              const cv::Mat& F) const
    {
        if (pairs.empty() || F.empty())
        {
            return {-1.0, -1.0};
        }

        std::vector<double> dists;
        for (const auto& p : pairs)
        {
            const size_t n = std::min(p.imgL.size(), p.imgR.size());
            for (size_t i = 0; i < n; ++i)
            {
                const double d = sampsonDistance(p.imgL[i], p.imgR[i], F);
                if (d >= 0.0)
                {
                    dists.push_back(std::sqrt(d));
                }
            }
        }

        if (dists.empty())
        {
            return {-1.0, -1.0};
        }

        const double mean = std::accumulate(dists.begin(), dists.end(), 0.0) / static_cast<double>(dists.size());

        std::nth_element(dists.begin(), dists.begin() + dists.size() / 2, dists.end());
        const double median = dists[dists.size() / 2];

        return {mean, median};
    }

    void StereoCalibrator::rectify(const CameraIntrinsics& KL, const CameraIntrinsics& KR,
                                   const StereoExtrinsics& RL,
                                   const cv::Size& imageSize,
                                   double alpha,
                                   cv::Mat& RL_rect, cv::Mat& RR_rect, cv::Mat& PL,
                                   cv::Mat& PR, cv::Mat& Q) const
    {
        cv::stereoRectify(
            KL.K,
            KL.D,
            KR.K,
            KR.D,
            imageSize,
            RL.R_RL,
            RL.t_RL,
            RL_rect,
            RR_rect,
            PL,
            PR,
            Q,
            cv::CALIB_ZERO_DISPARITY,
            alpha);
    }
}
