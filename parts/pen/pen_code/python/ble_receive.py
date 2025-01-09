import struct
import asyncio
from bleak import BleakClient

# UUIDs for the BLE device
DPOINT_SERVICE_UUID = "19B10010-E8F2-537E-4F6C-D104768A1214"
DPOINT_CHARACTERISTIC_UUID = '14128a76-04d1-6c4f-7e53-f2e81000b119'

async def main():
    # Connect to the BLE device
    async with BleakClient(<what to use here>) as client:
        # Wait for the device to be connected
        await client.connect()

        # Check if the device is connected
        if client.is_connected:
            print("Connected to DPOINT")

            # Start notifications for the characteristic
            def notification_handler(sender, data):
                # Unpack the data
                unpacked_data = struct.unpack("<hhh hhhu", data)
                accel = unpacked_data[:3]
                gyro = unpacked_data[3:6]
                pressure = unpacked_data[6]

                # Print the data
                print(f"Accelerometer: {accel}, Gyroscope: {gyro}, Pressure: {pressure}")

            await client.start_notify(DPOINT_CHARACTERISTIC_UUID, notification_handler)

            # Wait for 10 seconds
            await asyncio.sleep(10)

            # Stop notifications
            await client.stop_notify(DPOINT_CHARACTERISTIC_UUID)
        else:
            print("Failed to connect to DPOINT")

asyncio.run(main())
