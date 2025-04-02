#ifndef AERGO_BLE_READER_H
#define AERGO_BLE_READER_H

#include <atomic>
#include <thread>
#include <mutex>
#include <optional>
#include <memory>
#include <simpleble/SimpleBLE.h>
#include <opencv2/opencv.hpp>

namespace aergo::pen_tracking
{
    struct PenDataPacket {
        int16_t accel[3];
        int16_t gyro[3];
        uint16_t flags; // 1 = valid, 2 = button primary, 4 = button secondary


        /// @brief Return the gyro value scaled (in radians)
        /// @param gyro_range should match the gyro range in MCU code
        cv::Vec3d getGyroScaled(int gyro_range) const;

        /// @brief Return the acceleration value scaled (normalized to 1 = 1 gravity)
        /// @param accel_range should match accel range in MCU code
        cv::Vec3d getAccelScaled(int accel_range) const;
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

    private:

        void backgroundThread();
        int64_t millis();
        void notifyCallback(SimpleBLE::ByteArray payload);

        std::atomic<bool> thread_stop_request_;
        std::thread background_thread_;

        std::optional<SimpleBLE::Peripheral> peripheral_;
        std::unique_ptr<DeviceScanner> device_scanner_;
        bool reader_running_;
        std::atomic<int64_t> last_callback_ms;
        std::function<void(PenDataPacket)> on_packet_callback_;

        const SimpleBLE::BluetoothUUID service_uuid_;
        const SimpleBLE::BluetoothUUID characteristic_uuid_;
    };

} // namespace aergo::pen_tracking

#endif // AERGO_BLE_READER_H