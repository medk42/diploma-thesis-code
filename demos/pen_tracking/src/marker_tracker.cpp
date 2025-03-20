#include "marker_tracker.h"

using namespace aergo::pen_tracking;


MarkerTracker::MarkerTracker(
    cv::Mat camera_matrix, cv::Mat distortion_coefficients, 
    cv::aruco::ArucoDetector aruco_detector, const std::set<int> used_marker_ids,
    std::vector<cv::Point3f> marker_points,
    std::map<int, Transformation> origin_to_other_transformations, double search_window_perc
) 
: camera_matrix_(std::move(camera_matrix)), 
distortion_coefficients_(std::move(distortion_coefficients)), 
aruco_detector_(std::move(aruco_detector)),
used_marker_ids_(used_marker_ids),
marker_points_(marker_points),
origin_to_other_transformations_(std::move(origin_to_other_transformations)),
search_window_perc_(search_window_perc)
{}



MarkerTracker::Result MarkerTracker::processImage(cv::Mat image, cv::Mat* return_visualization)
{
    auto time_start = cv::getTickCount();

    MarkerTracker::Result result { .success = false, .lost_tracking = true };

    std::optional<Transformation> last_camera_to_origin_copy = last_camera_to_origin_;
    last_camera_to_origin_ = std::nullopt;

    if (image.empty())
    {
        return result;
    }

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    if (return_visualization)
    {
        *return_visualization = image.clone();
    }



    std::vector<std::vector<cv::Point2f>> corners;
    std::vector<int> ids;
    if (last_camera_to_origin_copy)
    {
        std::optional<cv::Rect> roi = getRoi(*last_camera_to_origin_copy, cv::Size2i(gray.cols, gray.rows), search_window_perc_, return_visualization);
        if (roi)
        {
            cv::Mat gray_roi = gray(*roi);
            aruco_detector_.detectMarkers(gray_roi, corners, ids);

            for (int i = 0; i < ids.size(); ++i)
            {
                if (used_marker_ids_.contains(ids[i]))
                {
                    result.lost_tracking = false;
                }
                cv::Point2f roi_start((float)roi->x, (float)roi->y);
                corners[i][0] += roi_start;
                corners[i][1] += roi_start;
                corners[i][2] += roi_start;
                corners[i][3] += roi_start;
            }
        }
    }

    if (result.lost_tracking)
    {
        corners.clear();
        ids.clear();
        aruco_detector_.detectMarkers(gray, corners, ids);
    }


    
    auto time_detect = cv::getTickCount();
                
    if (return_visualization)
    {
        cv::aruco::drawDetectedMarkers(*return_visualization, corners, ids);
    }
    auto time_vis_1 = cv::getTickCount();

    std::vector<cv::Point3d> world_points;
    std::vector<cv::Point2d> image_points;
    for (int i = 0; i < ids.size(); ++i)
    {
        if (used_marker_ids_.contains(ids[i]))
        {
            if (return_visualization)
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
                result.camera_to_visible_marker[ids[i]] = Transformation(rvec, tvec);
                
                cv::drawFrameAxes(*return_visualization, camera_matrix_, distortion_coefficients_, rvec, tvec, 0.01f);
            }

            Transformation origin_to_marker = origin_to_other_transformations_[ids[i]];
            
            world_points.push_back(origin_to_marker * marker_points_[0]);
            world_points.push_back(origin_to_marker * marker_points_[1]);
            world_points.push_back(origin_to_marker * marker_points_[2]);
            world_points.push_back(origin_to_marker * marker_points_[3]);
            
            image_points.push_back(corners[i][0]);
            image_points.push_back(corners[i][1]);
            image_points.push_back(corners[i][2]);
            image_points.push_back(corners[i][3]);
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
    last_camera_to_origin_ = result.camera_to_origin;
    
    return result;
}



std::optional<cv::Rect> MarkerTracker::getRoi(Transformation last_position, cv::Size2i image_dimensions, double search_window_perc, cv::Mat* return_visualization)
{
    auto [rvec, tvec] = last_position.asRvecTvec();

    std::vector<cv::Point3f> tracker_world_points;
    for (int marker_id : used_marker_ids_)
    {
        auto origin_to_marker = origin_to_other_transformations_[marker_id];

        tracker_world_points.push_back(origin_to_marker * cv::Point3f(0, 0, 0));
    }

    std::vector<cv::Point2f> tracker_image_points;
    cv::projectPoints(tracker_world_points, rvec, tvec, camera_matrix_, distortion_coefficients_, tracker_image_points);



    cv::Point2i window_min(image_dimensions.width + 1, image_dimensions.height + 1), window_max(-1, -1);
    for (auto& point : tracker_image_points)
    {
        window_min.x = std::min<int>(window_min.x, (int)point.x);
        window_min.y = std::min<int>(window_min.y, (int)point.y);
        window_max.x = std::max<int>(window_max.x, (int)point.x);
        window_max.y = std::max<int>(window_max.y, (int)point.y);
    }

    window_min.x = std::max(std::min(window_min.x, image_dimensions.width - 2), 0);
    window_max.x = std::max(std::min(window_max.x, image_dimensions.width - 2), 0);
    window_min.y = std::max(std::min(window_min.y, image_dimensions.height - 2), 0);
    window_max.y = std::max(std::min(window_max.y, image_dimensions.height - 2), 0);

    cv::Rect smallest_window(window_min.x, window_min.y, window_max.x - window_min.x + 1, window_max.y - window_min.y + 1);



    double a = 1;
    double b = smallest_window.width + smallest_window.height;
    double c = smallest_window.area() - search_window_perc * image_dimensions.area();

    double ins = b * b - 4 * a * c;
    if (ins < 0)
    {
        return std::nullopt;
    }
    double x = (-b + std::sqrt(ins)) / (2 * a);



    cv::Point2i new_window_min((int)(window_min.x - x/2), (int)(window_min.y - x/2));
    cv::Point2i new_window_max((int)(window_max.x + x/2), (int)(window_max.y + x/2));
    
    new_window_min.x = std::max(std::min(new_window_min.x, image_dimensions.width - 2), 0);
    new_window_max.x = std::max(std::min(new_window_max.x, image_dimensions.width - 2), 0);
    new_window_min.y = std::max(std::min(new_window_min.y, image_dimensions.height - 2), 0);
    new_window_max.y = std::max(std::min(new_window_max.y, image_dimensions.height - 2), 0);

    cv::Rect search_window(new_window_min.x, new_window_min.y, new_window_max.x - new_window_min.x + 1, new_window_max.y - new_window_min.y + 1);



    if (return_visualization)
    {
        cv::rectangle(*return_visualization, smallest_window, cv::Scalar(0, 200, 0, 200));
        cv::rectangle(*return_visualization, search_window, cv::Scalar(0, 0, 200, 200));
    }

    return search_window;
}