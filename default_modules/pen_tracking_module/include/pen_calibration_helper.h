#ifndef AERGO_PEN_CALIBRATION_HELPER_H
#define AERGO_PEN_CALIBRATION_HELPER_H

#include <array>

#include <opencv2/opencv.hpp>

namespace aergo::pen_calibration::helper
{
    namespace cv_extensions
    {
        cv::Point3d asPoint(const cv::Mat& mat);
        cv::Mat asMat(const cv::Point3d& point);
    };

    struct Transformation {
        cv::Mat rotation;    // 3x3 rotation matrix
        cv::Mat translation; // 3x1 translation vector
    
        Transformation();
    
        Transformation(const cv::Mat& rvec, const cv::Mat& tvec);
    
        Transformation(double rvec0, double rvec1, double rvec2, double tvec0, double tvec1, double tvec2);
    
        std::pair<cv::Mat, cv::Mat> asRvecTvec() const;
    
        /// Overload the multiply operator to perform the transformation compositions
        Transformation operator*(const Transformation& other) const;
    
        cv::Point3d operator*(const cv::Point3d& other) const;
    
        Transformation inverse() const;

        double angleDeg() const;
    
        void print() const;
        
        cv::Point3d normal(cv::Point3d normal_dir = cv::Point3d(0, 0, 1)) const;
    };

    struct observed_marker
    {
        std::array<cv::Point2f, 4> markers_points_;
        Transformation camera_to_marker_;
        int marker_id_;
        int camera_id_;
    };

    class TransformationGraph
    {
        typedef std::list<std::pair<int, Transformation>> edge_list;

    public:

        /// @brief Add specified transformation in (start, end) direction.
        void addEdge(int start_node, int end_node, Transformation transformation);

        const edge_list& getEdges(int node) const;

    private:
        std::map<int, edge_list> graph_data_;
        edge_list empty_list;
    };

    struct CameraMarkerCostFunctor
    {
        CameraMarkerCostFunctor(cv::Mat& camera_matrix, cv::Mat& distortion_coefficients, observed_marker marker, std::vector<cv::Point3f>& marker_points);

        /// @brief Calculate the residual error for a specific camera-marker pair. 
        /// @param camera_rvec_tvec 6 doubles, rvec and tvec for camera, fixed_to_camera transformation
        /// @param marker_rvec_tvec 6 doubles, rvec and tvec for marker, fixed_to_marker transformation
        /// @param residuals 8 doubles, error in u and v in the image plane for each of the 4 marker corners
        bool operator()(const double* camera_rvec_tvec, const double* marker_rvec_tvec, double* residuals) const;

        cv::Mat& camera_matrix_;
        cv::Mat& distortion_coefficients_;
        observed_marker marker_;
        std::vector<cv::Point3f>& marker_points_;
    };

} // namespace helper

#endif // AERGO_PEN_CALIBRATION_HELPER_H