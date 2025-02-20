#include <iostream>
#include <vector>
#include <array>
#include <map>
#include <queue>

#include <opencv2/opencv.hpp>
#include <ceres/ceres.h>

#include "logging.h"
#include "defaults.h"
#include "pen_calibration.h"

using namespace aergo;
using namespace pen_calibration;

/*

Pen has 13x13mm calibration markers. DPoint multiplies this by 0.97.abort
White around markers is 19x19mm. 
Top of top cube to top of bottom cube is 30mm.
    (DPoint takes middle of top at 10mm from top and middle of bottom 39.5mm from the top)
Cubes are 20x20x20mm.
Pen tip is 170.7mm from pen top.

Let's consider:
    Origin is at the tip
    Z = bottom (makes sense for robot)
    Y = towards the buttons (= side with code 93)
    X = the remaining axis

We will originally define the position relative to the top of the pen.
    Easy to define for the markers.
    Then we will run the pen calibration procedure.
    That will fix marker 92 and the rest will get updated positions. 
    This will cause everything to be defined relative to marker 92 - if it is not glued properly, BAD.
        We will then run the procedure to find the pen tip. When we do, we will transform the pen 
        tracker positions based on the tip (tip will newly be at (0,0,0)). Or we will just remember the 
        transformation from marker to tip. 

    92 ... (0, 0.01, 0.04), rot -90=270
    93 ... (0, 0.01, 0.04), rot 0
    94 ... (0, 0.01, 0.04), rot 90
    95 ... (0, 0.01, 0.04), rot 180

    96 ... (0, 0.01, 0.01), rot -135 = 225
    97 ... (0, 0.01, 0.01), rot -45 = 315
    98 ... (0, 0.01, 0.01), rot 45
    99 ... (0, 0.01, 0.01), rot 135


    id: rvec  tvec
    92: 0,0,0  0,0,0
    93: 0,-pi/2,0  -0.01, 0, -0.01
    94: 0,-pi,0  0,0,-0.02
    95: 0,pi/2,0  0.01,0,-0.01

    96: 0,pi/4,0  sqrt(50),0.03,-(10-sqrt(50))
    97: 0,-pi/4,0  -sqrt(50),0.03,-(10-sqrt(50))
    98: 0,-3/4pi,0  -sqrt(50),0.03,-(10+sqrt(50))
    99: 0,3/4pi,0  sqrt(50),0.03,-(10+sqrt(50))


    // optimal transformation definition by hand
    // fixed_marker_to_other_transformations[92] = Transformation(0, 0, 0,    0, 0, 0);
    // fixed_marker_to_other_transformations[93] = Transformation(0, -CV_PI/2, 0,    -0.01, 0, -0.01);
    // fixed_marker_to_other_transformations[94] = Transformation(0, -CV_PI, 0,    0, 0, -0.02);
    // fixed_marker_to_other_transformations[95] = Transformation(0, CV_PI/2, 0,    0.01, 0, -0.01);

    // fixed_marker_to_other_transformations[96] = Transformation(0, CV_PI/4, 0,    cv::sqrt(50)/1000.0, 0.03, -(10 - cv::sqrt(50)) / 1000.0);
    // fixed_marker_to_other_transformations[97] = Transformation(0, -CV_PI/4, 0,    -cv::sqrt(50)/1000.0, 0.03, -(10 - cv::sqrt(50)) / 1000.0);
    // fixed_marker_to_other_transformations[98] = Transformation(0, -3*CV_PI/4, 0,    -cv::sqrt(50)/1000.0, 0.03, -(10 + cv::sqrt(50)) / 1000.0);
    // fixed_marker_to_other_transformations[99] = Transformation(0, 3*CV_PI/4, 0,    cv::sqrt(50)/1000.0, 0.03, -(10 + cv::sqrt(50)) / 1000.0);

*/


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



