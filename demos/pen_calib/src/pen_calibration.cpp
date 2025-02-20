#include "pen_calibration.h"
#include "pen_calibration_helper.h"

using namespace aergo::pen_calibration;
using namespace helper;

PenCalibration::PenCalibration(
    cv::Mat camera_matrix, cv::Mat distortion_coefficients, 
    cv::aruco::ArucoDetector aruco_detector, const std::set<int>& used_marker_ids,
    float marker_size, double ignore_markers_above_angle, int fixed_marker_id
) 
: camera_matrix_(std::move(camera_matrix)), 
distortion_coefficients_(std::move(distortion_coefficients)), 
aruco_detector_(std::move(aruco_detector)),
used_marker_ids_(used_marker_ids),
marker_size_(marker_size),
ignore_markers_above_angle_(ignore_markers_above_angle),
fixed_marker_id_(fixed_marker_id),
camera_count_(0),
marker_points_(std::move(getMarkerPoints3d()))
{
    int max_marker_id = *std::max_element(used_marker_ids_.begin(), used_marker_ids_.end());
    camera_first_id_ = (max_marker_id / 1000) * 1000 + 1000;
}



bool PenCalibration::addImage(cv::Mat image, cv::Mat* return_visualization)
{
    if (image.empty())
    {
        return false;
    }

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    std::vector<std::vector<cv::Point2f>> corners;
    std::vector<int> ids;
    aruco_detector_.detectMarkers(gray, corners, ids);

    std::vector<observed_marker> image_markers;
    for (int i = 0; i < corners.size(); ++i)
    {
        if (used_marker_ids_.contains(ids[i]))
        {
            observed_marker marker;
            marker.marker_id_ = ids[i];
            marker.markers_points_[0] = corners[i][0];
            marker.markers_points_[1] = corners[i][1];
            marker.markers_points_[2] = corners[i][2];
            marker.markers_points_[3] = corners[i][3];
            marker.camera_id_ = camera_count_;

            cv::Mat rvec, tvec;
            bool success = cv::solvePnP(
                marker_points_, marker.markers_points_, 
                camera_matrix_, distortion_coefficients_, 
                rvec, tvec, false, cv::SOLVEPNP_IPPE_SQUARE
            );
            if (!success)
            {
                continue;
            }

            double angle_off_axis = cv::abs(180 - cv::norm(rvec) * 180.0 / CV_PI);
            if (angle_off_axis < ignore_markers_above_angle_)
            {
                marker.camera_to_marker_ = Transformation(rvec, tvec);
                image_markers.emplace_back(std::move(marker));
            }
        }
    }
    
    // only use camera if at least two markers got detected.
    if (image_markers.size() < 2)
    {
        return false;
    }

    observed_markers_.emplace_back(std::move(image_markers));
    ++camera_count_;

    if (return_visualization)
    {
        *return_visualization = image.clone();
        cv::aruco::drawDetectedMarkers(*return_visualization, corners, ids);
    }
    
    return true;
}



std::vector<cv::Point3f> PenCalibration::getMarkerPoints3d()
{
    std::vector<cv::Point3f> marker_points = 
    {
        cv::Point3f(-marker_size_ / 2,  marker_size_ / 2, 0),
        cv::Point3f( marker_size_ / 2,  marker_size_ / 2, 0),
        cv::Point3f( marker_size_ / 2, -marker_size_ / 2, 0),
        cv::Point3f(-marker_size_ / 2, -marker_size_ / 2, 0)
    };

    return std::move(marker_points);
}



void PenCalibration::buildTransformationGraph(TransformationGraph& graph)
{
    for (auto& markers_in_camera : observed_markers_)
    {
        for (auto& first_marker : markers_in_camera)
        {
            graph.addEdge(
                first_marker.camera_id_ + camera_first_id_, 
                first_marker.marker_id_,
                first_marker.camera_to_marker_
            );
            graph.addEdge(
                first_marker.marker_id_,
                first_marker.camera_id_ + camera_first_id_, 
                first_marker.camera_to_marker_.inverse()
            );

            for (auto& second_marker: markers_in_camera)
            {
                if (first_marker.marker_id_ != second_marker.marker_id_)
                {
                    Transformation first_to_second = first_marker.camera_to_marker_.inverse() * second_marker.camera_to_marker_;
                    graph.addEdge(
                        first_marker.marker_id_, 
                        second_marker.marker_id_, 
                        first_to_second
                    );
                }
            }
        }
    }
}



