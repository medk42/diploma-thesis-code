#include <iostream>
#include <vector>
#include <array>
#include <map>

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
    int x = (int)pos1.x;
    int y = (int)pos1.y;

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

struct observed_marker
{
    std::array<cv::Point2f, 4> markers_points_;
    int marker_id_;
};

struct Transformation {
    cv::Mat rotation;    // 3x3 rotation matrix
    cv::Mat translation; // 3x1 translation vector

    Transformation() : rotation(cv::Mat::eye(3, 3, CV_32F)), translation(cv::Mat::zeros(3, 1, CV_32F)) {}

    Transformation(const cv::Mat& rvec, const cv::Mat& tvec) 
    {
        cv::Rodrigues(rvec, rotation);
        translation = tvec.clone();

        rotation.clone().convertTo(rotation, CV_32F);
        translation.clone().convertTo(translation, CV_32F);
    }

    /// Overload the multiply operator to perform the transformation compositions
    Transformation operator*(const Transformation& other) const
    {
        Transformation result;
        result.rotation = this->rotation * other.rotation;
        result.translation = this->rotation * other.translation + this->translation;
        return result;
    }

    cv::Point3f operator*(const cv::Point3f& other) const
    {
        cv::Mat other_mat(3, 1, CV_32F); 
        other_mat.at<float>(0) = other.x;
        other_mat.at<float>(1) = other.y;
        other_mat.at<float>(2) = other.z;
        
        cv::Mat res = rotation * other_mat;
        res += translation;

        return cv::Point3f(res.at<float>(0), res.at<float>(1), res.at<float>(2));
    }

    Transformation inverse() const
    {
        Transformation inv;
        cv::transpose(this->rotation, inv.rotation);
        inv.translation = -inv.rotation * this->translation;
        return inv;
    }

    void print() const
    {
        std::cout << "Rotation:\n" << this->rotation << std::endl;
        std::cout << "Translation:\n" << this->translation << std::endl;
    }
};

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

std::vector<std::vector<observed_marker>> getObservedMarkers(std::string pathname, bool display=false)
{
    std::vector<std::string> image_paths;
    cv::glob(pathname, image_paths);

    cv::aruco::ArucoDetector aruco_detector = getArucoDetector();

    std::vector<std::vector<observed_marker>> observed_markers;

    for (auto&& path : image_paths)
    {
        cv::Mat image = cv::imread(path);

        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

        std::vector<std::vector<cv::Point2f>> corners;
        std::vector<int> ids;

        aruco_detector.detectMarkers(gray, corners, ids);

        std::vector<observed_marker> image_markers;
        for (int i = 0; i < corners.size(); ++i)
        {
            if (defaults::pen::USED_MARKER_IDS.contains(ids[i]))
            {
                observed_marker marker;
                marker.marker_id_ = ids[i];
                marker.markers_points_[0] = corners[i][0];
                marker.markers_points_[1] = corners[i][1];
                marker.markers_points_[2] = corners[i][2];
                marker.markers_points_[3] = corners[i][3];
                
                image_markers.emplace_back(std::move(marker));
            }
        }
        observed_markers.emplace_back(std::move(image_markers));

        if (display)
        {
            cv::aruco::drawDetectedMarkers(image, corners, ids);
            cv::imshow("Image", image);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
    }

    return observed_markers;
}

void calibrateMarkers(cv::Mat& camera_matrix, cv::Mat& distortion_coefficients, std::vector<std::vector<observed_marker>> observed_markers)
{
    auto& single_image = observed_markers[0];
    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> positions;

    std::vector<cv::Point3f> marker_points = 
    {
        cv::Point3f(-defaults::pen::MARKER_SIZE / 2,  defaults::pen::MARKER_SIZE / 2, 0),
        cv::Point3f( defaults::pen::MARKER_SIZE / 2,  defaults::pen::MARKER_SIZE / 2, 0),
        cv::Point3f( defaults::pen::MARKER_SIZE / 2, -defaults::pen::MARKER_SIZE / 2, 0),
        cv::Point3f(-defaults::pen::MARKER_SIZE / 2, -defaults::pen::MARKER_SIZE / 2, 0)
    };

    std::map<int, Transformation> camera_to_marker_transformations;

    for (auto& marker : single_image)
    {
        cv::Mat rvec, tvec;
        cv::solvePnP(marker_points, marker.markers_points_, camera_matrix, distortion_coefficients, rvec, tvec, false, cv::SOLVEPNP_IPPE_SQUARE);

        camera_to_marker_transformations[marker.marker_id_] = Transformation(rvec, tvec);
    }

    auto camera_to_92 = camera_to_marker_transformations[92];
    auto m92_to_camera = camera_to_92.inverse();
    auto m92_to_m93 = m92_to_camera * camera_to_marker_transformations[93];
    auto m92_to_m97 = m92_to_camera * camera_to_marker_transformations[97];

    LOG("92 -> 97 in CAM space:\n")

    m92_to_m97.print();

    LOG("\n\n92 -> 93 in CAM space:\n")

    m92_to_m93.print();

    std::vector<cv::Point3f> test_points = 
    {
        cv::Point3f(-1,  1, 0),
        cv::Point3f( 1,  1, 0),
        cv::Point3f( 1, -1, 0),
        cv::Point3f(-1, -1, 0)
    };
    test_points = marker_points;

    LOG("\n\nTransform:\n\n92\n\t" << test_points[0] << "\n\t" << test_points[1] << "\n\t" << test_points[2] << "\n\t" << test_points[3] << "\n\n")
    LOG("93\n\t" << (m92_to_m93 * test_points[0]) << "\n\t" << (m92_to_m93 * test_points[1]) << "\n\t" << (m92_to_m93 * test_points[2]) << "\n\t" << (m92_to_m93 * test_points[3]))
    LOG("97\n\t" << (m92_to_m97 * test_points[0]) << "\n\t" << (m92_to_m97 * test_points[1]) << "\n\t" << (m92_to_m97 * test_points[2]) << "\n\t" << (m92_to_m97 * test_points[3]))
    
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



    std::vector<std::vector<observed_marker>> observed_markers = getObservedMarkers(argv[1]);
    calibrateMarkers(camera_matrix, distortion_coefficients, observed_markers);


    return 0;
}