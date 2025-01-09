import serial
import keyboard
class SerialReader:
    def __init__(self, port, baudrate):
        self.port = port
        self.baudrate = baudrate
        self.serial_port = serial.Serial(self.port, self.baudrate)
    def read_serial(self):
        while True:
            data = self.serial_port.readline().decode().strip()
            try:
                x, y, z = map(float, data[1:-1].split(','))
                return x,y,z
            except ValueError:
                pass

    def stop(self):
        self.serial_port.close()

reader = SerialReader('COM11', 9600)

while not keyboard.is_pressed('esc'):
    x, y, z = reader.read_serial()
    print(f'Accel X: {x}, Y: {y}, Z: {z}')

reader.stop()
