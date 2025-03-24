#include "logging.h"



#include <simpleble/SimpleBLE.h>
#include <thread>
#include <algorithm>
#include <mutex>



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


   // std::optional<SimpleBLE::Peripheral> peripheral;
   // bool found;
   // std::mutex peripheral_mutex;

   adapter.set_callback_on_scan_found([SERVICE_UUID](SimpleBLE::Peripheral peripheral) {
      std::string identifier = peripheral.identifier();

      std::vector<SimpleBLE::Service> services = peripheral.services();
      // std::find_if(services.begin(), services.end(), [SERVICE_UUID](SimpleBLE::Service& service){
      //    return service.uuid() == SERVICE_UUID
      // })
      for (SimpleBLE::Service& service : services)
      {
         if (service.uuid() == SERVICE_UUID)
         {
            AERGO_LOG("FOUND PEN!")
            std::cout << "Peripheral found: " << (identifier.empty() ? "<NO_NAME>" : identifier) << " (" << peripheral.address() << ")" << std::endl;
         }
      }
   });

   adapter.set_callback_on_scan_updated([](SimpleBLE::Peripheral peripheral) {
      std::string identifier = peripheral.identifier();
      if (peripheral.address() == "c9:4d:c0:e9:04:c5")
      {
         for (auto service : peripheral.services())
      {
         std::cout << "Peripheral UPDATED: " << (identifier.empty() ? "<NO_NAME>" : identifier) << " (" << peripheral.address() << ")" << std::endl;

         AERGO_LOG("\tSERVICE " << service.uuid())
         for (auto ch : service.characteristics())
         {
            AERGO_LOG("\t\tCHARACTERISTIC " << ch.uuid())
            for (auto des : ch.descriptors())
            {
               AERGO_LOG("\t\t\tDESCRIPTOR " << des.uuid())
            }
            if (ch.descriptors().size() == 0)
            {
               AERGO_LOG("\t\t\tNO DESCRIPTOR")
            }
         }
         if (service.characteristics().size() == 0)
         {
            AERGO_LOG("\t\tNO CHARACTERISTIC")
         }
      }
      }
         
   });

   adapter.scan_start();
   std::this_thread::sleep_for(std::chrono::seconds(5));
   adapter.scan_stop();

   return 0;
}