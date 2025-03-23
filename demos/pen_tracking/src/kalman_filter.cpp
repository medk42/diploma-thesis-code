#include "kalman_filter.h"

using namespace aergo::pen_tracking;

KalmanFilterPosition::KalmanFilterPosition(float dt) : dt_(dt) {
    state_ = Eigen::VectorXf::Zero(6);
    A_ = Eigen::MatrixXf::Identity(6, 6);
    for (int i = 0; i < 3; ++i) A_(i, i + 3) = dt_;

    H_ = Eigen::MatrixXf::Zero(3, 6);
    H_.block<3, 3>(0, 0) = Eigen::Matrix3f::Identity();

    P_ = Eigen::MatrixXf::Identity(6, 6);
    Q_ = Eigen::MatrixXf::Identity(6, 6) * 1e-3f;
    R_ = Eigen::MatrixXf::Identity(3, 3) * 5e-2f;
    I_ = Eigen::MatrixXf::Identity(6, 6);
}

Eigen::Vector3f KalmanFilterPosition::update(Eigen::Vector3f measured_position) {
    if (!initialized_)
    {
        state_.segment<3>(0) = measured_position;
        state_.segment<3>(3) = Eigen::Vector3f::Zero();

        initialized_ = true;

        return measured_position;
    }

    // Predict
    state_ = A_ * state_;
    P_ = A_ * P_ * A_.transpose() + Q_;

    // Correct
    Eigen::VectorXf z = measured_position;
    Eigen::VectorXf y = z - H_ * state_; // Residual
    Eigen::MatrixXf S = H_ * P_ * H_.transpose() + R_;
    Eigen::MatrixXf K = P_ * H_.transpose() * S.inverse();
    state_ = state_ + K * y;
    P_ = (I_ - K * H_) * P_;

    return state_.segment<3>(0);
}
