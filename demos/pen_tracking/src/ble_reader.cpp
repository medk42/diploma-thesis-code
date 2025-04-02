#include "ble_reader.h"
#include <algorithm>

#define CALLBACK_TIMEOUT_MS 500

using namespace std::chrono_literals;
using namespace aergo::pen_tracking;



cv::Vec3d PenDataPacket::getGyroScaled(int gyro_range) const
{
    cv::Vec3d gyro_data((double)gyro[0], (double)gyro[1], (double)gyro[2]);
    return gyro_data * 4.375 * (gyro_range / 125.0) / 1000.0;
}



cv::Vec3d PenDataPacket::getAccelScaled(int accel_range) const
{
    cv::Vec3d accel_data((double)accel[0], (double)accel[1], (double)accel[2]);
    return accel_data * 0.061 * (accel_range / 2.0) / 1000.0;
}



DeviceScanner::DeviceScanner(SimpleBLE::Adapter&& adapter, SimpleBLE::BluetoothUUID service_uuid)
: adapter_(std::move(adapter)), service_uuid_(service_uuid), state_(State::IDLE) {}



bool DeviceScanner::start()
{
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (state_ != State::IDLE)
        {
            return false;
        }
    }

    if (!adapter_.bluetooth_enabled() || !adapter_.initialized())
    {
        return false;
    }

    state_ = State::SCANNING;
    adapter_.set_callback_on_scan_found([this](SimpleBLE::Peripheral peripheral) { onScanFound(peripheral); });
    adapter_.set_callback_on_scan_stop([this]() { onScanStop(); });
    adapter_.scan_start();

    return true;
}



bool DeviceScanner::running()
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return state_ != State::IDLE;
}



std::optional<SimpleBLE::Peripheral> DeviceScanner::getResult()
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (state_ != State::FINISHED)
    {
        return std::nullopt;
    }
    else
    {
        state_ = State::IDLE;
        return peripheral_;
    }
}



void DeviceScanner::cancel()
{
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            switch (state_)
            {
                case State::SCANNING:
                    state_ = State::FINISHING_SCAN;
                    adapter_.scan_stop();
                    break;
                case State::FINISHED:
                case State::IDLE:
                    state_ = State::IDLE;
                    return;
            }
        }

        std::this_thread::sleep_for(10ms);
    }
}



void DeviceScanner::onScanFound(SimpleBLE::Peripheral peripheral)
{
    std::vector<SimpleBLE::Service> services = peripheral.services();
    auto it = std::find_if(services.begin(), services.end(), [this](SimpleBLE::Service& service){
        return service.uuid() == service_uuid_;
    });

    if (it != services.end())
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (state_ == State::SCANNING)
        {
            peripheral_ = peripheral;
            state_ = State::FINISHING_SCAN;
            adapter_.scan_stop();
        }   
    }
}



void DeviceScanner::onScanStop()
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    state_ = State::FINISHED;
}



BleReader::BleReader(SimpleBLE::BluetoothUUID service_uuid, SimpleBLE::BluetoothUUID characteristic_uuid, std::function<void(PenDataPacket)> on_packet_callback) 
: thread_stop_request_(false), reader_running_(false), last_callback_ms(0), service_uuid_(service_uuid), characteristic_uuid_(characteristic_uuid), on_packet_callback_(on_packet_callback)
{}



bool BleReader::start()
{
    if (reader_running_)
    {
        return false;
    }

    if (!SimpleBLE::Adapter::bluetooth_enabled())
    {
        return false;
    }

    std::vector<SimpleBLE::Adapter> adapters = SimpleBLE::Adapter::get_adapters();
    if (adapters.empty())
    {
        return false;
    }

    SimpleBLE::Adapter adapter = adapters[0];
    if (!adapter.initialized())
    {
        return false;
    }
    device_scanner_ = std::make_unique<DeviceScanner>(std::move(adapter), service_uuid_);
    peripheral_ = std::nullopt;

    thread_stop_request_ = false;
    background_thread_ = std::thread(&BleReader::backgroundThread, this);

    reader_running_ = true;

    return true;
}



bool BleReader::stop()
{
    if (!reader_running_)
    {
        return false;
    }



    thread_stop_request_ = true;
    int64_t start_ms = millis();
    while (millis() < start_ms + 5000 && thread_stop_request_)
    {
        std::this_thread::sleep_for(10ms);
    }
    if (!thread_stop_request_ && background_thread_.joinable())
    {
        background_thread_.join();
    }



    if (peripheral_ && peripheral_->is_connected())
    {
        peripheral_->disconnect();
        peripheral_ = std::nullopt;
    }



    device_scanner_->cancel();
    device_scanner_ = nullptr;



    reader_running_ = false;



    if (thread_stop_request_)
    {
        return false;
    }

    return true;
}



bool BleReader::isRunning()
{
    return reader_running_;
}



void BleReader::backgroundThread()
{
     while (!thread_stop_request_)
     {
        if (!peripheral_)
        {
            if (!device_scanner_->running())
            {
                device_scanner_->start();
            }
            else
            {
                peripheral_ = device_scanner_->getResult();
            }
        }
        else if (!peripheral_->is_connectable())
        {
            peripheral_ = std::nullopt;
        }
        else if (!peripheral_->is_connected())
        {
            peripheral_->connect();
            if (!peripheral_->is_connected())
            {
                peripheral_ = std::nullopt;
            }
            peripheral_->notify(service_uuid_, characteristic_uuid_, [this](SimpleBLE::ByteArray payload) { notifyCallback(payload); });
        }
        else if (millis() > last_callback_ms + CALLBACK_TIMEOUT_MS)
        {
            peripheral_->disconnect();
            peripheral_ = std::nullopt;
        }
        
        std::this_thread::sleep_for(100ms);
     }

     
     thread_stop_request_ = false;
}



int64_t BleReader::millis()
{
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto value = now_ms.time_since_epoch();
    return value.count();
}



void BleReader::notifyCallback(SimpleBLE::ByteArray payload)
{
    last_callback_ms = millis();

    if (payload.size() == sizeof(PenDataPacket))
    {
        PenDataPacket pen_data_packet;
        std::memcpy(&pen_data_packet, payload.data(), payload.size());

        on_packet_callback_(pen_data_packet);
    }
}