#include "pen_calibration.h"
#include "pen_calibration_helper.h"

using namespace aergo::pen_calibration;
using namespace helper;

PenCalibration::PenCalibration(
    cv::Mat camera_matrix, cv::Mat distortion_coefficients, 
    cv::aruco::ArucoDetector aruco_detector, const std::set<int>& used_marker_ids,
    std::vector<cv::Point3f>& marker_points, double ignore_markers_above_angle, int fixed_marker_id
) 
: camera_matrix_(std::move(camera_matrix)), 
distortion_coefficients_(std::move(distortion_coefficients)), 
aruco_detector_(std::move(aruco_detector)),
used_marker_ids_(used_marker_ids),
ignore_markers_above_angle_(ignore_markers_above_angle),
fixed_marker_id_(fixed_marker_id),
camera_count_(0),
marker_points_(marker_points)
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

        determineMarkerPositions(fixed_marker_to_other_transformations_optimized, result);
    }

    if (result.result_ == PenCalibrationResult::Result::SUCCESS)
    {
        determinePenOrigin(fixed_marker_to_other_transformations_optimized, result);
    }

    return result;
}



cv::Point3d PenCalibration::determineMidpoint(std::map<int, Transformation>& fixed_to_other_transformations, int front_id, int back_id, int left_id, int right_id)
{
    Transformation fixed_to_front = fixed_to_other_transformations[front_id];
    Transformation fixed_to_back = fixed_to_other_transformations[back_id];
    Transformation fixed_to_left = fixed_to_other_transformations[left_id];
    Transformation fixed_to_right = fixed_to_other_transformations[right_id];

    cv::Point3d p1 = (cv_extensions::asPoint(fixed_to_front.translation) + cv_extensions::asPoint(fixed_to_back.translation)) / 2;
    cv::Point3d n1 = fixed_to_front.normal() - fixed_to_back.normal();
    n1 /= cv::norm(n1);

    cv::Point3d p2 = (cv_extensions::asPoint(fixed_to_left.translation) + cv_extensions::asPoint(fixed_to_right.translation)) / 2;
    cv::Point3d n2 = fixed_to_left.normal() - fixed_to_right.normal();
    n2 /= cv::norm(n2);

    double h1 = n1.dot(p1);   // plane 1:   n1 * p = n1 * p1 = h1
    double h2 = n2.dot(p2);   // plane 2:   n2 * p = n2 * p2 = h2
    double n1_dot_n2 = n1.dot(n2);

    double c1 = (h1 - h2 * n1_dot_n2) / (1 - n1_dot_n2 * n1_dot_n2);  // https://en.wikipedia.org/wiki/Plane%E2%80%93plane_intersection
    double c2 = (h2 - h1 * n1_dot_n2) / (1 - n1_dot_n2 * n1_dot_n2);
    cv::Point3d p_line = c1 * n1 + c2 * n2;
    cv::Point3d n_line = n1.cross(n2);
    n_line /= cv::norm(n_line);

    double multiplier = n_line.dot(cv_extensions::asPoint(fixed_to_front.translation) - p_line);
    cv::Point3d midpoint = p_line + multiplier * n_line;

    return midpoint;
}



