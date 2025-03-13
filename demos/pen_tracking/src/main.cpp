#include <opencv2/opencv.hpp>


#include "logging.h"
#include "pen_calibration_helper.h"


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
}