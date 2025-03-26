#ifndef AERGO_BLE_READER_H
#define AERGO_BLE_READER_H

#include <atomic>
#include <thread>
#include <mutex>
#include <simpleble/SimpleBLE.h>

namespace aergo::pen_tracking
{
    struct PenDataPacket {
        int16_t accel[3];
        int16_t gyro[3];
        uint16_t flags; // 1 = valid, 2 = button primary, 4 = button secondary
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
        void scanFoundCallback(SimpleBLE::Peripheral peripheral);
        int64_t millis();
        void notifyCallback(SimpleBLE::ByteArray payload);

        std::mutex data_mutex_;
        std::atomic<bool> thread_stop_request_;
        std::atomic<bool> scan_stopped_;
        std::thread background_thread_;

        std::optional<SimpleBLE::Peripheral> peripheral_;
        SimpleBLE::Adapter adapter_;
        bool reader_running_;
        int64_t last_callback_ms;
        std::function<void(PenDataPacket)> on_packet_callback_;

        const SimpleBLE::BluetoothUUID service_uuid_;
        const SimpleBLE::BluetoothUUID characteristic_uuid_;
    };

} // namespace aergo::pen_tracking

#endif // AERGO_BLE_READER_H