void printCalibrationResults(const PenCalibrationResult& result) {
    // Print result status
    std::cout << "Calibration Result: ";
    switch (result.result_) {
        case PenCalibrationResult::Result::SUCCESS:
            std::cout << "SUCCESS" << std::endl;
            break;
        case PenCalibrationResult::Result::FAILED_TO_BUILD_GRAPH:
            std::cout << "FAILED TO BUILD GRAPH" << std::endl;
            break;
        case PenCalibrationResult::Result::SANITY_CHECK_FAIL:
            std::cout << "SANITY CHECK FAIL" << std::endl;
            break;
        case PenCalibrationResult::Result::SOLVER_NO_CONVERGENCE:
            std::cout << "SOLVER DID NOT CONVERGE" << std::endl;
            break;
        case PenCalibrationResult::Result::SOLVER_FAIL:
            std::cout << "SOLVER FAILED" << std::endl;
            break;
        case PenCalibrationResult::Result::OPPOSITE_FAIL:
            std::cout << "FAILED TO DETERMINE OPPOSITE MARKERS" << std::endl;
            break;
    }

    if (result.result_ != PenCalibrationResult::Result::SUCCESS)
    {
        return;
    }

    // Print metrics
    std::cout << "\nCalibration Metrics:" << std::endl;
    std::cout << "---------------------" << std::endl;
    std::cout << "Mean Reprojection Error (after initialization): " << result.metrics_.init_mre_ << std::endl;
    std::cout << "Root Mean Squared Reprojection Error (after initialization): " << result.metrics_.init_rmsre_ << std::endl;
    std::cout << "Mean Reprojection Error (after optimization): " << result.metrics_.final_mre_ << std::endl;
    std::cout << "Root Mean Squared Reprojection Error (after optimization): " << result.metrics_.final_rmsre_ << std::endl;

    // Print solver statistics
    std::cout << "\nSolver Statistics:" << std::endl;
    std::cout << "-----------------" << std::endl;
    std::cout << "Solver Run Time (seconds): " << result.solver_stats_.solver_time_ << std::endl;
    std::cout << "Initial Cost: " << std::scientific << result.solver_stats_.solver_initial_cost_ << std::fixed << std::endl;
    std::cout << "Final Cost: " << std::scientific << result.solver_stats_.solver_final_cost_ << std::fixed << std::endl;

    // Print opposite markers data
    if (!result.opposite_markers_.empty()) {
        std::cout << "\nOpposite Markers Data:" << std::endl;
        std::cout << "---------------------" << std::endl;
        for (const auto& oppositeMarker : result.opposite_markers_) {
            std::cout << "First Marker ID: " << oppositeMarker.first_id_ << std::endl;
            std::cout << "Second Marker ID: " << oppositeMarker.second_id_ << std::endl;
            std::cout << "Angle (degrees): " << oppositeMarker.angle_deg_ << std::endl;
            std::cout << "Distance (mm): " << oppositeMarker.distance_m_  * 1000 << std::endl;
            std::cout << "---------------------" << std::endl;
        }
    }
}



int main(int argc, char* argv[]) {
    if (argc != 3)
    {
        LOG_ERROR("Requires two arguments!")
        return 0;
    }



    // File containing camera calibration data
    std::string filename = argv[2];

    cv::Mat camera_matrix, distortion_coefficients;

    if (loadCameraCalibration(filename, camera_matrix, distortion_coefficients))
    {
        LOG("Loaded Camera Matrix:\n" << camera_matrix << "\n\nLoaded Distortion Coefficients:\n" << distortion_coefficients << "\n")
    } 
    else 
    {
        return -1;
    }



    pen_calibration::PenCalibration pen_calibration(
        camera_matrix, distortion_coefficients, 
        std::move(getArucoDetector()), defaults::pen::USED_MARKER_IDS,
        defaults::pen::MARKER_SIZE, defaults::pen::IGNORE_MARKERS_ABOVE_ANGLE_DEG,
        defaults::pen::PEN_FIXED_MARKER_ID
    );

    std::vector<std::string> image_paths;
    cv::glob(argv[1], image_paths);

    for (auto&& path : image_paths)
    {
        cv::Mat image = cv::imread(path);
        
        cv::Mat result;
        bool success = pen_calibration.addImage(image, &result);

        if (success)
        {
            cv::putText(result, "SUCCESS", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 3);
            cv::imshow("result", result);
            cv::waitKey(100);
        }
        else
        {
            cv::putText(result, "FAIL", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 3);
            cv::imshow("result", image);
            cv::waitKey(1000);
        }
    }
    cv::destroyAllWindows();


    aergo::pen_calibration::PenCalibrationResult result = pen_calibration.calibratePen();
    printCalibrationResults(result);

    return 0;
}