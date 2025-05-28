#include <opencv2/opencv.hpp>
#include <atomic>



#include <pangolin/pangolin.h>
#include <pangolin/gl/glvbo.h>
#include <Eigen/Core>
#include <vector>



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
        AERGO_LOG("Failed to load camera matrices!")
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
        AERGO_LOG("Failed to load pen calibration data!")
        return false;
    }

    tip_to_origin.translation.at<double>(2) = -defaults::pen::ORIGIN_TO_TIP_DISTANCE;

    return true;
}



// pangolin::OpenGlRenderState s_cam(
//     pangolin::ProjectionMatrix(640, 480, 420, 420, 320, 240, 0.1, 1000),
//     pangolin::ModelViewLookAt(0, -2, -5, 0, 0, 0, pangolin::AxisY)
// );

pangolin::OpenGlRenderState s_cam(
    pangolin::ProjectionMatrix(
        1280, 720,             // image width and height
        915.29, 913.95,        // fx, fy
        638.65, 361.71,        // cx, cy
        0.01, 1000             // near, far
    ),
    pangolin::ModelViewLookAt(0, 0, 0, 0, 0, 1, pangolin::AxisNegY)
);

std::vector<std::vector<Eigen::Vector3f>> pen_paths;
Eigen::Matrix4f pen_pose_3d;

Eigen::Matrix4f createPoseFromRvecTvec(const cv::Mat& rvec, const cv::Mat& tvec) {
    cv::Mat R;
    cv::Rodrigues(rvec, R); // Convert rotation vector to 3x3 matrix

    Eigen::Matrix3f rotation;
    Eigen::Vector3f translation;

    // Copy data from OpenCV to Eigen
    for (int i = 0; i < 3; ++i) {
        translation(i) = (float)tvec.at<double>(i);
        for (int j = 0; j < 3; ++j) {
            rotation(i, j) = (float)R.at<double>(i, j);
        }
    }

    // OpenGL expects column-major matrix, but Pangolin handles it fine
    Eigen::Matrix4f pose = Eigen::Matrix4f::Identity();
    pose.block<3,3>(0,0) = rotation;
    pose.block<3,1>(0,3) = translation;

    return pose;
}

void draw_pen(const Eigen::Matrix4f& pose) {
    glPushMatrix();
    glMultMatrixf(pose.data()); // Apply pose to coordinate system and pen

    // Draw coordinate axes
    pangolin::glDrawAxis(0.01f);

    // Draw cylinder as pen
    // glColor3f(0.1f, 0.8f, 0.1f);
    // pangolin::glDrawCylinder(0.05f, 0.1f, 0.3f);

    glPopMatrix();
}

void draw_path() {
    if (pen_paths.size() < 2) return;

    glColor3f(1.0f, 0.0f, 0.0f);
    for (const auto& pen_path : pen_paths)
    {
        glBegin(GL_LINE_STRIP);
        for (const auto& p : pen_path) {
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
    }
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
        AERGO_LOG("Failed to load camera data!")
        return -1;
    }

    std::atomic<bool> button_primary_pressed = false;
    bool last_button_primary_pressed = false;
    
    aergo::pen_tracking::BleReader ble_reader(aergo::defaults::pen::SERVICE_UUID, aergo::defaults::pen::CHARACTERISTIC_UUID, [&button_primary_pressed](aergo::pen_tracking::PenDataPacket packet) {
        AERGO_LOG("Flags: " << packet.flags << "   valid: " << ((packet.flags & defaults::pen::FLAG_VALID) ? "true" : "false") << "   prim pressed: " << ((packet.flags & defaults::pen::FLAG_BUT_PRIM_PRESSED) ? "true" : "false"))
        if (packet.flags & defaults::pen::FLAG_VALID)
        {
            button_primary_pressed = packet.flags & defaults::pen::FLAG_BUT_PRIM_PRESSED;
        }
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
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

    if (!cap.isOpened())
    {
        return -1;
    }



    pangolin::CreateWindowAndBind("3D Pen Visualizer", 1280, 720);
    glEnable(GL_DEPTH_TEST);

    // 3D camera handler
    pangolin::Handler3D handler(s_cam);

    // Create GUI panel
    pangolin::View& d_cam = pangolin::CreateDisplay()
        .SetBounds(0.0, 1.0, 0, 1.0, -1280/720.0f)
        .SetHandler(&handler);



    std::atomic<bool> quit_requested = false;
    std::atomic<bool> clear_path_requested = false;
    
    pangolin::RegisterKeyPressCallback('q', [&]() {
        quit_requested = true;
    });
    
    pangolin::RegisterKeyPressCallback('c', [&]() {
        clear_path_requested = true;
    });

    int64 start_time = cv::getTickCount();

    while (!pangolin::ShouldQuit())
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

            auto pos = cv_extensions::asPoint(tvec);
            pen_pose_3d = createPoseFromRvecTvec(rvec, tvec);

            bool button_primary_pressed_copy = button_primary_pressed;

            if (!last_button_primary_pressed && button_primary_pressed_copy)
            {
                pen_paths.emplace_back();
            }

            if (button_primary_pressed_copy)
            {
                if (pen_paths.empty())
                {
                    pen_paths.emplace_back();
                }
                pen_paths.back().emplace_back((float)pos.x, (float)pos.y, (float)pos.z);
            }
            last_button_primary_pressed = button_primary_pressed_copy;
        }
        
        cv::putText(output, processing_time_stream.str(), cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, (processing_time_ms < 15) ? cv::Scalar(100, 255, 100) : cv::Scalar(0, 0, 255), 3);

        cv::imshow("Camera visualization", output);



        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        d_cam.Activate(s_cam);

        draw_pen(pen_pose_3d);
        draw_path();

        pangolin::FinishFrame();


        int opencv_key = cv::waitKey(1);
        
        if (opencv_key == 'q' || quit_requested)
        {
            break;
        }

        if (opencv_key == 'c' || clear_path_requested)
        {
            pen_paths.clear();
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