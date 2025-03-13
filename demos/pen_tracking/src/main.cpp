#include <opencv2/opencv.hpp>


#include "logging.h"
#include "defaults.h"
#include "pen_calibration_helper.h"


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


cv::aruco::ArucoDetector getArucoDetector()
{
    cv::aruco::DetectorParameters detector_parameters;
    detector_parameters.cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;

    cv::aruco::ArucoDetector aruco_detector(
        defaults::pen::DICTIONARY, 
        detector_parameters
    );

    return std::move(aruco_detector);
}


std::vector<cv::Point3f> getMarkerPoints3d(float marker_size_)
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


    auto aruco_detector = getArucoDetector();



    std::vector<std::string> image_paths;
    cv::glob(argv[1], image_paths);

    int max_image = 5;
    if (image_paths.size() > max_image)
    {
        image_paths.erase(image_paths.begin() + max_image, image_paths.end());
    }
    
    for (auto&& path : image_paths)
    {
        cv::Mat image = cv::imread(path);

        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

        std::vector<std::vector<cv::Point2f>> corners;
        std::vector<int> ids;
        aruco_detector.detectMarkers(gray, corners, ids);

        cv::aruco::drawDetectedMarkers(image, corners, ids);

        for (int i = 0; i < corners.size(); ++i)
        {
            if (defaults::pen::USED_MARKER_IDS.contains(ids[i]))
            {
                cv::Mat rvec, tvec;
                bool success = cv::solvePnP(
                    getMarkerPoints3d(defaults::pen::MARKER_SIZE), corners[i], 
                    camera_matrix, distortion_coefficients, 
                    rvec, tvec, false, cv::SOLVEPNP_IPPE_SQUARE
                );
                if (!success)
                {
                    continue;
                }

                double angle_off_axis = cv::abs(180 - cv::norm(rvec) * 180.0 / CV_PI);

                if (angle_off_axis < defaults::pen::IGNORE_MARKERS_ABOVE_ANGLE_DEG)
                {
                    Transformation camera_to_marker = Transformation(rvec, tvec);
                    Transformation origin_to_marker = origin_to_other_transformations[ids[i]];
                    Transformation camera_to_origin = camera_to_marker * origin_to_marker.inverse();
                    Transformation camera_to_tip = camera_to_origin * tip_to_origin.inverse();

                    auto image2 = image.clone();

                    float axis_size = defaults::pen::MARKER_SIZE / 2;
                    auto [rvec1, tvec1] = camera_to_marker.asRvecTvec();
                    cv::drawFrameAxes(image, camera_matrix, distortion_coefficients, rvec1, tvec1, axis_size);

                    auto [rvec2, tvec2] = camera_to_origin.asRvecTvec();
                    // cv::drawFrameAxes(image2, camera_matrix, distortion_coefficients, rvec2, tvec2, axis_size);
                    
                    // auto [rvec3, tvec3] = camera_to_origin.asRvecTvec();
                    // cv::drawFrameAxes(image2, camera_matrix, distortion_coefficients, rvec3, tvec3, axis_size);




                    float length = 0.1f;
                    std::vector<cv::Point3f> axisPoints = {
                        {0, 0, 0},               // Origin
                        {length, 0, 0},          // X-axis (red)
                        {0, length, 0},          // Y-axis (green)
                        {0, 0, length}           // Z-axis (blue)
                    };
                
                    // Project the 3D points to 2D
                    std::vector<cv::Point2f> imagePoints;
                    cv::projectPoints(axisPoints, rvec2, tvec2, camera_matrix, distortion_coefficients, imagePoints);
                
                    // Draw the axes
                    cv::line(image, imagePoints[0], imagePoints[1], cv::Scalar(0, 0, 255), 1);  // X-axis (red)
                    cv::line(image, imagePoints[0], imagePoints[2], cv::Scalar(0, 255, 0), 1);  // Y-axis (green)
                    cv::line(image, imagePoints[0], imagePoints[3], cv::Scalar(255, 0, 0), 1);  // Z-axis (blue)








                    
                }
            }
        }
        
        cv::imshow("image", image);
        cv::waitKey(0);
        cv::destroyAllWindows();

        
    }
}