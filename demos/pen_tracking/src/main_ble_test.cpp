#include "logging.h"
#include "defaults.h"

#include "ble_reader.h"

#include <chrono>
#include <cstdint>

int64_t micros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}


int main(int argc, char** argv) {

    int64_t last_time = micros();

    aergo::pen_tracking::BleReader ble_reader(aergo::defaults::pen::SERVICE_UUID, aergo::defaults::pen::CHARACTERISTIC_UUID, [&last_time](aergo::pen_tracking::PenDataPacket packet) {
        int64_t current_time = micros();
        double freq = 1000000.0 / (current_time - last_time);
        last_time = current_time;
        AERGO_LOG("Flags: " << packet.flags << " Freq: " << freq << " Hz");
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