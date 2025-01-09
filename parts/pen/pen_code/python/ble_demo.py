import asyncio
import bleak
import struct
import time

PEN_SERVICE_ID = '2bfae565-df4e-45b6-b1fa-a6f75c1be2b3'

PEN_CHARACTERISTIC_UUID = 'e76d106d-a549-4b3a-afbd-8879582943fe'


import struct

last_notify = time.time()

def notification_handler(sender, data):
    global last_notify
    current_time = time.time()
    time_diff = current_time - last_notify
    last_notify = current_time

    # Define the correct format string
    # '<' for little-endian, '3h' for 3 int16_t, '3h' for 3 int16_t, 'H' for uint16_t
    format_string = "<3h3hH"

    print(len(data), data)
    # Unpack the data
    unpacked_data = struct.unpack(format_string, data)

    # Extract the values
    accel = unpacked_data[:3]
    gyro = unpacked_data[3:6]
    flags = unpacked_data[6]

    # Print the data
    print(f"Time diff: {time_diff * 1000:.1f}ms, Accelerometer: {accel}, Gyroscope: {gyro}, Flags: {flags}")

async def communicate(client):
    if client.is_connected:
        print('Connected')

        services = await client.get_services()
        for service in services:
            print(f"Service: {service.uuid}")
            for characteristic in service.characteristics:
                print(f"  Characteristic: {characteristic.uuid} ({characteristic.properties})")
        await client.start_notify(PEN_CHARACTERISTIC_UUID, notification_handler)
        await asyncio.sleep(1)
        await client.stop_notify(PEN_CHARACTERISTIC_UUID)
        print("Disconnected")
    else:
        print('Failed to connect')

async def main():
    # Create a BleakScanner
    scanner = bleak.BleakScanner()

    # Start scanning for devices
    print("Scanning for devices...")
    devices = await scanner.discover()

    # Print the discovered devices
    for i, device in enumerate(devices):
        print(f"Device {i+1}: {device.name}, Address: {device.address}")

    # Use the address of the device you want to connect to
    if devices:
        found_device = None
        for device in devices:
            if len(device.metadata['uuids']) > 0 and device.metadata['uuids'][0] == PEN_SERVICE_ID:
                found_device = device
        if found_device:
            print(f"Connecting to device: {found_device.address}")

            async with bleak.BleakClient(found_device) as client:
                await communicate(client)
        else:
            print('No "DPOINT" device found.')
    else:
        print("No devices found")

# Run the main function
asyncio.run(main())