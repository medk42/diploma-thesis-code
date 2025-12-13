#pragma once

#include "calib/types.h"
#include "calib/pose_utils.h"

#include <opencv2/objdetect/aruco_detector.hpp>

// z axis points out of the pen tip
// y axis points towards the buttons
// x axis points to complete the right-handed frame (towards the marker 92)

namespace aergo::default_modules::pen_calibration_multicam_module::pen::defaults {

    namespace {
        inline cv::Matx33d rotZ(double rad) // -pi/2
        {
            const double c = std::cos(rad); // 0
            const double s = std::sin(rad); // -1
            return cv::Matx33d(   //  0 1 0   (x,y,z) -> (y,-x,z)
                c, -s, 0,         // -1 0 0
                s,  c, 0,         //  0 0 1
                0,  0, 1
            );
        }

        inline calib::SE3 rotateAboutPenZ(const calib::SE3& T_P_M0, double rad, double offset_z = 0.0, bool rotateTranslation = true)
        {
            const cv::Matx33d Rz = rotZ(rad);
            calib::SE3 out;
            out.R = Rz * T_P_M0.R;                 // rotate orientation in Pen frame
            out.t = rotateTranslation ? (Rz * T_P_M0.t) : T_P_M0.t;  // optional
            out.t[2] += offset_z;                  // optional offset along Pen Z
            return out;
        }

        inline constexpr double PI = 3.14159265358979323846;
        inline constexpr double OFFSET_Z_UPPER_CUBE = -0.029; // vertical offset of upper cube markers in Pen frame
    }

    inline constexpr int DICTIONARY_ID = cv::aruco::DICT_4X4_100;
    inline constexpr int PEN_REFERENCE_MARKER_ID = 92;
    inline constexpr double MARKER_SIZE = 0.013; // in meters
    inline constexpr calib::Vector3 PEN_TIP_INIT_P{0, 0, 0.1307}; // initial tip vector in Pen frame, in meters

    inline constexpr int JIG_MARKER_ID = 90;
    inline constexpr double JIG_MARKER_SIZE = 0.06; // in meters
    
    // CAD marker poses in Pen frame: Pen <- Marker
    inline std::array<std::pair<int, calib::SE3>, 8> getDefaultMarkers() {
        std::array<std::pair<int, calib::SE3>, 8> markers;
    
        markers[0] = {92, calib::SE3{  // Pen <- Marker 92
            .R = cv::Matx33d(
                0.0,  0.0,  1.0,
               -1.0,  0.0,  0.0,
                0.0, -1.0,  0.0
            ),
            .t = cv::Vec3d(0.01, 0.0, 0.0)
        }};
        markers[1] = {93, rotateAboutPenZ(markers[0].second, PI / 2)};
        markers[2] = {94, rotateAboutPenZ(markers[0].second, PI)};
        markers[3] = {95, rotateAboutPenZ(markers[0].second, -PI / 2)};
        markers[4] = {96, rotateAboutPenZ(markers[0].second, -PI / 4, OFFSET_Z_UPPER_CUBE)};
        markers[5] = {97, rotateAboutPenZ(markers[0].second, PI / 4, OFFSET_Z_UPPER_CUBE)};
        markers[6] = {98, rotateAboutPenZ(markers[0].second, PI * 3 / 4, OFFSET_Z_UPPER_CUBE)};
        markers[7] = {99, rotateAboutPenZ(markers[0].second, -PI * 3 / 4, OFFSET_Z_UPPER_CUBE)};
        
        return markers;
    }
    
}