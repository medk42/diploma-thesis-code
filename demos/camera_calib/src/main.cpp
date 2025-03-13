#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>

#include <vector>

#include "defaults.h"
#include "logging.h"

#include "camera_calibration.h"

using namespace aergo;

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        LOG_ERROR("Requires one argument")
        return 0;
    }



    std::string charuco_calib_path = argv[1];

    std::vector<std::string> image_paths;
    cv::glob(charuco_calib_path, image_paths);



    cv::aruco::CharucoBoard charuco_board(
        cv::Size(
            defaults::charucoboard::COL_COUNT,
            defaults::charucoboard::ROW_COUNT
        ),
        defaults::charucoboard::SQUARE_LENGTH,
        defaults::charucoboard::MARKER_LENGTH,
        defaults::charucoboard::DICTIONARY
    );
    charuco_board.setLegacyPattern(defaults::charucoboard::LEGACY_PATTERN);

    camera_calibration::CharucoCalibration charuco_calibration(charuco_board, cv::aruco::CornerRefineMethod::CORNER_REFINE_SUBPIX);



    for (auto&& image_path : image_paths)   
    {
        cv::Mat image = cv::imread(image_path);
        if (image.empty())
        {
            LOG_ERROR("Failed to read image: " + image_path)
            continue;
        }

        cv::Mat visualization;
        bool result = charuco_calibration.addImage(image, &visualization);
        if (result)
        {
            cv::imshow("Found corners", visualization);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
        else
        {
            LOG_ERROR("Failed to calibrate image \"" << image_path << "\"")
        }
    }

    camera_calibration::CalibrationResult result = charuco_calibration.calibrateCamera();

    AERGO_LOG("\n\nSuccess: " << result.success << "\n" << "Camera:\n" << result.camera_matrix << "\n\nDistortion:\n" << result.distortion_coefficients << "\n\nRMS error: " << result.rms_error << "\n\n")

    std::string export_filename = "camera_parameters.xml";

    cv::FileStorage fs(export_filename, cv::FileStorage::WRITE);
    if (!fs.isOpened())
    {
        LOG_ERROR("Failed to open export file \"" << export_filename << "\"");
    }
    else
    {
        try
        {
            fs << "CAMERA_MATRIX" << result.camera_matrix;
            fs << "DISTORTION_COEFFICIENTS" << result.distortion_coefficients;
            fs.release();
            AERGO_LOG("Successfully wrote calibration to file \"" << export_filename << "\"");
        }
        catch(const cv::Exception& e)
        {
            LOG_ERROR("Failed to write to file \"" << export_filename << "\", error: " << e.what());
        }
        
    }

    return 0;
}