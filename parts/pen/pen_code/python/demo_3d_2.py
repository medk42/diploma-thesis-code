import pyqtgraph as pg
import numpy as np
import serial
import keyboard
import sys
import time
from pyqtgraph.Qt import QtCore, QtGui

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


class AccelVisualizer:
    def __init__(self):
        self.app = pg.mkQApp()
        self.widget = pg.PlotWidget()
        self.widget.show()
        self.plot = self.widget.plot(pen='y')
        self.plot2 = self.widget.plot(pen='r')
        self.plot3 = self.widget.plot(pen='g')
        self.view = pg.GLViewWidget()
        self.view.opts['distance'] = 1000
        self.view.opts['fov'] = 60
        self.view.show()
        self.data = np.array([[0, 0, 0]])
        self.scatter = pg.GLScatterPlotItem(pos=self.data)
        self.view.addItem(self.scatter)
        self.box_size = 20
        self.box = np.array([[-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],
                              [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]])
        self.edges = np.array([[0, 1], [1, 2], [2, 3], [3, 0],
                               [4, 5], [5, 6], [6, 7], [7, 4],
                               [0, 4], [1, 5], [2, 6], [3, 7]])
        self.line = pg.GLLinePlotItem(pos=self.box * self.box_size, color=(1, 1, 1, 1), width=2, mode='lines')
        self.view.addItem(self.line)
    def accelUpdate(self, x, y, z):
        self.plot.setData([x])
        self.plot2.setData([y])
        self.plot3.setData([z])
        rot = np.array([[np.cos(y), -np.sin(y), 0],
                        [np.sin(y), np.cos(y), 0],
                        [0, 0, 1]])
        rot2 = np.array([[np.cos(x), 0, np.sin(x)],
                        [0, 1, 0],
                        [-np.sin(x), 0, np.cos(x)]])
        rot3 = np.array([[1, 0, 0],
                        [0, np.cos(z), -np.sin(z)],
                        [0, np.sin(z), np.cos(z)]])
        self.scatter.setData(pos=np.array([[0, 0, 0]]))
        self.box_rotated = np.dot(np.dot(np.dot(self.box, rot), rot2), rot3) * self.box_size
        self.line.setData(pos=self.box_rotated, color=(1, 1, 1, 1), width=2, mode='lines')
        self.app.processEvents()
        

visualizer = AccelVisualizer()
x, y, z = 0, 0, 0

reader = SerialReader('COM11', 9600)

start_time = time.time()

while not keyboard.is_pressed('esc'):
    x, y, z = reader.read_serial()
    visualizer.accelUpdate(x, y, z)
    visualizer.accelUpdate(x/100, y/100, z/100)
    new_time = time.time()
    print(f'runtime: {(new_time - start_time) * 1000:.2f}ms')
    start_time = new_time

reader.stop()
sys.exit()