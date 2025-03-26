#include "logging.h"
#include "defaults.h"

#include "ble_reader.h"



int main(int argc, char** argv) {
   

    aergo::pen_tracking::BleReader ble_reader(aergo::defaults::pen::SERVICE_UUID, aergo::defaults::pen::CHARACTERISTIC_UUID, [](aergo::pen_tracking::PenDataPacket packet) {
        AERGO_LOG("Flags: " << packet.flags)
    });

    bool res = ble_reader.start();
    AERGO_LOG("Start success: " << res)
    if (!res)
    {
        return 1;
    }

    AERGO_LOG("Press enter to stop")
    std::string inp;
    std::cin >> inp;

    AERGO_LOG("STOPPING...")

    res = ble_reader.stop();
    AERGO_LOG("Stop success: " << res)

   return 0;
}