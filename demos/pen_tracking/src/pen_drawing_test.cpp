#include <opencv2/opencv.hpp>



#include <pangolin/pangolin.h>



#include "logging.h"
#include "defaults.h"
#include "pen_calibration_helper.h"
#include "marker_tracker.h"
#include "ble_reader.h"



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



bool loadCameraData(std::string camera_filename, std::string pen_calib_filename, cv::Mat& camera_matrix, cv::Mat& distortion_coefficients, std::map<int, Transformation>& origin_to_other_transformations, Transformation& tip_to_origin)
{
    
    if (loadCameraCalibration(camera_filename, camera_matrix, distortion_coefficients))
    {
        AERGO_LOG("Loaded Camera Matrix:\n" << camera_matrix << "\n\nLoaded Distortion Coefficients:\n" << distortion_coefficients << "\n")
    } 
    else 
    {
        return false;
    }

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
        return false;
    }

    tip_to_origin.translation.at<double>(2) = -defaults::pen::ORIGIN_TO_TIP_DISTANCE;

    return true;
}



int main(int argc, char** argv)
{
    if (argc != 3)
    {
        LOG_ERROR("Wrong number of arguments: " << argc - 1 << " (required 2, [camera params] [pen calibration])")
    }

    cv::Mat camera_matrix, distortion_coefficients;
    std::map<int, Transformation> origin_to_other_transformations;
    Transformation tip_to_origin;
    if (!loadCameraData(argv[1], argv[2], camera_matrix, distortion_coefficients, origin_to_other_transformations, tip_to_origin))
    {
        return -1;
    }

    
    aergo::pen_tracking::BleReader ble_reader(aergo::defaults::pen::SERVICE_UUID, aergo::defaults::pen::CHARACTERISTIC_UUID, [](aergo::pen_tracking::PenDataPacket packet) {
        AERGO_LOG("Flags: " << packet.flags)
    });

    if (!ble_reader.start())
    {
        return -1;
    }
    




    double search_window_perc = 9;
    aergo::pen_tracking::MarkerTracker marker_tracker(
        camera_matrix, distortion_coefficients, defaults::pen::getArucoDetector(), 
        defaults::pen::USED_MARKER_IDS, defaults::pen::getMarkerPoints3d(), 
        origin_to_other_transformations, search_window_perc
    );


    cv::VideoCapture cap(0, cv::CAP_DSHOW);
    // cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

    if (!cap.isOpened())
    {
        return -1;
    }

    while (true)
    {
        cv::Mat frame;
        cap.read(frame);
        if (frame.empty())
        {
            return -1;
        }

        cv::Mat visualization;
        int64_t start_time = cv::getTickCount();
        auto result = marker_tracker.processImage(frame, &visualization);
        double processing_time_ms = (cv::getTickCount() - start_time) / cv::getTickFrequency() * 1000;
        std::ostringstream processing_time_stream;
        processing_time_stream << std::fixed << std::setprecision(1) << processing_time_ms << "ms";

        if (processing_time_ms > 50)
        {
            LOG_ERROR("PROCESSING TOOK " << processing_time_ms << "ms!")
        }

        cv::Mat output = (visualization.empty()) ? frame : visualization;

        if (result.success)
        {
            Transformation camera_to_tip = result.camera_to_origin * tip_to_origin.inverse();
            auto [rvec, tvec] = camera_to_tip.asRvecTvec();
            cv::drawFrameAxes(visualization, camera_matrix, distortion_coefficients, rvec, tvec, 0.01f);
        }
        
        cv::putText(output, processing_time_stream.str(), cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, (processing_time_ms < 15) ? cv::Scalar(100, 255, 100) : cv::Scalar(0, 0, 255), 3);

        cv::imshow("Camera visualization", output);
        
        if (cv::waitKey(1) == 'q')
        {
            break;
        }
    }

    cv::destroyAllWindows();

    AERGO_LOG("Waiting for BLE stop...")
    if (!ble_reader.stop())
    {
        return -1;
    }

    return 0;
}