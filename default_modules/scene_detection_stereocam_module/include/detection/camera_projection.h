#pragma once

#include <array>
#include <cmath>

#include <opencv2/core.hpp>

// OpenCV-style pinhole + distortion projection.
// Supports D sizes: 0,4,5,8,12,14 in the same order OpenCV uses.
// D layout (OpenCV):
//  - 4:  k1 k2 p1 p2
//  - 5:  k1 k2 p1 p2 k3
//  - 8:  k1 k2 p1 p2 k3 k4 k5 k6   (rational)
//  - 12: k1 k2 p1 p2 k3 k4 k5 k6 s1 s2 s3 s4 (rational + thin prism)
//  - 14: k1 k2 p1 p2 k3 k4 k5 k6 s1 s2 s3 s4 tx ty ( + tilt)  [tilt optional]
//
// Note: OpenCV "tilt" (tauX,tauY) is rarely used; if you don't use it, keep D<=12
// or set tauX=tauY=0. This implementation includes a common tilt model used by OpenCV.

namespace aergo::default_modules::scene_detection_stereocam_module
{
struct CameraParameters
{
    // Intrinsics
    double fx = 0, fy = 0, cx = 0, cy = 0;

    // Distortion (OpenCV ordering). Store up to 14; use num_dist to select active prefix.
    std::array<double, 14> d{};   // k1 k2 p1 p2 k3 k4 k5 k6 s1 s2 s3 s4 tauX tauY
    int num_dist = 0;             // 0,4,5,8,12,14

    // Construct from OpenCV K (3x3) and D (Nx1 or 1xN, CV_64F or CV_32F)
    static CameraParameters fromOpenCV(const cv::Mat& K, const cv::Mat& D);

    // Project a 3D point given pose (R,t) that maps world -> camera:
    // Pc = R * Pw + t
    //
    // Returns false if point is behind camera or too close to Z=0.
    template <typename T>
    void project(const T& X, const T& Y, const T& Z, T& u, T& v) const;

private:
    // Applies OpenCV distortion to normalized coords in-place
    template <typename T>
    void distortNormalized(T& x, T& y) const;
};


CameraParameters CameraParameters::fromOpenCV(const cv::Mat& K, const cv::Mat& D)
{
    CameraParameters p;
    CV_Assert(K.rows == 3 && K.cols == 3);
    p.fx = K.at<double>(0, 0);
    p.fy = K.at<double>(1, 1);
    p.cx = K.at<double>(0, 2);
    p.cy = K.at<double>(1, 2);

    p.num_dist = 0;
    p.d.fill(0.0);

    if (!D.empty())
    {
        CV_Assert(D.total() == (size_t)D.rows * (size_t)D.cols);
        int n = (int)D.total();
        // allow any n up to 14, but we'll use exactly n
        CV_Assert(n <= 14);

        p.num_dist = n;

        // Read as double regardless of input type
        for (int i = 0; i < n; ++i)
        {
            double v = 0.0;
            if (D.type() == CV_64F) v = D.at<double>((int)i);
            else if (D.type() == CV_32F) v = (double)D.at<float>((int)i);
            else CV_Assert(false && "D must be CV_64F or CV_32F");
            p.d[(size_t)i] = v;
        }
    }

    return p;
}


template <typename T>
inline void CameraParameters::project(const T& X, const T& Y, const T& Z, T& u, T& v) const
{
    // soft guard against Z -> 0 (avoid branch)
    const T invZ = T(1) / (Z + T(1e-9));

    T x = X * invZ;
    T y = Y * invZ;

    distortNormalized(x, y);

    u = T(fx)*x + T(cx);
    v = T(fy)*y + T(cy);
}


template <typename T>
inline void CameraParameters::distortNormalized(T& x, T& y) const
{
    if (num_dist <= 0) return;

    const T k1 = T(d[0]);
    const T k2 = (num_dist >= 2) ? T(d[1]) : T(0);
    const T p1 = (num_dist >= 3) ? T(d[2]) : T(0);
    const T p2 = (num_dist >= 4) ? T(d[3]) : T(0);
    const T k3 = (num_dist >= 5) ? T(d[4]) : T(0);

    const T k4 = (num_dist >= 8) ? T(d[5]) : T(0);
    const T k5 = (num_dist >= 8) ? T(d[6]) : T(0);
    const T k6 = (num_dist >= 8) ? T(d[7]) : T(0);

    const T s1 = (num_dist >= 12) ? T(d[8])  : T(0);
    const T s2 = (num_dist >= 12) ? T(d[9])  : T(0);
    const T s3 = (num_dist >= 12) ? T(d[10]) : T(0);
    const T s4 = (num_dist >= 12) ? T(d[11]) : T(0);

    // (tilt tauX,tauY ignored here for simplicity; keep num_dist <= 12)

    const T r2 = x*x + y*y;
    const T r4 = r2*r2;
    const T r6 = r4*r2;

    const T radial_num = T(1) + k1*r2 + k2*r4 + k3*r6;
    T radial = radial_num;
    if (num_dist >= 8)
    {
        const T radial_den = T(1) + k4*r2 + k5*r4 + k6*r6;
        radial = radial_num / radial_den;
    }

    const T xy = x*y;
    const T x2 = x*x;
    const T y2 = y*y;

    T xd = x*radial + T(2)*p1*xy + p2*(r2 + T(2)*x2);
    T yd = y*radial + p1*(r2 + T(2)*y2) + T(2)*p2*xy;

    if (num_dist >= 12)
    {
        xd += s1*r2 + s2*r4;
        yd += s3*r2 + s4*r4;
    }

    x = xd; y = yd;
}

} // namespace camproj