void PenCalibration::traverseGraph(const TransformationGraph& transf_graph, std::map<int, Transformation>& fixed_marker_to_other_transformations, int start_node)
{
    fixed_marker_to_other_transformations.clear();

    std::queue<std::pair<int, Transformation>> node_queue;
    std::set<int> seen_nodes;
    node_queue.push(std::make_pair(start_node, Transformation()));
    seen_nodes.insert(start_node);
    
    while (!node_queue.empty())
    {
        auto [node, fixed_to_node] = node_queue.front();
        node_queue.pop();

        fixed_marker_to_other_transformations[node] = fixed_to_node;

        for (auto [other, node_to_other] : transf_graph.getEdges(node))
        {
            if (!seen_nodes.contains(other))
            {
                seen_nodes.insert(other);
                Transformation fixed_to_other = fixed_to_node * node_to_other;
                node_queue.push(std::make_pair(other, fixed_to_other));
            }
        }
    }
}



std::pair<double, double> PenCalibration::calculateMRE_RMSRE(std::map<int, Transformation>& fixed_marker_to_others)
{
    std::vector<double> errors;

    for (auto& camera_markers : observed_markers_)
    {
        for (auto& marker : camera_markers)
        {
            Transformation fixed_to_camera = fixed_marker_to_others[camera_first_id_ + marker.camera_id_];
            Transformation fixed_to_marker = fixed_marker_to_others[marker.marker_id_];
            Transformation camera_to_marker = fixed_to_camera.inverse() * fixed_to_marker;
            auto [rvec, tvec] = camera_to_marker.asRvecTvec();

            std::vector<cv::Point2f> projected_points;
            cv::projectPoints(marker_points_, rvec, tvec, camera_matrix_, distortion_coefficients_, projected_points);

            for (int i = 0; i < 4; ++i)
            {
                errors.push_back(cv::norm(marker.markers_points_[i] - projected_points[i]));
            }
        }
    }

    double mre = std::accumulate(errors.begin(), errors.end(), 0.0) / errors.size();
    double rmsre = cv::sqrt(
        std::accumulate(
            errors.begin(), errors.end(), 0.0, 
            [](double sum, double elem) { return sum + elem * elem; }
        ) / errors.size()
    );

    return {mre, rmsre};
}



PenCalibrationResult PenCalibration::calibratePen()
{
    PenCalibrationResult result = { .result_ = PenCalibrationResult::Result::SUCCESS };
    TransformationGraph graph;
    buildTransformationGraph(graph);

    std::map<int, Transformation> fixed_marker_to_other_transformations;
    traverseGraph(graph, fixed_marker_to_other_transformations, fixed_marker_id_);

    for (int marker_id : used_marker_ids_)
    {
        if (!fixed_marker_to_other_transformations.contains(marker_id))
        {
            return {.result_ = PenCalibrationResult::Result::FAILED_TO_BUILD_GRAPH};
        }
    }

    for (int i = 0; i < camera_count_; ++i)
    {
        if (!fixed_marker_to_other_transformations.contains(camera_first_id_ + i))
        {
            return {.result_ = PenCalibrationResult::Result::SANITY_CHECK_FAIL};
        }
    }


    auto [init_mre, init_rmsre] = calculateMRE_RMSRE(fixed_marker_to_other_transformations);
    result.metrics_.init_mre_ = init_mre;
    result.metrics_.init_rmsre_ = init_rmsre;

     
    std::map<int, Transformation> fixed_marker_to_other_transformations_optimized;
    runOptimizer(fixed_marker_to_other_transformations, result, fixed_marker_to_other_transformations_optimized);

    if (result.result_ == PenCalibrationResult::Result::SUCCESS)
    {
        auto [final_mre, final_rmsre] = calculateMRE_RMSRE(fixed_marker_to_other_transformations_optimized);
        result.metrics_.final_mre_ = final_mre;
        result.metrics_.final_rmsre_ = final_rmsre;

        determineOppositeSides(fixed_marker_to_other_transformations_optimized, result);
    }

    return result;
}