void PenCalibration::determinePenOrigin(std::map<int, Transformation>& fixed_to_other_transformations, PenCalibrationResult& result)
{
    cv::Point3d normal_x1 = fixed_to_other_transformations[result.marker_position_data_.marker_id_0_].normal();
    cv::Point3d normal_x2 = -fixed_to_other_transformations[result.marker_position_data_.marker_id_180_].normal();
    cv::Point3d normal_x = normal_x1 + normal_x2;

    cv::Point3d normal_y1 = fixed_to_other_transformations[result.marker_position_data_.marker_id_90_].normal();
    cv::Point3d normal_y2 = -fixed_to_other_transformations[result.marker_position_data_.marker_id_270_].normal();
    cv::Point3d normal_y = normal_y1 + normal_y2;

    cv::Point3d normal_z = normal_x.cross(normal_y);
    normal_y = normal_z.cross(normal_x);

    normal_x /= cv::norm(normal_x);
    normal_y /= cv::norm(normal_y);
    normal_z /= cv::norm(normal_z);



    cv::Point3d midpoint_bottom = determineMidpoint(
        fixed_to_other_transformations, 
        result.marker_position_data_.marker_id_0_, 
        result.marker_position_data_.marker_id_180_, 
        result.marker_position_data_.marker_id_90_, 
        result.marker_position_data_.marker_id_270_
    );

    cv::Point3d midpoint_top = determineMidpoint(
        fixed_to_other_transformations, 
        result.marker_position_data_.marker_id_45_, 
        result.marker_position_data_.marker_id_225_, 
        result.marker_position_data_.marker_id_135_, 
        result.marker_position_data_.marker_id_315_
    );

    cv::Point3d midpoint_bottom_proj = midpoint_bottom - normal_z.dot(midpoint_bottom) * normal_z;
    cv::Point3d midpoint_top_proj = midpoint_top - normal_z.dot(midpoint_top) * normal_z;
    cv::Point3d origin = (midpoint_bottom_proj + midpoint_top_proj) / 2;

    Transformation fixed_to_origin;
    fixed_to_origin.rotation.at<double>(0, 0) = normal_x.x;
    fixed_to_origin.rotation.at<double>(1, 0) = normal_x.y;
    fixed_to_origin.rotation.at<double>(2, 0) = normal_x.z;

    fixed_to_origin.rotation.at<double>(0, 1) = normal_y.x;
    fixed_to_origin.rotation.at<double>(1, 1) = normal_y.y;
    fixed_to_origin.rotation.at<double>(2, 1) = normal_y.z;

    fixed_to_origin.rotation.at<double>(0, 2) = normal_z.x;
    fixed_to_origin.rotation.at<double>(1, 2) = normal_z.y;
    fixed_to_origin.rotation.at<double>(2, 2) = normal_z.z;

    fixed_to_origin.translation.at<double>(0) = origin.x;
    fixed_to_origin.translation.at<double>(1) = origin.y;
    fixed_to_origin.translation.at<double>(2) = origin.z;

    Transformation origin_to_fixed = fixed_to_origin.inverse();

    for (int marker_id : used_marker_ids_)
    {
        result.origin_to_other_transformations[marker_id] = origin_to_fixed * fixed_to_other_transformations[marker_id];
    }
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



double PenCalibration::calculateVectorVectorAngleDeg(cv::Point3d vector_1, cv::Point3d vector_2)
{
    double cos_theta = vector_1.dot(vector_2) / (cv::norm(vector_1) * cv::norm(vector_2));
    double theta = std::acos(cos_theta);
    double theta_deg = theta * 180.0 / CV_PI;

    return theta_deg;
}



std::optional<cv::Point3d> PenCalibration::estimateZAxis(std::map<int, Transformation>& fixed_to_other_transformations)
{
    cv::Point3d fixed_normal = fixed_to_other_transformations[fixed_marker_id_].normal();

    std::vector<cv::Point3d> top_cube;
    std::vector<cv::Point3d> bottom_cube;

    for (int marker_id : used_marker_ids_)
    {
        Transformation marker_transformation = fixed_to_other_transformations[marker_id];
        double theta_deg = calculateVectorVectorAngleDeg(fixed_normal, marker_transformation.normal());
        double differentiator = std::min(theta_deg, std::min(std::abs(90 - theta_deg), std::abs(180 - theta_deg)));

        if (differentiator < 22.5)
        {
            bottom_cube.push_back(cv_extensions::asPoint(marker_transformation.translation));
        }
        else
        {
            top_cube.push_back(cv_extensions::asPoint(marker_transformation.translation));
        }
    }

    if (top_cube.size() != 4 || bottom_cube.size() != 4)
    {
        return std::nullopt;
    }

    cv::Point3d top_middle = std::accumulate(top_cube.begin(), top_cube.end(), cv::Point3d(0, 0, 0)) / 4;
    cv::Point3d bottom_middle = std::accumulate(bottom_cube.begin(), bottom_cube.end(), cv::Point3d(0, 0, 0)) / 4;

    return bottom_middle - top_middle;
}



int PenCalibration::findClosestMarker(std::map<int, Transformation>& fixed_to_other_transformations, double angle_deg, cv::Point3d z_estimate)
{
    cv::Point3d fixed_normal = fixed_to_other_transformations[fixed_marker_id_].normal();

    double min_difference_deg = 360;
    int found_id = -1;

    for (int marker_id : used_marker_ids_)
    {
        cv::Point3d marker_normal = fixed_to_other_transformations[marker_id].normal();
        double theta_deg = calculateVectorVectorAngleDeg(fixed_normal, marker_normal);

        if (calculateVectorVectorAngleDeg(z_estimate, fixed_normal.cross(marker_normal)) > 90)
        {
            theta_deg = 360-theta_deg;
        }
        
        double angle_diff = cv::abs(theta_deg - angle_deg);
        if (angle_diff < min_difference_deg)
        {
            min_difference_deg = angle_diff;
            found_id = marker_id;
        }
    }

    return found_id;
}



void PenCalibration::determineMarkerPositions(std::map<int, Transformation>& fixed_to_other_transformations, PenCalibrationResult& result)
{
    std::optional<cv::Point3d> z_estimate = estimateZAxis(fixed_to_other_transformations);
    if (!z_estimate)
    {
        result.result_ = PenCalibrationResult::Result::MARKER_POSITION_FAIL;
        return;
    }

    result.marker_position_data_.marker_id_0_ = findClosestMarker(fixed_to_other_transformations, 0, *z_estimate);
    result.marker_position_data_.marker_id_90_ = findClosestMarker(fixed_to_other_transformations, 90, *z_estimate);
    result.marker_position_data_.marker_id_180_ = findClosestMarker(fixed_to_other_transformations, 180, *z_estimate);
    result.marker_position_data_.marker_id_270_ = findClosestMarker(fixed_to_other_transformations, 270, *z_estimate);

    result.marker_position_data_.marker_id_45_ = findClosestMarker(fixed_to_other_transformations, 45, *z_estimate);
    result.marker_position_data_.marker_id_135_ = findClosestMarker(fixed_to_other_transformations, 135, *z_estimate);
    result.marker_position_data_.marker_id_225_ = findClosestMarker(fixed_to_other_transformations, 225, *z_estimate);
    result.marker_position_data_.marker_id_315_ = findClosestMarker(fixed_to_other_transformations, 315, *z_estimate);

    std::set<int> found_marker_ids({
        result.marker_position_data_.marker_id_0_, 
        result.marker_position_data_.marker_id_90_, 
        result.marker_position_data_.marker_id_180_, 
        result.marker_position_data_.marker_id_270_, 
        result.marker_position_data_.marker_id_45_, 
        result.marker_position_data_.marker_id_135_, 
        result.marker_position_data_.marker_id_225_, 
        result.marker_position_data_.marker_id_315_, 
    });

    // check that we have used every id and only once
    if (found_marker_ids.size() != 8)
    {
        result.result_ = PenCalibrationResult::Result::MARKER_POSITION_FAIL;
        return;
    }
}