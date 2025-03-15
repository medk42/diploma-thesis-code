#include "marker_tracker.h"

using namespace aergo::pen_tracking;


MarkerTracker::MarkerTracker(
    cv::Mat camera_matrix, cv::Mat distortion_coefficients, 
    cv::aruco::ArucoDetector aruco_detector, const std::set<int> used_marker_ids,
    std::vector<cv::Point3f> marker_points, double ignore_markers_above_angle,
    std::map<int, Transformation> origin_to_other_transformations
) 
: camera_matrix_(std::move(camera_matrix)), 
distortion_coefficients_(std::move(distortion_coefficients)), 
aruco_detector_(std::move(aruco_detector)),
used_marker_ids_(used_marker_ids),
marker_points_(marker_points),
ignore_markers_above_angle_(ignore_markers_above_angle),
origin_to_other_transformations_(std::move(origin_to_other_transformations))
{}



MarkerTracker::Result MarkerTracker::processImage(cv::Mat image, cv::Mat* return_visualization)
{
    auto time_start = cv::getTickCount();

    MarkerTracker::Result result { .success = false };

    if (image.empty())
    {
        return result;
    }

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    std::vector<std::vector<cv::Point2f>> corners;
    std::vector<int> ids;
    aruco_detector_.detectMarkers(gray, corners, ids);
    
    auto time_detect = cv::getTickCount();
                
    if (return_visualization)
    {
        *return_visualization = image.clone();
        cv::aruco::drawDetectedMarkers(*return_visualization, corners, ids);
    }
    auto time_vis_1 = cv::getTickCount();

    std::vector<cv::Point3d> world_points;
    std::vector<cv::Point2d> image_points;
    for (int i = 0; i < ids.size(); ++i)
    {
        if (used_marker_ids_.contains(ids[i]))
        {
            cv::Mat rvec, tvec;
            bool success = cv::solvePnP(
                marker_points_, corners[i], 
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
                Transformation origin_to_marker = origin_to_other_transformations_[ids[i]];
                result.camera_to_visible_marker[ids[i]] = Transformation(rvec, tvec);
                
                world_points.push_back(origin_to_marker * marker_points_[0]);
                world_points.push_back(origin_to_marker * marker_points_[1]);
                world_points.push_back(origin_to_marker * marker_points_[2]);
                world_points.push_back(origin_to_marker * marker_points_[3]);
                
                image_points.push_back(corners[i][0]);
                image_points.push_back(corners[i][1]);
                image_points.push_back(corners[i][2]);
                image_points.push_back(corners[i][3]);

                if (return_visualization)
                {
                    cv::drawFrameAxes(*return_visualization, camera_matrix_, distortion_coefficients_, rvec, tvec, 0.01f);
                }
            }
        }
    }
    auto time_solve_all = cv::getTickCount();

    if (world_points.empty())
    {
        return result;
    }

    cv::Mat rvec, tvec;
    bool success = cv::solvePnP(
        world_points, image_points,
        camera_matrix_, distortion_coefficients_, 
        rvec, tvec, false, cv::SOLVEPNP_ITERATIVE
    );
    auto time_single_solve = cv::getTickCount();
    if (!success)
    {
        return result;
    }

    if (return_visualization)
    {
        cv::drawFrameAxes(*return_visualization, camera_matrix_, distortion_coefficients_, rvec, tvec, 0.02f);
    }
    auto time_end = cv::getTickCount();

    std::cout << (time_detect - time_start) / cv::getTickFrequency() * 1000 << " / "
     << (time_vis_1 - time_detect) / cv::getTickFrequency() * 1000 << " / "
      << (time_solve_all - time_vis_1) / cv::getTickFrequency() * 1000 << " / "
       << (time_single_solve - time_solve_all) / cv::getTickFrequency() * 1000 << " / "
        << (time_end - time_single_solve) / cv::getTickFrequency() * 1000 << std::endl;

    result.camera_to_origin = Transformation(rvec, tvec);
    result.success = true;
    
    return result;
}