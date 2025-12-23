import asyncio
import struct
import time

from bleak import BleakClient, BleakScanner

PEN_SERVICE_ID = "2bfae565-df4e-45b6-b1fa-a6f75c1be2b3"
PEN_CHARACTERISTIC_UUID = "e76d106d-a549-4b3a-afbd-8879582943fe"

FLAG_VALID = 1
FLAG_BUT_PRIM_PRESSED = 2
FLAG_BUT_SEC_PRESSED = 4

last_notify = time.time()
but_prim_last = False
but_sec_last = False


def notification_handler(sender: int, data: bytearray):
    global last_notify, but_prim_last, but_sec_last

    now = time.time()
    dt = now - last_notify
    last_notify = now

    accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z, flags = struct.unpack("<3h3hH", data)

    if not (flags & FLAG_VALID):
        return

    but_prim_pressed = bool(flags & FLAG_BUT_PRIM_PRESSED)
    but_sec_pressed = bool(flags & FLAG_BUT_SEC_PRESSED)

    if but_prim_pressed and not but_prim_last:
        print("Primary button: PRESSED")
    if not but_prim_pressed and but_prim_last:
        print("Primary button: RELEASED")
    if but_sec_pressed and not but_sec_last:
        print("Secondary button: PRESSED")
    if not but_sec_pressed and but_sec_last:
        print("Secondary button: RELEASED")

    but_prim_last = but_prim_pressed
    but_sec_last = but_sec_pressed

    accel = (accel_x, accel_y, accel_z)
    gyro = (gyro_x, gyro_y, gyro_z)
    print(f"Time diff: {dt * 1000:.1f}ms, Accelerometer: {accel}, Gyroscope: {gyro}, Flags: {flags}")


async def print_services(client: BleakClient):
    # Bleak 2.x: services are available via client.services after connect
    services = getattr(client, "services", None)

    # Bleak 1.x fallback
    if services is None and hasattr(client, "get_services"):
        services = await client.get_services()

    if not services:
        print("(No services available)")
        return

    for service in services:
        print(f"Service: {service.uuid}")
        for ch in service.characteristics:
            print(f"  Characteristic: {ch.uuid} ({ch.properties})")


async def communicate(client: BleakClient):
    print("Connected:", client.is_connected)

    await print_services(client)

    await client.start_notify(PEN_CHARACTERISTIC_UUID, notification_handler)
    try:
        await asyncio.sleep(10)
    finally:
        await client.stop_notify(PEN_CHARACTERISTIC_UUID)
        print("Disconnected")


async def find_pen(timeout: float = 5.0):
    dev = await BleakScanner.find_device_by_name("3D Pen", timeout=timeout)
    if dev is not None:
        return dev

    results = await BleakScanner.discover(timeout=timeout, return_adv=True)
    target = PEN_SERVICE_ID.lower()

    for dev, adv in results.values():  # address -> (BLEDevice, AdvertisementData)
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        name = dev.name or adv.local_name or ""
        if target in uuids or name == "3D Pen":
            return dev

    return None


async def main():
    dev = await find_pen(timeout=5.0)
    print("Selected device:", dev)

    if dev is None:
        print('No "3D Pen" device found.')
        return

    print(f"Connecting to device: {dev.address}")
    async with BleakClient(dev) as client:
        await communicate(client)


if __name__ == "__main__":
    asyncio.run(main())
