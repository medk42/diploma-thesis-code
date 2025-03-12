#ifndef AERGO_PEN_CALIBRATION_H
#define AERGO_PEN_CALIBRATION_H

#include <set>

#include <opencv2/opencv.hpp>
#include <ceres/ceres.h>

#include "pen_calibration_helper.h"

namespace aergo::pen_calibration
{
    struct PenCalibrationResult
    {
        enum class Result 
        { 
            SUCCESS, 
            FAILED_TO_BUILD_GRAPH, // Some markers were not visible or could not be reached from fixed marker
            SANITY_CHECK_FAIL,     // One of the sanity checks failed - not all cameras could be reached in the transformation graph
            SOLVER_NO_CONVERGENCE, // Solver did not converge
            SOLVER_FAIL,           // Solved failed in other way aside from failure to converge
            MARKER_POSITION_FAIL   // Failed to determine marker positions
        };

        Result result_;

        struct
        {
            double init_mre_;          // Mean reprojection error after automatic initialization
            double init_rmsre_;        // Root mean squared reprojection error after automatic initialization
    
            double final_mre_;         // Mean reprojection error after Ceres optimization
            double final_rmsre_;       // Root mean squared reprojection error after Ceres optimization
        } metrics_;
        

        struct 
        {
            double solver_time_;         // Solver run time in seconds.
            double solver_initial_cost_; // Initial cost reported by solver.
            double solver_final_cost_;   // Final cost reported by solver.
        } solver_stats_;
        

        struct
        {
            int marker_id_0_;
            int marker_id_45_;
            int marker_id_90_;
            int marker_id_135_;
            int marker_id_180_;
            int marker_id_225_;
            int marker_id_270_;
            int marker_id_315_;
        } marker_position_data_;        // IDs of markers at specific rotation to default marker

        std::map<int, helper::Transformation> origin_to_other_transformations;
    };

    class PenCalibration
    {
    public:

        PenCalibration(
            cv::Mat camera_matrix, cv::Mat distortion_coefficients, 
            cv::aruco::ArucoDetector aruco_detector, const std::set<int>& used_marker_ids,
            float marker_size, double ignore_markers_above_angle, int fixed_marker_id
        );

        ~PenCalibration() = default;

        /// @param image image to add to calibration
        /// @param return_visualization draw the visualization of the result onto an image, only drawn if image added successfully
        /// @return true on success, false on failure (image empty, not enough markers found)
        bool addImage(cv::Mat image, cv::Mat* return_visualization = nullptr);

        /// @brief Run pen calibration. Function call is slow, since optimizer is used - should finish in 10-20s, at most a few minutes.
        PenCalibrationResult calibratePen();

    private:

        std::vector<cv::Point3f> getMarkerPoints3d();
        void buildTransformationGraph(helper::TransformationGraph& graph);
        void traverseGraph(const helper::TransformationGraph& transf_graph, std::map<int, helper::Transformation>& fixed_marker_to_other_transformations, int start_node);
        std::pair<double, double> calculateMRE_RMSRE(std::map<int, helper::Transformation>& fixed_marker_to_others);

        void runOptimizer(std::map<int, helper::Transformation>& fixed_marker_to_other_transformations, PenCalibrationResult& result, std::map<int, helper::Transformation>& fixed_marker_to_other_transformations_out);
        void optimizerPrepareData(std::map<int, helper::Transformation>& fixed_marker_to_other_transformations, std::vector<double>& rvec_tvec_markers, std::map<int, int>& marker_id_to_i, std::vector<double>& rvec_tvec_cameras, std::map<int, int>& camera_id_to_i);
        void optimizerPrepareProblem(ceres::Problem& problem, std::vector<double>& rvec_tvec_markers, std::map<int, int>& marker_id_to_i, std::vector<double>& rvec_tvec_cameras, std::map<int, int>& camera_id_to_i);
        void optimizerCollectResults(std::map<int, helper::Transformation>& fixed_marker_to_other_transformations_out, std::vector<double>& rvec_tvec_markers, std::map<int, int>& marker_id_to_i, std::vector<double>& rvec_tvec_cameras, std::map<int, int>& camera_id_to_i);

        /// @brief Find the closest marker to specified angle with z axis as reference (pointing towards the tip of the pen) for rotation direction.
        /// @param angle_deg positive direction is anti-clockwise when looking into z.
        int findClosestMarker(std::map<int, helper::Transformation>& fixed_to_other_transformations, double angle_deg, cv::Point3d z_estimate);
        void determineMarkerPositions(std::map<int, helper::Transformation>& fixed_to_other_transformations, PenCalibrationResult& result);
        double calculateVectorVectorAngleDeg(cv::Point3d vector_1, cv::Point3d vector_2);

        /// @brief Estimate the z axis (pointing towards the tip) using detected marker positions.
        std::optional<cv::Point3d> estimateZAxis(std::map<int, helper::Transformation>& fixed_to_other_transformations);

        void determinePenOrigin(std::map<int, helper::Transformation>& fixed_to_other_transformations, PenCalibrationResult& result);
        cv::Point3d determineMidpoint(std::map<int, helper::Transformation>& fixed_to_other_transformations, int front_id, int back_id, int left_id, int right_id);

        cv::Mat camera_matrix_;
        cv::Mat distortion_coefficients_;
        cv::aruco::ArucoDetector aruco_detector_;
        const std::set<int>& used_marker_ids_;
        float marker_size_;
        double ignore_markers_above_angle_;
        int fixed_marker_id_;
        std::vector<cv::Point3f> marker_points_;

        int camera_count_;
        int camera_first_id_;

        std::vector<std::vector<helper::observed_marker>> observed_markers_;
    };

} // namespace aergo::pen_calibration

#endif // AERGO_PEN_CALIBRATION_H