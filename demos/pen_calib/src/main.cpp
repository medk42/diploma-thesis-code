#include <iostream>

#include <opencv2/opencv.hpp>
#include <ceres/ceres.h>

#include "logging.h"

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

    98 ... (0, 0.01, 0.01), rot 

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

// struct SimpleCostFunction {
//     bool operator()(const double* const x, double* residual) const {
//         residual[0] = (x[0] - (2.0)) * (x[0] - (2.0)) * (x[0] + (2.0));
//         return true;
//     }
// };

struct MyCostFunctor {
    bool operator()(const double* params, double* residuals) const {
        residuals[0] = params[0] - 3;  // x3 -> 3
        residuals[1] = params[1] - 4;  // x4 -> 4
        return true;
    }
};

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

    std::vector<double> params = {10.0, 20.0, 0.0, 0.0};
    std::cout << "Before optimization: "
    << params[0] << " " << params[1] << " " << params[2] << " " << params[3] << std::endl;

    ceres::Problem problem;

    // Add residual block (uses all 4 elements)
    problem.AddResidualBlock(
        new ceres::NumericDiffCostFunction<MyCostFunctor, ceres::CENTRAL, 2, 2>(
            new MyCostFunctor()
        ), 
        nullptr, 
        params.data()
    );
    problem.AddResidualBlock(
        new ceres::NumericDiffCostFunction<MyCostFunctor, ceres::CENTRAL, 2, 2>(
            new MyCostFunctor()
        ), 
        nullptr, 
        params.data() + 2
    );

    // Keep first 2 elements fixed
    problem.SetParameterBlockConstant(params.data());

    ceres::Solver::Options options;
    // options.linear_solver_type = ceres::DENSE_QR;
    options.minimizer_progress_to_stdout = true;
    // options.max_num_iterations = 1000;

    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);

    std::cout << summary.FullReport() << std::endl;
    std::cout << "After optimization: "
              << params[0] << " " << params[1] << " " << params[2] << " " << params[3] << std::endl;


    return 0;
}