void PenCalibration::optimizerPrepareData(std::map<int, helper::Transformation>& fixed_marker_to_other_transformations, std::vector<double>& rvec_tvec_markers, std::map<int, int>& marker_id_to_i, std::vector<double>& rvec_tvec_cameras, std::map<int, int>& camera_id_to_i)
{
    int g = 0;
    for (int marker_id : used_marker_ids_)
    {
        Transformation fixed_to_marker = fixed_marker_to_other_transformations[marker_id];
        auto [rvec, tvec] = fixed_to_marker.asRvecTvec();

        rvec_tvec_markers.insert(rvec_tvec_markers.end(), {
            rvec.at<double>(0), rvec.at<double>(1), rvec.at<double>(2), 
            tvec.at<double>(0), tvec.at<double>(1), tvec.at<double>(2)
        });

        marker_id_to_i[marker_id] = g;
        ++g;
    }

    g = 0;
    for (int camera_id = 0; camera_id < camera_count_; ++camera_id)
    {
        Transformation fixed_to_camera = fixed_marker_to_other_transformations[camera_first_id_ + camera_id];
        auto [rvec, tvec] = fixed_to_camera.asRvecTvec();

        rvec_tvec_cameras.insert(rvec_tvec_cameras.end(), {
            rvec.at<double>(0), rvec.at<double>(1), rvec.at<double>(2), 
            tvec.at<double>(0), tvec.at<double>(1), tvec.at<double>(2)
        });

        camera_id_to_i[camera_first_id_ + camera_id] = g;
        ++g;
    }
}



void PenCalibration::optimizerPrepareProblem(ceres::Problem& problem, std::vector<double>& rvec_tvec_markers, std::map<int, int>& marker_id_to_i, std::vector<double>& rvec_tvec_cameras, std::map<int, int>& camera_id_to_i)
{
    for (auto& camera_markers : observed_markers_)
    {
        for (auto& marker : camera_markers)
        {
            problem.AddResidualBlock(
                new ceres::NumericDiffCostFunction<CameraMarkerCostFunctor, ceres::CENTRAL, 8, 6, 6>(
                    new CameraMarkerCostFunctor(camera_matrix_, distortion_coefficients_, marker, marker_points_)
                ), 
                new ceres::SoftLOneLoss(1), 
                rvec_tvec_cameras.data() + 6 * camera_id_to_i[camera_first_id_ + marker.camera_id_],
                rvec_tvec_markers.data() + 6 * marker_id_to_i[marker.marker_id_]
            );
        }
    }

    // Keep fixed marker position fixed
    problem.SetParameterBlockConstant(rvec_tvec_markers.data() + 6 * marker_id_to_i[fixed_marker_id_]);
}



void PenCalibration::optimizerCollectResults(std::map<int, helper::Transformation>& fixed_marker_to_other_transformations_out, std::vector<double>& rvec_tvec_markers, std::map<int, int>& marker_id_to_i, std::vector<double>& rvec_tvec_cameras, std::map<int, int>& camera_id_to_i)
{
    fixed_marker_to_other_transformations_out.clear();

    for (int marker_id : used_marker_ids_)
    {
        double* marker_rvec_tvec = rvec_tvec_markers.data() + 6 * marker_id_to_i[marker_id];
        cv::Mat mark_rvec = (cv::Mat_<double>(3, 1) << marker_rvec_tvec[0], marker_rvec_tvec[1], marker_rvec_tvec[2]);
        cv::Mat mark_tvec = (cv::Mat_<double>(3, 1) << marker_rvec_tvec[3], marker_rvec_tvec[4], marker_rvec_tvec[5]);

        Transformation fixed_to_marker(mark_rvec, mark_tvec);
        fixed_marker_to_other_transformations_out[marker_id] = fixed_to_marker;
    }

    for (int camera_id = 0; camera_id < camera_count_; ++camera_id)
    {
        double* camera_rvec_tvec = rvec_tvec_cameras.data() + 6 * camera_id_to_i[camera_first_id_ + camera_id];
        cv::Mat cam_rvec = (cv::Mat_<double>(3, 1) << camera_rvec_tvec[0], camera_rvec_tvec[1], camera_rvec_tvec[2]);
        cv::Mat cam_tvec = (cv::Mat_<double>(3, 1) << camera_rvec_tvec[3], camera_rvec_tvec[4], camera_rvec_tvec[5]);

        Transformation fixed_to_camera(cam_rvec, cam_tvec);
        fixed_marker_to_other_transformations_out[camera_first_id_ + camera_id] = fixed_to_camera;
    }
}



