#ifndef AERGO_CAMERA_CALIBRATION_EXCEPTION_H
#define AERGO_CAMERA_CALIBRATION_EXCEPTION_H

#include <exception>
#include <string>

namespace aergo::camera_calibration
{
    /**
     * @brief Exception class for Charuco calibration errors.
     */
    class CameraCalibrationException : public std::exception {
    public:
        /**
         * @brief Constructs a CharucoCalibrationException with a given message.
         * @param message The error message.
         */
        CameraCalibrationException(const std::string& message) : message_(message) {}

        /**
         * @brief Returns the error message.
         * @return The error message.
         */
        const char* what() const throw() {
            return message_.c_str();
        }

    private:
        std::string message_;
    };
} // namespace aergo::camera_calibration

#endif // AERGO_CAMERA_CALIBRATION_EXCEPTION_H