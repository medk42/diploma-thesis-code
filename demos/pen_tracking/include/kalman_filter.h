#ifndef AERGO_KALMAN_FILTER_H
#define AERGO_KALMAN_FILTER_H

#include "Eigen/Dense"

namespace aergo::pen_tracking
{

    class KalmanFilterPosition {
    public:
        KalmanFilterPosition(float dt);

        Eigen::Vector3f update(Eigen::Vector3f measured_position);

    private:
        Eigen::VectorXf state_;          // [x y z vx vy vz]
        Eigen::MatrixXf A_;              // State transition
        Eigen::MatrixXf H_;              // Measurement model
        Eigen::MatrixXf P_;              // Estimate uncertainty
        Eigen::MatrixXf Q_;              // Process noise
        Eigen::MatrixXf R_;              // Measurement noise
        Eigen::MatrixXf I_;              // Identity

        float dt_;
        bool initialized_ = false;
    };
} // namespace aergo::pen_tracking

#endif // AERGO_KALMAN_FILTER_H