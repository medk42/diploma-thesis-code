#include "logging.h"



#include <simpleble/SimpleBLE.h>
#include <thread>
#include <algorithm>
#include <mutex>
#include <chrono>

int64_t millis()
{
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto value = now_ms.time_since_epoch();
    return value.count();
}


struct IMUDataPacket {
   int16_t accel[3];
   int16_t gyro[3];
   uint16_t flags; // 1 = valid, 2 = button primary, 4 = button secondary
 };


int main(int argc, char** argv) {
   if (!SimpleBLE::Adapter::bluetooth_enabled()) {
      AERGO_LOG("Bluetooth is not enabled");
      return 1;
   }

   auto adapters = SimpleBLE::Adapter::get_adapters();
   if (adapters.empty()) {
      AERGO_LOG("No Bluetooth adapters found");
      return 1;
   }

   auto adapter = adapters[0];

   AERGO_LOG("Adapter identifier: " << adapter.identifier());
   AERGO_LOG("Adapter address: " << adapter.address());


   const std::string SERVICE_UUID = "2bfae565-df4e-45b6-b1fa-a6f75c1be2b3";
   const std::string CHARACTERISTIC_UUID = "e76d106d-a549-4b3a-afbd-8879582943fe";
   const int SCAN_TIME_MS = 10000;


   SimpleBLE::Peripheral peripheral;
   bool found = false;
   std::mutex peripheral_mutex;
   bool stopped = false;

   adapter.set_callback_on_scan_found([SERVICE_UUID, &found, &peripheral, &peripheral_mutex](SimpleBLE::Peripheral p) {
      std::vector<SimpleBLE::Service> services = p.services();
      auto it = std::find_if(services.begin(), services.end(), [SERVICE_UUID](SimpleBLE::Service& service){
         return service.uuid() == SERVICE_UUID;
      });
      
      if (it != services.end())
      {
         std::lock_guard<std::mutex> lock(peripheral_mutex);
         peripheral = p;
         found = true;
      }
   });

   adapter.set_callback_on_scan_stop([&stopped, &peripheral_mutex](){
      std::lock_guard<std::mutex> lock(peripheral_mutex);
      stopped = true;
   });


   adapter.scan_start();

   bool success = false;
   int64_t start_time = millis();
   while (millis() < start_time + SCAN_TIME_MS)
   {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      {
         std::lock_guard<std::mutex> lock(peripheral_mutex);
         if (found)
         {
            success = true;
            break;
         }
      }
   }

   adapter.scan_stop();
   while (true)
   {
      {
         std::lock_guard<std::mutex> lock(peripheral_mutex);
         if (stopped)
         {
            break;
         }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }

   if (!success)
   {
      AERGO_LOG("Scan failed to find the \"3D Pen\" device.")
      return 1;
   }

   AERGO_LOG("Successfully got the \"3D Pen\" device, connecting...")

   peripheral.connect();

   if (!peripheral.is_connected())
   {
      AERGO_LOG("Failed to connect");
      return 1;
   }

   AERGO_LOG("Connected!")

   int64_t last_time_ms = millis();
   peripheral.notify(SERVICE_UUID, CHARACTERISTIC_UUID, [&last_time_ms](SimpleBLE::ByteArray payload) {
      if (payload.size() != sizeof(IMUDataPacket))
      {
         AERGO_LOG("WARN: expected size " << sizeof(IMUDataPacket) << ", actual size " << payload.size())
      }
      IMUDataPacket imu_data_packet;
      std::memcpy(&imu_data_packet, payload.data(), payload.size());

      int64_t curr_ms = millis();
      double freq = 1000.0 / (curr_ms - last_time_ms);
      last_time_ms = curr_ms;

      // AERGO_LOG("IMU Data: Accel: " << imu_data_packet.accel[0] << ", " << imu_data_packet.accel[1] << ", " << imu_data_packet.accel[2] 
      //    << "\nGyro: " << imu_data_packet.gyro[0] << ", " << imu_data_packet.gyro[1] << ", " << imu_data_packet.gyro[2] 
      //    << "\nFlags: " << imu_data_packet.flags)
      AERGO_LOG("Frequency: " << freq << "Hz\n")
   });

   std::this_thread::sleep_for(std::chrono::seconds(10));

   return 0;
}