void PenCalibration::runOptimizer(std::map<int, helper::Transformation>& fixed_marker_to_other_transformations, PenCalibrationResult& result, std::map<int, helper::Transformation>& fixed_marker_to_other_transformations_out)
{
    std::vector<double> rvec_tvec_markers;
    std::vector<double> rvec_tvec_cameras;

    std::map<int, int> marker_id_to_i;
    std::map<int, int> camera_id_to_i;

    optimizerPrepareData(fixed_marker_to_other_transformations, rvec_tvec_markers, marker_id_to_i, rvec_tvec_cameras, camera_id_to_i);



    ceres::Problem problem;
    optimizerPrepareProblem(problem, rvec_tvec_markers, marker_id_to_i, rvec_tvec_cameras, camera_id_to_i);



    ceres::Solver::Options options;
    options.linear_solver_type = ceres::DENSE_QR;
    options.max_num_iterations = 100;
    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);



    result.solver_stats_.solver_time_ = summary.total_time_in_seconds;
    result.solver_stats_.solver_initial_cost_ = summary.initial_cost;
    result.solver_stats_.solver_final_cost_ = summary.final_cost;

    if (summary.termination_type == ceres::TerminationType::NO_CONVERGENCE)
    {
        result.result_ = PenCalibrationResult::Result::SOLVER_NO_CONVERGENCE;
        return;
    }
    else if (summary.termination_type != ceres::TerminationType::CONVERGENCE)
    {
        result.result_ = PenCalibrationResult::Result::SOLVER_FAIL;
        return;
    }


    optimizerCollectResults(fixed_marker_to_other_transformations_out, rvec_tvec_markers, marker_id_to_i, rvec_tvec_cameras, camera_id_to_i);
}



void PenCalibration::determineOppositeSides(std::map<int, Transformation>& fixed_to_other_transformations, PenCalibrationResult& result)
{
    std::vector<PenCalibrationResult::OppositeData> opposites;
    std::set<int> used_markers;

    for (int marker_id : used_marker_ids_)
    {
        if (!used_markers.contains(marker_id))
        {
            int best_id = -1;
            double best_angle = -1;
            double distance = -1;
            for (int other_id : used_marker_ids_)
            {
                if (!used_markers.contains(other_id))
                {
                    Transformation fixed_to_marker = fixed_to_other_transformations[marker_id];
                    Transformation fixed_to_other = fixed_to_other_transformations[other_id];
                    Transformation marker_to_other = fixed_to_marker.inverse() * fixed_to_other;
                    double angle = cv::abs(marker_to_other.angleDeg());
                    if (angle > best_angle)
                    {
                        best_angle = angle;
                        best_id = other_id;
                        distance = cv::norm(marker_to_other.translation);
                    }
                }
            }

            if (best_id == -1)
            {
                result.result_ = PenCalibrationResult::Result::OPPOSITE_FAIL;
                return;
            }

            opposites.push_back({
                .first_id_ = marker_id, 
                .second_id_ = best_id,
                .angle_deg_ = best_angle,
                .distance_m_ = distance
            });
            used_markers.insert({marker_id, best_id});
        }
    }

    result.opposite_markers_ = std::move(opposites);
}