#include <opencv2/opencv.hpp>


#include "logging.h"
#include "defaults.h"
#include "pen_calibration_helper.h"
#include "marker_tracker.h"


using namespace aergo;
using namespace aergo::pen_calibration::helper;



bool loadCameraCalibration(const std::string& filename, cv::Mat& camera_matrix, cv::Mat& distortion_coefficients)
{
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    
    if (!fs.isOpened())
    {
        LOG_ERROR("Failed to open file for reading. Check if the file exists and has correct permissions.");
        return false; 
    }

    try
    {
        fs["CAMERA_MATRIX"] >> camera_matrix;
        fs["DISTORTION_COEFFICIENTS"] >> distortion_coefficients;

        if (camera_matrix.empty() || distortion_coefficients.empty())
        {
            LOG_ERROR("Camera Matrix or Distortion Coefficients not found in file.");
            return false;
        }

    } 
    catch (const cv::Exception& e)
    {
        LOG_ERROR("Camera Matrix or Distortion Coefficients failed to load, error: " << e.what());
        return false;
    }

    fs.release();

    return true;
}



bool loadPenCalibration(std::string filename, std::map<int, Transformation>& origin_to_other_transformations)
{
    origin_to_other_transformations.clear();

    cv::FileStorage fs(filename, cv::FileStorage::READ);
    
    if (!fs.isOpened())
    {
        LOG_ERROR("Failed to open file for reading. Check if the file exists and has correct permissions.");
        return false; 
    }

    try
    {
        cv::FileNode transformations_node = fs["origin_to_other"];
        for (cv::FileNodeIterator it = transformations_node.begin(); it != transformations_node.end(); ++it)
        {
            cv::FileNode it_node = (*it);
            int key = std::stoi(it_node.name().substr(4));
            Transformation t;
            it_node["translation"] >> t.translation;
            it_node["rotation"] >> t.rotation;
            origin_to_other_transformations[key] = t;

            if (t.translation.empty() || t.rotation.empty())
            {
                LOG_ERROR("Transformation data not found in file.");
                return false;
            }
        }

    } 
    catch (const cv::Exception& e)
    {
        LOG_ERROR("Camera Matrix or Distortion Coefficients failed to load, error: " << e.what());
        return false;
    }

    fs.release();

    return true;
}



int main(int argc, char** argv)
{
    if (argc != 4)
    {
        LOG_ERROR("Requires three argument")
        return -1;
    }


    std::string camera_filename = argv[2];
    cv::Mat camera_matrix, distortion_coefficients;
    if (loadCameraCalibration(camera_filename, camera_matrix, distortion_coefficients))
    {
        AERGO_LOG("Loaded Camera Matrix:\n" << camera_matrix << "\n\nLoaded Distortion Coefficients:\n" << distortion_coefficients << "\n")
    } 
    else 
    {
        return -1;
    }

    std::string pen_calib_filename = argv[3];
    std::map<int, Transformation> origin_to_other_transformations;
    if (loadPenCalibration(pen_calib_filename, origin_to_other_transformations))
    {
        AERGO_LOG("Loaded transformations:")
        for (const auto& [key, origin_to_key] : origin_to_other_transformations)
        {
            AERGO_LOG("\t" << key << ": " << cv_extensions::asPoint(origin_to_key.translation) * 1000 << "mm")
        }
    } 
    else 
    {
        return -1;
    }

    Transformation tip_to_origin;
    tip_to_origin.translation.at<double>(2) = -defaults::pen::ORIGIN_TO_TIP_DISTANCE;



    std::vector<std::string> image_paths;
    cv::glob(argv[1], image_paths);

    // int max_image = 5;
    // if (image_paths.size() > max_image)
    // {
    //     image_paths.erase(image_paths.begin() + max_image, image_paths.end());
    // }

    double search_window_perc = 0.05;
    aergo::pen_tracking::MarkerTracker marker_tracker(
        camera_matrix, distortion_coefficients, defaults::pen::getArucoDetector(), 
        defaults::pen::USED_MARKER_IDS, defaults::pen::getMarkerPoints3d(), 
        origin_to_other_transformations, search_window_perc
    );
    
    // for (auto&& path : image_paths)
    // {
    //     cv::Mat image = cv::imread(path);  

    //     cv::Mat visualization;
    //     marker_tracker.processImage(image, &visualization);
        

    //     cv::imshow("image", visualization);
    //     cv::waitKey(0);
    //     cv::destroyAllWindows();                 

        
    // }

    cv::VideoCapture cap(0);
    if (!cap.isOpened())
    {
        return -1;
    }

    int64_t curr_time = 0, prev_time = 0;
    while (true)
    {
        cv::Mat frame;
        cap.read(frame);
        if (frame.empty())
        {
            return -1;
        }

        curr_time = cv::getTickCount();
        double frame_time_ms = (curr_time - prev_time) / cv::getTickFrequency() * 1000;
        prev_time = curr_time;

        std::ostringstream stream;
        stream << std::fixed << std::setprecision(1) << frame_time_ms << "ms";

        cv::Mat visualization;
        auto result = marker_tracker.processImage(frame, &visualization);

        cv::Mat output = (visualization.empty()) ? frame : visualization;

        if (result.success)
        {
            Transformation camera_to_tip = result.camera_to_origin * tip_to_origin.inverse();
            auto [rvec, tvec] = camera_to_tip.asRvecTvec();
            cv::drawFrameAxes(visualization, camera_matrix, distortion_coefficients, rvec, tvec, 0.01f);

            cv::putText(output, stream.str(), cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(100, 255, 100), 3);
        }
        else
        {
            cv::putText(output, stream.str(), cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 3);
            
        }


        if (result.success)
        {
            std::vector<cv::Point2f> image_point;
            std::vector<cv::Point3f> origin_point = { cv::Point3f(0, 0, 0) };
            auto [rv,tv] = result.camera_to_origin.asRvecTvec();
            cv::projectPoints(origin_point, rv, tv, camera_matrix, distortion_coefficients, image_point);
            cv::Point2i center((int)image_point[0].x, (int)image_point[0].y);

            int req_size = 100;
            int goal_size = 1000;
            cv::Rect cut_rect(
                center.x - req_size/2,
                center.y - req_size/2,
                req_size, 
                req_size
            );
            cut_rect.x = std::min(std::max(cut_rect.x, 0), output.cols - 10);
            cut_rect.y = std::min(std::max(cut_rect.y, 0), output.rows - 10);
            cut_rect.width = std::max(std::min(cut_rect.width, output.cols - cut_rect.x - 1), 1);
            cut_rect.height = std::max(std::min(cut_rect.height, output.rows - cut_rect.y - 1), 1);

            cv::Mat output_cut = output(cut_rect);
            cv::Mat output_cut_resize;
            cv::resize(output_cut, output_cut_resize, cv::Size(goal_size, goal_size));
            cv::imshow("Zoomed demo", output_cut_resize);
        }



        cv::imshow("Pen tracking demo", output);


        
        
        if (cv::waitKey(1) == 'q')
        {
            break;
        }
    }

    cap.release();
    cv::destroyAllWindows();
    
    return 0;
}