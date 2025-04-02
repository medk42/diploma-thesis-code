#include <opencv2/opencv.hpp>
#include <vector>
#include <mutex>
#include <cmath>

#include "defaults.h"
#include "ble_reader.h"
#include "logging.h"



int main() {
    std::mutex data_mutex;
    aergo::pen_tracking::PenDataPacket packet_shared;

    aergo::pen_tracking::BleReader ble_reader(aergo::defaults::pen::SERVICE_UUID, aergo::defaults::pen::CHARACTERISTIC_UUID, [&data_mutex, &packet_shared](aergo::pen_tracking::PenDataPacket packet) {
        std::stringstream ss;

        cv::Vec3d accel = packet.getAccelScaled(aergo::defaults::pen::ACCEL_RANGE);
        cv::Vec3d gyro = packet.getGyroScaled(aergo::defaults::pen::GYRO_RANGE);

        ss << std::fixed << std::setprecision(3);
        ss << "accel: " << accel[0] << ", " << accel[1] << ", " << accel[2];
        int ll = (int)ss.str().length();
        for (int i = 0; i < 30 - ll; ++i)
        {
            ss << " ";
        }

        double max_gyro = 573;
        ss << "  gyro: " << gyro[0] << ", " << gyro[1] << ", " << gyro[2] << ((std::abs(gyro[0]) > max_gyro || std::abs(gyro[1]) > max_gyro || std::abs(gyro[2]) > max_gyro) ? " MAX_REACH" : "") << "                       \r";
        std::cout << ss.str() << std::flush;


        std::lock_guard<std::mutex> lock(data_mutex);
        packet_shared = packet;
    });

    bool res = ble_reader.start();
    AERGO_LOG("Start success: " << res)
    if (!res)
    {
        return 1;
    }


    // Define 3D points (e.g. a cube or coordinate axes)
    std::vector<cv::Point3f> objectPoints = {
        {0, 0, 0},   // origin
        {1, 0, 0},   // X
        {0, 1, 0},   // Y
        {0, 0, 1},   // Z
        {0, 0, 1}    // point from accel 
    };

    // Camera intrinsic matrix: [fx, 0, cx], [0, fy, cy], [0, 0, 1]
    cv::Mat cameraMatrix = (cv::Mat_<double>(3,3) <<
        500, 0, 320,
        0, 500, 240,
        0, 0, 1
    );

    // Distortion coefficients: assume no distortion
    cv::Mat distCoeffs = cv::Mat::zeros(5, 1, CV_64F);

    // Rotation and translation (camera pose)
    // For demo, we'll rotate around Z and translate a bit
    double angle = 0;
    while (true) {
        aergo::pen_tracking::PenDataPacket packet;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            packet = packet_shared;
        }

        // cv::Mat rvec = accelToRvec(cv::Vec3f(packet.accel[0], packet.accel[1], packet.accel[2]));

        // cv::Mat rvec = (cv::Mat_<double>(3,1) << 1, 2, angle);
        cv::Mat rvec = (cv::Mat_<double>(3,1) << 1, 2, 0);
        cv::Mat tvec = (cv::Mat_<double>(3,1) << 0, 0, 5);  // move camera back

        cv::Vec3f accel_vec = (cv::Vec3f)packet.getAccelScaled(aergo::defaults::pen::ACCEL_RANGE);
        objectPoints[4].x = accel_vec[0];
        objectPoints[4].y = accel_vec[1];
        objectPoints[4].z = accel_vec[2];
        objectPoints[1].x = accel_vec[0];
        objectPoints[2].y = accel_vec[1];
        objectPoints[3].z = accel_vec[2];

        std::vector<cv::Point2f> imagePoints;
        cv::projectPoints(objectPoints, rvec, tvec, cameraMatrix, distCoeffs, imagePoints);

        // Draw
        cv::Mat img = cv::Mat::zeros(480, 640, CV_8UC3);
        cv::circle(img, imagePoints[0], 5, {255,255,255}, -1); // origin
        cv::line(img, imagePoints[0], imagePoints[1], {0,0,255}, 2); // X - red
        cv::line(img, imagePoints[0], imagePoints[2], {0,255,0}, 2); // Y - green
        cv::line(img, imagePoints[0], imagePoints[3], {255,0,0}, 2); // Z - blue
        cv::line(img, imagePoints[0], imagePoints[4], {255,255,0}, 2); // accel - cyan

        cv::imshow("Projected Axes", img);
        if (cv::waitKey(10) == 27) break; // ESC to quit

        angle += 0.01;
    }

    cv::destroyAllWindows();

    res = ble_reader.stop();
    AERGO_LOG("Stop success: " << res)

    return 0;
}
