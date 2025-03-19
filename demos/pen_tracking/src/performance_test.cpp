#include <opencv2/opencv.hpp>


#include "logging.h"
#include "defaults.h"
#include "pen_calibration_helper.h"
#include "marker_tracker.h"


#include <algorithm>
#include <numeric>
#include <cmath>


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


struct test_results
{
    std::string video_path;
    double search_window_perc;
    bool run_visualization;

    double time_min_ms;
    double time_max_ms;
    double time_percentile_01_ms;
    double time_percentile_1_ms;
    double time_percentile_10_ms;
    double time_percentile_25_ms;
    double time_percentile_50_ms;
    double time_avg_ms;
    double time_std_ms;
    int failure_count;
    double failure_perc;
    int lost_tracking_count;
    double lost_tracking_perc;
    int frame_count;
};


void print_log(test_results results)
{
    std::ostringstream stream_results;
    stream_results << std::fixed << std::setprecision(2);
    stream_results << "Test results:\n";
    stream_results << "\tVideo Path: " << results.video_path << "\n";
    stream_results << "\tSearch Window Percentage: " << results.search_window_perc << "\n";
    stream_results << "\tWith visualization: " << (results.run_visualization ? "true" : "false") << "\n";
    stream_results << "\tTime statistics:\n";
    stream_results << "\t\tMin: " << results.time_min_ms << "ms\n";
    stream_results << "\t\tMax: " << results.time_max_ms << "ms\n";
    stream_results << "\t\t0.1% percentile: " << results.time_percentile_01_ms << "ms\n";
    stream_results << "\t\t1% percentile: " << results.time_percentile_1_ms << "ms\n";
    stream_results << "\t\t10% percentile: " << results.time_percentile_10_ms << "ms\n";
    stream_results << "\t\t25% percentile: " << results.time_percentile_25_ms << "ms\n";
    stream_results << "\t\t50% percentile: " << results.time_percentile_50_ms << "ms\n";
    stream_results << "\t\tAverage: " << results.time_avg_ms << "ms\n";
    stream_results << "\t\tStandard Deviation: " << results.time_std_ms << "ms\n";
    stream_results << "\tSuccess rate:\n";
    stream_results << "\t\tFailure count: " << results.failure_count << " (" << results.failure_perc * 100 << "%)\n";
    stream_results << "\t\tLost tracking count: " << results.lost_tracking_count << " (" << results.lost_tracking_perc * 100 << "%)\n";
    stream_results << "\t\tTotal frames: " << results.frame_count << "\n";
    AERGO_LOG(stream_results.str());
}


