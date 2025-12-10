#include "calib/charuco_board_model.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    CharucoBoardModel::CharucoBoardModel(const Params& p)
        : params_(p)
    {
        dict_ = cv::aruco::getPredefinedDictionary(params_.dictionary);
        board_ = cv::makePtr<cv::aruco::CharucoBoard>(
            cv::Size(params_.cols, params_.rows),
            params_.squareLength,
            params_.markerLength,
            dict_);
    }

    CharucoBoardModel CharucoBoardModel::Create(const Params& p)
    {
        if (p.rows < 2 || p.cols < 2)
        {
            throw std::invalid_argument("CharucoBoardModel: rows and cols must be >= 2");
        }

        if (p.markerLength <= 0.0f || p.squareLength <= 0.0f || p.markerLength >= p.squareLength)
        {
            throw std::invalid_argument("CharucoBoardModel: invalid marker/square lengths");
        }

        return CharucoBoardModel(p);
    }

    const cv::aruco::Dictionary& CharucoBoardModel::dictionary() const
    {
        return dict_;
    }

    const cv::Ptr<cv::aruco::CharucoBoard>& CharucoBoardModel::board() const
    {
        return board_;
    }

    std::vector<cv::Point3f> CharucoBoardModel::boardPointsFromIds(const std::vector<int>& ids) const
    {
        std::vector<cv::Point3f> pts;
        pts.reserve(ids.size());

        const auto corners = board_->getChessboardCorners();
        for (int id : ids)
        {
            if (id >= 0 && static_cast<size_t>(id) < corners.size())
            {
                pts.push_back(corners[static_cast<size_t>(id)]);
            }
        }

        return pts;
    }

    int CharucoBoardModel::rows() const
    {
        return params_.rows;
    }

    int CharucoBoardModel::cols() const
    {
        return params_.cols;
    }

    float CharucoBoardModel::squareLength() const
    {
        return params_.squareLength;
    }

    float CharucoBoardModel::markerLength() const
    {
        return params_.markerLength;
    }
}
