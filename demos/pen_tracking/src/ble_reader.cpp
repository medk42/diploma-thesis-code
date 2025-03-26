#include "ble_reader.h"
#include <algorithm>

#define CALLBACK_TIMEOUT_MS 500

using namespace std::chrono_literals;
using namespace aergo::pen_tracking;



BleReader::BleReader(SimpleBLE::BluetoothUUID service_uuid, SimpleBLE::BluetoothUUID characteristic_uuid, std::function<void(PenDataPacket)> on_packet_callback) 
: thread_stop_request_(false), reader_running_(false), scan_stopped_(false), last_callback_ms(0), service_uuid_(service_uuid), characteristic_uuid_(characteristic_uuid), on_packet_callback_(on_packet_callback)
{}



bool BleReader::start()
{
    std::lock_guard<std::mutex> lock(data_mutex_);

    if (!SimpleBLE::Adapter::bluetooth_enabled())
    {
        return false;
    }

    std::vector<SimpleBLE::Adapter> adapters = SimpleBLE::Adapter::get_adapters();
    if (adapters.empty())
    {
        return false;
    }

    adapter_ = adapters[0];
    if (!adapter_.initialized())
    {
        return false;
    }
    adapter_.set_callback_on_scan_found([this](SimpleBLE::Peripheral peripheral){ scanFoundCallback(peripheral); });

    peripheral_ = std::nullopt;

    thread_stop_request_ = false;
    background_thread_ = std::thread(&BleReader::backgroundThread, this);

    reader_running_ = true;

    return true;
}

#include "logging.h"

void BleReader::scanFoundCallback(SimpleBLE::Peripheral peripheral)
{
    AERGO_LOG("FOUND DEV: " << peripheral.address())
    std::vector<SimpleBLE::Service> services = peripheral.services();
    auto it = std::find_if(services.begin(), services.end(), [this](SimpleBLE::Service& service){
        return service.uuid() == service_uuid_;
    });
    
    if (it != services.end())
    {
        AERGO_LOG("found pen!")
        std::lock_guard<std::mutex> lock(data_mutex_);
        AERGO_LOG("NOTIFY LOCK")
        peripheral_ = peripheral;
        AERGO_LOG("NOTIFY UNLOCK")
    }
}



bool BleReader::stop()
{
    reader_running_ = false;

    AERGO_LOG("Request thread stop: " << millis())
    thread_stop_request_ = true;
    int64_t start_ms = millis();
    while (millis() < start_ms + 5000 && thread_stop_request_)
    {
        std::this_thread::sleep_for(10ms);
    }


    {
        AERGO_LOG("stop LOCK: " << millis())
        std::lock_guard<std::mutex> lock(data_mutex_);
        AERGO_LOG("Checking for peripheral")
        if (peripheral_ && peripheral_->is_connected())
        {
            AERGO_LOG("Peripheral connected, disconnecting")
            peripheral_->disconnect();
            peripheral_ = std::nullopt;
            AERGO_LOG("Peripheral disconnected")
        }
        AERGO_LOG("stop UNLOCK: " << millis())

        AERGO_LOG("Thread stop check: " << thread_stop_request_)
        AERGO_LOG("adapter scan active: " << (adapter_.initialized() ? adapter_.scan_is_active() : false))

        
        if (adapter_.initialized() && adapter_.scan_is_active())
        {
            AERGO_LOG("Stopping scan")
            scan_stopped_ = false;
            adapter_.set_callback_on_scan_stop([this](){ scan_stopped_ = true; });
            adapter_.scan_stop();
            
            start_ms = millis();
            while (millis() < start_ms + 5000 && !scan_stopped_)
            {
                std::this_thread::sleep_for(10ms);
            }
            AERGO_LOG("Stopped: " << scan_stopped_)

            if (!scan_stopped_)
            {
                return false;
            }
        }
    }

    if (thread_stop_request_)
    {
        return false;
    }
    else if (background_thread_.joinable())
    {
        background_thread_.join();
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
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            AERGO_LOG("THREAD LOCK: " << millis())
            if (!peripheral_)
            {
                if (!adapter_.scan_is_active())
                {
                    AERGO_LOG("Starting scan")
                    adapter_.scan_start();
                    AERGO_LOG("SCAN STARTED")
                }
            }
            else
            {
                if (adapter_.scan_is_active())
                {
                    AERGO_LOG("Stopping scan")
                    adapter_.scan_stop();
                    AERGO_LOG("SCAN STOPPED")
                }

                if (!peripheral_->is_connectable())
                {
                    AERGO_LOG("device not connectable :(")
                    peripheral_ = std::nullopt;
                }
                else if (!peripheral_->is_connected())
                {
                    AERGO_LOG("device not connected, connecting")
                    peripheral_->connect();
                    AERGO_LOG("DEVICE CONNECTED")
                    peripheral_->notify(service_uuid_, characteristic_uuid_, [this](SimpleBLE::ByteArray payload) { notifyCallback(payload); });
                    AERGO_LOG("DEVICE NOTIFY")
                }
                else if (millis() > last_callback_ms + CALLBACK_TIMEOUT_MS)
                {
                    AERGO_LOG("device connected, but failed to provide update, time: " << millis() << "   last: " << last_callback_ms)
                    peripheral_->disconnect();
                    AERGO_LOG("DEVICE DISCONNECTED")
                    peripheral_ = std::nullopt;
                }
            }

            AERGO_LOG("THREAD UNLOCK " << millis())
        }
        std::this_thread::sleep_for(100ms);
     }

     AERGO_LOG("THREAD STOPPED")
     
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
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        AERGO_LOG("NOTIFY2 LOCK")
        last_callback_ms = millis();
        AERGO_LOG("NOTIFY2 UNLOCK")
    }

    if (payload.size() == sizeof(PenDataPacket))
    {
        PenDataPacket pen_data_packet;
        std::memcpy(&pen_data_packet, payload.data(), payload.size());

        on_packet_callback_(pen_data_packet);
    }
}