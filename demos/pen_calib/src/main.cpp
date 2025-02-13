#include <iostream>
#include <vector>

#include <opencv2/opencv.hpp>
#include <ceres/ceres.h>

#include "logging.h"
#include "defaults.h"

using namespace aergo;

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

void displayError(cv::Mat& image, cv::Point2f pos1, cv::Point2f pos2)
{
    int x = pos1.x;
    int y = pos1.y;

    cv::Rect2i roi(x - 25, y - 25, 50, 50);
    cv::Mat cropped = image(roi);
    cv::Mat scaled;
    cv::resize(cropped, scaled, cv::Size(1000, 1000), cv::INTER_CUBIC);

    float p1x = (pos1.x - x + 25) * 20;
    float p1y = (pos1.y - y + 25) * 20;
    float p2x = (pos2.x - x + 25) * 20;
    float p2y = (pos2.y - y + 25) * 20;

    cv::line(scaled, cv::Point2f(p1x - 20, p1y - 20), cv::Point2f(p1x + 20, p1y + 20), cv::Scalar(0, 0, 255));
    cv::line(scaled, cv::Point2f(p1x - 20, p1y + 20), cv::Point2f(p1x + 20, p1y - 20), cv::Scalar(0, 0, 255));

    cv::line(scaled, cv::Point2f(p2x - 20, p2y - 20), cv::Point2f(p2x + 20, p2y + 20), cv::Scalar(0, 255, 0));
    cv::line(scaled, cv::Point2f(p2x - 20, p2y + 20), cv::Point2f(p2x + 20, p2y - 20), cv::Scalar(0, 255, 0)); 

    cv::imshow("Error", scaled);
    cv::waitKey(0);
    cv::destroyAllWindows();
}

void getObservedMarkers(std::string pathname)
{
    std::vector<std::string> image_paths;
    cv::glob(pathname, image_paths);

    cv::aruco::DetectorParameters detector_parameters;
    detector_parameters.cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
    // detector_parameters.cornerRefinementWinSize = 2;
    // detector_parameters.adaptiveThreshWinSizeMin = 15;
    // detector_parameters.adaptiveThreshWinSizeMax = 15;
    // detector_parameters.useAruco3Detection = false;
    // detector_parameters.minMarkerPerimeterRate = 0.02;
    // detector_parameters.maxMarkerPerimeterRate = 2;
    // detector_parameters.minSideLengthCanonicalImg = 16;
    // detector_parameters.adaptiveThreshConstant = 7;

    cv::aruco::ArucoDetector aruco_detector(
        defaults::pen::DICTIONARY, 
        cv::aruco::DetectorParameters(), 
        cv::aruco::RefineParameters()
    );

    cv::aruco::ArucoDetector aruco_detector2(
        defaults::pen::DICTIONARY, 
        detector_parameters, 
        cv::aruco::RefineParameters()
    );

    for (auto&& path : image_paths)
    {
        cv::Mat image = cv::imread(path);

        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

        std::vector<std::vector<cv::Point2f>> corners;
        std::vector<int> ids;

        aruco_detector.detectMarkers(gray, corners, ids);

        cv::Mat image_draw = image.clone();
        cv::aruco::drawDetectedMarkers(image_draw, corners, ids);
    

        cv::imshow("Image", image_draw);
        cv::waitKey(0);
        cv::destroyAllWindows();

        std::vector<std::vector<cv::Point2f>> corners2;
        std::vector<int> ids2;
        aruco_detector2.detectMarkers(gray, corners2, ids2);

        image_draw = image.clone();
        cv::aruco::drawDetectedMarkers(image_draw, corners, ids);

        cv::imshow("Image", image_draw);
        cv::waitKey(0);
        cv::destroyAllWindows();

        if (corners.size() == corners2.size())
        {
            for (int i = 0; i < corners.size(); ++i)
            {
                displayError(image, corners[i][0], corners2[i][0]);
                displayError(image, corners[i][1], corners2[i][1]);
                displayError(image, corners[i][2], corners2[i][2]);
                displayError(image, corners[i][3], corners2[i][3]);
            }
        }
    }
}

void calibrateMarkers(cv::Mat& camera_matrix, cv::Mat& distortion_coefficients)
{

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

    getObservedMarkers(argv[1]);



    return 0;
}