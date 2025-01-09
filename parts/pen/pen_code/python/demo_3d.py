import numpy as np
import open3d as o3d
import time
import copy  # Import the copy module
import serial

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

# Define the vertices and triangles of the box
vertices = np.array([
    [-0.5, -0.5, -0.5],  # 0
    [0.5, -0.5, -0.5],   # 1
    [0.5, 0.5, -0.5],    # 2
    [-0.5, 0.5, -0.5],  # 3
    [-0.5, -0.5, 5.0],   # 4
    [0.5, -0.5, 5.0],    # 5
    [0.5, 0.5, 5.0],     # 6
    [-0.5, 0.5, 5.0]     # 7
])

# Define the triangles of the box
triangles = np.array([
    [0, 1, 2], [0, 2, 3],  # bottom face
    [4, 5, 6], [4, 6, 7],  # top face
    [0, 4, 5], [0, 5, 1],  # front face
    [2, 6, 7], [2, 7, 3],  # back face
    [0, 4, 7], [0, 7, 3],  # left face
    [1, 5, 6], [1, 6, 2]   # right face
])

# Create a mesh from the vertices and triangles
mesh = o3d.geometry.TriangleMesh(
    o3d.utility.Vector3dVector(vertices),
    o3d.utility.Vector3iVector(triangles)
)

# Set up the visualization
vis = o3d.visualization.Visualizer()
vis.create_window(window_name='Animated 3D Waves', width=800, height=600)

rendering_options = vis.get_render_option()
rendering_options.mesh_show_back_face = True

# Function to update Z values based on the sine and cosine waves
def align_mesh(original_mesh, direction):
    # Normalize the direction vector
    direction = np.array(direction)
    direction_norm = direction / np.linalg.norm(direction)

    # Define the reference vector (original orientation of the mesh)
    reference = np.array([0, 0, 1])  # Assuming the mesh is aligned with the Z-axis by default

    # Compute the rotation axis (cross product) and angle
    axis = np.cross(reference, direction_norm)
    angle = np.arccos(np.dot(reference, direction_norm))

    # Handle the edge case where direction is parallel to reference
    if np.linalg.norm(axis) < 1e-6:  # Avoid numerical instability
        rotation_matrix = np.eye(3)
    else:
        axis = axis / np.linalg.norm(axis)
        rotation_matrix = o3d.geometry.get_rotation_matrix_from_axis_angle(axis * angle)

    # Reset the mesh to its original state
    mesh = copy.deepcopy(original_mesh)

    # Apply the rotation
    mesh.rotate(rotation_matrix, center=mesh.get_center())

    return mesh

vis.add_geometry(mesh)

axis = o3d.geometry.TriangleMesh.create_coordinate_frame(size=10)
vis.add_geometry(axis)


reader = SerialReader('COM11', 9600)

start_time = time.time()

# Main loop to animate the waves
try:
    while True:
        import random

        x, y, z = reader.read_serial()

        updated_mesh = align_mesh(mesh, [x,y,z])  # Update wave with current offset
        vis.clear_geometries()
        vis.add_geometry(updated_mesh)
        vis.add_geometry(axis)
        
        vis.poll_events()  # Poll for events
        vis.update_renderer()  # Update the renderer
except KeyboardInterrupt:
    pass
finally:
    vis.destroy_window()  # Close the window on exit
