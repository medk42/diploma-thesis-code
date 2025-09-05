#include "pen_calibration_helper.h"

using namespace aergo::pen_calibration::helper;

cv::Point3d cv_extensions::asPoint(const cv::Mat& mat)
{
    return std::move(
        cv::Point3d(
            mat.at<double>(0),
            mat.at<double>(1),
            mat.at<double>(2)
        )
    );
}



cv::Mat cv_extensions::asMat(const cv::Point3d& point)
{
    cv::Mat mat(3, 1, CV_64F); 
    mat.at<double>(0) = point.x;
    mat.at<double>(1) = point.y;
    mat.at<double>(2) = point.z;

    return std::move(mat);
}



Transformation::Transformation()
: rotation(cv::Mat::eye(3, 3, CV_64F)), translation(cv::Mat::zeros(3, 1, CV_64F)) {}



Transformation::Transformation(const cv::Mat& rvec, const cv::Mat& tvec)
{
    cv::Rodrigues(rvec, rotation);
    translation = tvec.clone();

    rotation.clone().convertTo(rotation, CV_64F);
    translation.clone().convertTo(translation, CV_64F);
}



Transformation::Transformation(double rvec0, double rvec1, double rvec2, double tvec0, double tvec1, double tvec2)
{
    cv::Mat rvec(3, 1, CV_64F); 
    rvec.at<double>(0) = rvec0;
    rvec.at<double>(1) = rvec1;
    rvec.at<double>(2) = rvec2;

    cv::Mat tvec(3, 1, CV_64F); 
    tvec.at<double>(0) = tvec0;
    tvec.at<double>(1) = tvec1;
    tvec.at<double>(2) = tvec2;

    cv::Rodrigues(rvec, rotation);
    translation = tvec;
    
    rotation.clone().convertTo(rotation, CV_64F);
    translation.clone().convertTo(translation, CV_64F);
}



std::pair<cv::Mat, cv::Mat> Transformation::asRvecTvec() const
{
    cv::Mat rvec;
    cv::Rodrigues(rotation, rvec);  // Convert rotation matrix back to rotation vector
    cv::Mat tvec = translation.clone();  // Clone the translation vector
    return std::make_pair(rvec, tvec);
}



Transformation Transformation::operator*(const Transformation& other) const
{
    Transformation result;
    result.rotation = this->rotation * other.rotation;
    result.translation = this->rotation * other.translation + this->translation;
    return result;
}



cv::Point3d Transformation::operator*(const cv::Point3d& other) const
{
    cv::Mat other_mat = cv_extensions::asMat(other);
    
    cv::Mat res = rotation * other_mat;
    res += translation;

    return cv_extensions::asPoint(res);
}



Transformation Transformation::inverse() const
{
    Transformation inv;
    cv::transpose(this->rotation, inv.rotation);
    inv.translation = -inv.rotation * this->translation;
    return inv;
}



double Transformation::angleDeg() const
{
    auto [rvec, tvec] = asRvecTvec();
    return cv::norm(rvec) * 180.0 / CV_PI;
}



void Transformation::print() const
{
    std::cout << "Rotation:\n" << this->rotation << std::endl;
    std::cout << "Translation:\n" << this->translation << std::endl;
}



cv::Point3d Transformation::normal(cv::Point3d normal_dir) const
{
    cv::Mat normal_dir_mat = cv_extensions::asMat(normal_dir);
    cv::Mat fixed_normal_mat = rotation * normal_dir_mat;
    return cv_extensions::asPoint(fixed_normal_mat);
}



void TransformationGraph::addEdge(int start_node, int end_node, Transformation transformation)
{
    graph_data_[start_node].push_back(std::make_pair(end_node, transformation));
}



const TransformationGraph::edge_list& TransformationGraph::getEdges(int node) const
{
    if (!graph_data_.contains(node))
    {
        return empty_list;
    }
    
    return graph_data_.at(node);
}



CameraMarkerCostFunctor::CameraMarkerCostFunctor(cv::Mat& camera_matrix, cv::Mat& distortion_coefficients, observed_marker marker, std::vector<cv::Point3f>& marker_points)
: camera_matrix_(camera_matrix), distortion_coefficients_(distortion_coefficients), marker_(marker), marker_points_(marker_points)
{}



bool CameraMarkerCostFunctor::operator()(const double* camera_rvec_tvec, const double* marker_rvec_tvec, double* residuals) const
{
    cv::Mat cam_rvec = (cv::Mat_<double>(3, 1) << camera_rvec_tvec[0], camera_rvec_tvec[1], camera_rvec_tvec[2]);
    cv::Mat cam_tvec = (cv::Mat_<double>(3, 1) << camera_rvec_tvec[3], camera_rvec_tvec[4], camera_rvec_tvec[5]);

    cv::Mat mark_rvec = (cv::Mat_<double>(3, 1) << marker_rvec_tvec[0], marker_rvec_tvec[1], marker_rvec_tvec[2]);
    cv::Mat mark_tvec = (cv::Mat_<double>(3, 1) << marker_rvec_tvec[3], marker_rvec_tvec[4], marker_rvec_tvec[5]);

    Transformation fixed_to_camera(cam_rvec, cam_tvec);
    Transformation fixed_to_marker(mark_rvec, mark_tvec);

    Transformation camera_to_marker = fixed_to_camera.inverse() * fixed_to_marker;
    auto [rvec, tvec] = camera_to_marker.asRvecTvec();
    
    std::vector<cv::Point2f> projected_points;

    cv::projectPoints(marker_points_, rvec, tvec, camera_matrix_, distortion_coefficients_, projected_points);

    for (int i = 0; i < 4; ++i)
    {
        cv::Point2f diff = projected_points[i] - marker_.markers_points_[i];
        residuals[2 * i] = diff.x;
        residuals[2 * i + 1] = diff.y;
    }

    return true;
}