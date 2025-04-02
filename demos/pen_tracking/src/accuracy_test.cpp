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



void show_frame(cv::Mat& frame, int frame_id, int frame_count, double target_fps, aergo::pen_tracking::MarkerTracker& marker_tracker, cv::Mat& camera_matrix, cv::Mat& distortion_coefficients, Transformation& tip_to_origin)
{
    int64_t start_time = cv::getTickCount();
    cv::Mat visualization;
    auto result = marker_tracker.processImage(frame, &visualization);
    double runtime_ms = (cv::getTickCount() - start_time) / cv::getTickFrequency() * 1000;
    double perf_perc = target_fps / (1000 / runtime_ms);

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << runtime_ms << "ms";

    std::ostringstream stream_perc;
    stream_perc << std::fixed << std::setprecision(1) << perf_perc * 100 << "%";

    std::ostringstream stream_3;
    stream_3 << frame_id << "/" << frame_count;

    cv::Scalar perc_color;
    if (perf_perc < 0.7)
    {
        perc_color = cv::Scalar(100, 255, 100);
    }
    else if (perf_perc < 1)
    {
        perc_color = cv::Scalar(0, 255, 255);
    }
    else
    {
        perc_color = cv::Scalar(0, 0, 255);
    }

    

    cv::Mat output = (visualization.empty()) ? frame : visualization;

    if (result.success)
    {
        Transformation camera_to_tip = result.camera_to_origin * tip_to_origin.inverse();
        auto [rvec, tvec] = camera_to_tip.asRvecTvec();
        cv::drawFrameAxes(output, camera_matrix, distortion_coefficients, rvec, tvec, 0.01f);
    }

    cv::putText(output, stream.str(), cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(100, 255, 100), 3);
    cv::putText(output, stream_perc.str(), cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 1, perc_color, 3);
    cv::putText(frame, stream_3.str(), cv::Point(10, 90), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(100, 255, 100), 3);
    cv::imshow("Pen tracking demo", output);

    
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

    double search_window_perc = 0.05;
    aergo::pen_tracking::MarkerTracker marker_tracker(
        camera_matrix, distortion_coefficients, defaults::pen::getArucoDetector(), 
        defaults::pen::USED_MARKER_IDS, defaults::pen::getMarkerPoints3d(), 
        origin_to_other_transformations, search_window_perc
    );

    std::string video_path = argv[1];
    cv::VideoCapture cap(video_path);
    if (!cap.isOpened())
    {
        return -1;
    }
    double target_fps = cap.get(cv::CAP_PROP_FPS);

    std::cout << "Loading video..." << std::flush;
    std::vector<cv::Mat> video_full;
    while (true)
    {
        cv::Mat frame;
        cap.read(frame);
        if (frame.empty())
        {
            break;
        }
        video_full.push_back(frame);
    }
    cap.release();
    int frame_count = (int)video_full.size();
    std::cout << "LOADED " << frame_count << " frames" << std::endl;


    int current_frame = 0;
    bool running_forward = false;
    bool last_forward_key = false;
    show_frame(video_full[0], current_frame, frame_count, target_fps, marker_tracker, camera_matrix, distortion_coefficients, tip_to_origin);
    while (true)
    {
        if (running_forward)
        {
            ++current_frame;
            if (current_frame == frame_count)
            {
                --current_frame;
                running_forward = false;
            }
            show_frame(video_full[current_frame], current_frame, frame_count, target_fps, marker_tracker, camera_matrix, distortion_coefficients, tip_to_origin);
            
        }

        int pressed_key = cv::waitKey(1);
        if (pressed_key == ' ')
        {
            if (!last_forward_key)
            {
                running_forward = !running_forward;
            }
            last_forward_key = true;
        }
        else
        {
            last_forward_key = false;
        }

        if (pressed_key == 'l')
        {
            ++current_frame;
            if (current_frame >= frame_count)
            {
                current_frame = frame_count - 1;
            }
            show_frame(video_full[current_frame], current_frame, frame_count, target_fps, marker_tracker, camera_matrix, distortion_coefficients, tip_to_origin);
        }
        if (pressed_key == 'j')
        {
            --current_frame;
            if (current_frame < 0)
            {
                current_frame = 0;
            }
            show_frame(video_full[current_frame], current_frame, frame_count, target_fps, marker_tracker, camera_matrix, distortion_coefficients, tip_to_origin);
        }
        if (pressed_key == 'o')
        {
            current_frame += 10;
            if (current_frame >= frame_count)
            {
                current_frame = frame_count - 1;
            }
            show_frame(video_full[current_frame], current_frame, frame_count, target_fps, marker_tracker, camera_matrix, distortion_coefficients, tip_to_origin);
        }
        if (pressed_key == 'u')
        {
            current_frame -= 10;
            if (current_frame < 0)
            {
                current_frame = 0;
            }
            show_frame(video_full[current_frame], current_frame, frame_count, target_fps, marker_tracker, camera_matrix, distortion_coefficients, tip_to_origin);
        }
        if (pressed_key == 'q')
        {
            break;
        }
    }

    cv::destroyAllWindows();
    
    return 0;
}