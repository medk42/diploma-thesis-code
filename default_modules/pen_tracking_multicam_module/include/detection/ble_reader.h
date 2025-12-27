#pragma once

#include <atomic>
#include <thread>
#include <mutex>
#include <optional>
#include <memory>
#include <simpleble/SimpleBLE.h>
#include <opencv2/opencv.hpp>

namespace aergo::default_modules::pen_tracking_multicam_module
{
    struct PenDataPacket {
        int16_t accel[3];
        int16_t gyro[3];
        uint16_t flags; // 1 = valid, 2 = button primary, 4 = button secondary 

        static constexpr uint16_t VALID_FLAG = 0x1;
        static constexpr uint16_t BUTTON_PRIMARY_FLAG = 0x2;
        static constexpr uint16_t BUTTON_SECONDARY_FLAG = 0x4;
        static constexpr int ACCEL_RANGE = 4;
        static constexpr int GYRO_RANGE = 500;

        /// @brief Return the gyro value scaled (in radians)
        /// @param gyro_range should match the gyro range in MCU code
        cv::Vec3d getGyroScaled(int gyro_range = GYRO_RANGE) const;

        /// @brief Return the acceleration value scaled (normalized to 1 = 1 gravity)
        /// @param accel_range should match accel range in MCU code
        cv::Vec3d getAccelScaled(int accel_range = ACCEL_RANGE) const;
        bool isValid() const { return (flags & VALID_FLAG) != 0; }

        bool isPrimaryButtonPressed() const { return (flags & BUTTON_PRIMARY_FLAG) != 0; }

        bool isSecondaryButtonPressed() const { return (flags & BUTTON_SECONDARY_FLAG) != 0; }
    };



    class DeviceScanner
    {
    public:
        DeviceScanner(SimpleBLE::Adapter&& adapter, SimpleBLE::BluetoothUUID service_uuid);
        bool start();
        std::optional<SimpleBLE::Peripheral> getResult();
        void cancel();
        bool running();

    private:
        enum class State { IDLE, SCANNING, FINISHING_SCAN, FINISHED };

        SimpleBLE::Adapter adapter_;
        const SimpleBLE::BluetoothUUID service_uuid_;
        std::mutex data_mutex_;
        
        SimpleBLE::Peripheral peripheral_;
        State state_;

        void onScanFound(SimpleBLE::Peripheral peripheral);
        void onScanStop();
    };
     

    
    class BleReader
    {
    public:

        BleReader(SimpleBLE::BluetoothUUID service_uuid, SimpleBLE::BluetoothUUID characteristic_uuid, std::function<void(PenDataPacket)> on_packet_callback);

        bool start();
        bool stop();
        bool isRunning();
        bool isConnected();

    private:

        void backgroundThread();
        int64_t millis();
        void notifyCallback(SimpleBLE::ByteArray payload);

        std::atomic<bool> thread_stop_request_;
        std::thread background_thread_;

        std::atomic<bool> connected_{false};
        std::optional<SimpleBLE::Peripheral> peripheral_;
        std::unique_ptr<DeviceScanner> device_scanner_;
        bool reader_running_;
        std::atomic<int64_t> last_callback_ms;
        std::function<void(PenDataPacket)> on_packet_callback_;

        const SimpleBLE::BluetoothUUID service_uuid_;
        const SimpleBLE::BluetoothUUID characteristic_uuid_;
    };

} // namespace aergo::pen_tracking