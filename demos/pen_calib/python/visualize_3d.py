import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import numpy as np

# Define your point sets with names
point_sets = [
    {
        'name': '92',
        'points': np.array([
            [-0.0065, 0.0065, 0],
        [0.0065, 0.0065, 0],
        [0.0065, -0.0065, 0],
        [-0.0065, -0.0065, 0]
        ])
    },
    {
        'name': '93',
        'points': np.array([
               [-0.0112844, 0.00705241, -0.0160643],
        [-0.0109948, 0.00687594, -0.00306871],
        [-0.0108209, -0.00612165, -0.00324909],
        [-0.0111105, -0.00594517, -0.0162447]
        ])
    },
    {
        'name': '97',
        'points': np.array([
                    [-0.0140686, 0.0367766, -0.00588565],
        [-0.00462495, 0.0368371, 0.00304825],
        [-0.00447748, 0.0238381, 0.00298043],
        [-0.0139211, 0.0237776, -0.00595347]
        ])
    }
]

# Create figure
fig = plt.figure(figsize=(10, 8))
ax = fig.add_subplot(111, projection='3d')

# Plot each point set
colors = ['r', 'g', 'b']  # Colors for each set

mins = np.ones(3) * 100000
maxs = np.ones(3) * -100000

for i, point_set in enumerate(point_sets):
    # Plot points
    ax.plot(point_set['points'][:, 0], point_set['points'][:, 1], point_set['points'][:, 2],
                c=colors[i])
    ax.scatter(point_set['points'][:, 0], point_set['points'][:, 1], point_set['points'][:, 2],
                c=colors[i], label=point_set['name'])
    ax.scatter(point_set['points'][0, 0], point_set['points'][0, 1], point_set['points'][0, 2],
            c=colors[i], s=60, marker='X')
    
    mins = np.min(np.stack((np.min(point_set['points'], axis=0), mins)), axis=0)
    maxs = np.max(np.stack((np.max(point_set['points'], axis=0), maxs)), axis=0)
    
    # Add text labels for each point
    for j, point in enumerate(point_set['points']):
        ax.text(point[0], point[1], point[2], f"{point_set['name']}\nPoint {j+1}",
                size=8, zorder=1, color=colors[i])

print(mins)
print(maxs)

mids = (maxs + mins) / 2
ranges = maxs - mins
max_range = np.max(ranges)
mins = mids - max_range
maxs = mids + max_range

# Set axis limits and labels
ax.set_xlim(mins[0], maxs[0])
ax.set_ylim(mins[1], maxs[1])
ax.set_zlim(mins[2], maxs[2])
ax.set_xlabel('X')
ax.set_ylabel('Y')
ax.set_zlabel('Z')
ax.set_title('3D Point Sets')
ax.legend()

# Show the plot
plt.show()