std::optional<test_results> perform_test(cv::Mat camera_matrix, cv::Mat distortion_coefficients, std::map<int, Transformation>& origin_to_other_transformations, Transformation& tip_to_origin, std::string video_path, double search_window_perc, bool run_visualization, bool video_output)
{
    aergo::pen_tracking::MarkerTracker marker_tracker(
        camera_matrix, distortion_coefficients, defaults::pen::getArucoDetector(), 
        defaults::pen::USED_MARKER_IDS, defaults::pen::getMarkerPoints3d(), 
        defaults::pen::IGNORE_MARKERS_ABOVE_ANGLE_DEG, origin_to_other_transformations, search_window_perc
    );

    cv::VideoCapture cap(video_path);
    if (!cap.isOpened())
    {
        return std::nullopt;
    }
    double target_fps = cap.get(cv::CAP_PROP_FPS);
    

    std::vector<double> runtimes;
    int frame_count = 0;
    int lost_tracking_count = 0;
    int failure_count = 0;

    while (true)
    {
        cv::Mat frame;
        cap.read(frame);
        if (frame.empty())
        {
            break;
        }



        int64_t start_time = cv::getTickCount();

        aergo::pen_tracking::MarkerTracker::Result result;

        cv::Mat visualization;
        if (run_visualization)
        {
            result = marker_tracker.processImage(frame, &visualization);
        }
        else
        {
            result = marker_tracker.processImage(frame, nullptr);
        }
        

        double runtime_ms = (cv::getTickCount() - start_time) / cv::getTickFrequency() * 1000;
        double perf_perc = target_fps / (1000 / runtime_ms);
        runtimes.push_back(runtime_ms);

        if (!result.success)
        {
            ++failure_count;
        }
        if (result.lost_tracking)
        {
            ++lost_tracking_count;
        }
        ++frame_count;



        if (video_output)
        {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(1) << runtime_ms << "ms";

            std::ostringstream stream_perc;
            stream_perc << std::fixed << std::setprecision(1) << perf_perc * 100 << "%";

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
            cv::imshow("Pen tracking demo", output);

            cv::waitKey(1);
        }
    }



    cap.release();
    if (video_output)
    {
        cv::destroyAllWindows();
    }



    std::sort(runtimes.begin(), runtimes.end());    

    int pos_01 = (int)(runtimes.size() - 1 - runtimes.size() * 0.001);
    int pos_1 = (int)(runtimes.size() - 1 - runtimes.size() * 0.01);
    int pos_10 = (int)(runtimes.size() - 1 - runtimes.size() * 0.10);
    int pos_25 = (int)(runtimes.size() - 1 - runtimes.size() * 0.25);
    int pos_50 = (int)(runtimes.size() - 1 - runtimes.size() * 0.50);

    double runtimes_avg = std::accumulate(runtimes.begin(), runtimes.end(), 0.0) / runtimes.size();
    double runtimes_var = std::accumulate(
        runtimes.begin(), 
        runtimes.end(), 
        0.0, 
        [runtimes_avg](double sum, double x) { 
            return sum + (x - runtimes_avg) * (x - runtimes_avg); 
        }
    ) / runtimes.size();
    double runtimes_std = std::sqrt(runtimes_var);

    test_results results {
        .video_path = video_path,
        .search_window_perc = search_window_perc,
        .run_visualization = run_visualization,
        
        .time_min_ms = runtimes[0],
        .time_max_ms = runtimes[runtimes.size() - 1],
        .time_percentile_01_ms = runtimes[pos_01],
        .time_percentile_1_ms = runtimes[pos_1],
        .time_percentile_10_ms = runtimes[pos_10],
        .time_percentile_25_ms = runtimes[pos_25],
        .time_percentile_50_ms = runtimes[pos_50],
        .time_avg_ms = runtimes_avg,
        .time_std_ms = runtimes_std,
        
        .failure_count = failure_count,
        .failure_perc = failure_count / (double) frame_count,
        .lost_tracking_count = lost_tracking_count,
        .lost_tracking_perc = lost_tracking_count / (double) frame_count,
        .frame_count = frame_count
    };

    return results;
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



    std::string video_path = argv[1];


    std::vector<test_results> all_results;
    for (double search_window_perc : std::vector<double>{0.01, 0.02, 0.03, 0.04, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3})
    {
        for (bool visualize : std::vector<bool>{true, false})
        {
            std::optional<test_results> result_opt = perform_test(camera_matrix, distortion_coefficients, origin_to_other_transformations, tip_to_origin, video_path, search_window_perc, visualize, true);
            if (result_opt)
            {
                all_results.push_back(*result_opt);
                print_log(*result_opt);
            }
        }
    }


    std::cout << "video_path,search_window_perc,with_visualization,time_min_ms,time_max_ms,time_percentile_01_ms,time_percentile_1_ms,time_percentile_10_ms,"
                 "time_percentile_25_ms,time_percentile_50_ms,time_avg_ms,time_std_ms,failure_count,failure_perc,lost_tracking_count,"
                 "lost_tracking_perc,frame_count" << std::endl;
    for (auto results : all_results)
    {
        std::cout << results.video_path << "," 
            << results.search_window_perc << "," 
            << results.run_visualization << ","
            << results.time_min_ms << "," 
            << results.time_max_ms << "," 
            << results.time_percentile_01_ms << "," 
            << results.time_percentile_1_ms << "," 
            << results.time_percentile_10_ms << "," 
            << results.time_percentile_25_ms << "," 
            << results.time_percentile_50_ms << "," 
            << results.time_avg_ms << "," 
            << results.time_std_ms << "," 
            << results.failure_count << "," 
            << results.failure_perc << "," 
            << results.lost_tracking_count << "," 
            << results.lost_tracking_perc << "," 
            << results.frame_count << std::endl;
    }
    
    
    return 0;
}