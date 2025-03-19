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


    aergo::pen_tracking::MarkerTracker marker_tracker(
        camera_matrix, distortion_coefficients, defaults::pen::getArucoDetector(), 
        defaults::pen::USED_MARKER_IDS, defaults::pen::getMarkerPoints3d(), 
        defaults::pen::IGNORE_MARKERS_ABOVE_ANGLE_DEG, origin_to_other_transformations
    );

    cv::VideoCapture cap(video_path);
    if (!cap.isOpened())
    {
        LOG_ERROR("Failed to open the video");
        return -1;
    }
    double target_fps = cap.get(cv::CAP_PROP_FPS);
    AERGO_LOG("Source video: " << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x" << cap.get(cv::CAP_PROP_FRAME_HEIGHT) << " at " << target_fps << "fps")
    

    std::vector<double> runtimes;

    while (true)
    {
        cv::Mat frame;
        cap.read(frame);
        if (frame.empty())
        {
            break;
        }



        int64_t start_time = cv::getTickCount();

        cv::Mat visualization;
        auto result = marker_tracker.processImage(frame, &visualization);

        double runtime_ms = (cv::getTickCount() - start_time) / cv::getTickFrequency() * 1000;
        double perf_perc = target_fps / (1000 / runtime_ms);
        runtimes.push_back(runtime_ms);



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

        

        cv::Mat output = (visualization.empty()) ? output : visualization;

        if (result.success)
        {
            Transformation camera_to_tip = result.camera_to_origin * tip_to_origin.inverse();
            auto [rvec, tvec] = camera_to_tip.asRvecTvec();
            cv::drawFrameAxes(visualization, camera_matrix, distortion_coefficients, rvec, tvec, 0.01f);
        }

        cv::putText(output, stream.str(), cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(100, 255, 100), 3);
        cv::putText(output, stream_perc.str(), cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 1, perc_color, 3);
        cv::imshow("Pen tracking demo", output);

        cv::waitKey(1);
    }



    cap.release();
    cv::destroyAllWindows();


    std::sort(runtimes.begin(), runtimes.end());

    std::ostringstream stream_results;
    stream_results << std::fixed << std::setprecision(2);
    

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


    stream_results << "Test results:\n";
    stream_results << "\tMin/max:\n";
    stream_results << "\t\tMin: " << runtimes[0] << "ms\n";
    stream_results << "\t\tMax: " << runtimes[runtimes.size() - 1] << "ms\n";
    stream_results << "\tPercentiles:\n";
    stream_results << "\t\t0.1%: " << runtimes[pos_01] << "ms\n";
    stream_results << "\t\t1%: " << runtimes[pos_1] << "ms\n";
    stream_results << "\t\t10%: " << runtimes[pos_10] << "ms\n";
    stream_results << "\t\t25%: " << runtimes[pos_25] << "ms\n";
    stream_results << "\t\t50%: " << runtimes[pos_50] << "ms\n";
    stream_results << "\tAvg/std:\n";
    stream_results << "\t\tResult: " << runtimes_avg << "ms +- " << runtimes_std << "ms\n";
    stream_results << "\n\n\n";
    AERGO_LOG(stream_results.str())
    
    return 0;
}