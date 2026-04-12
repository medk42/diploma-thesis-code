# Dummy Robot Joint/FK/IK Plan

## Goal
Replace the current pose-based dummy robot internals with a joint-based robot model that supports:
- real joint state as the primary state
- forward kinematics for flange and TCP
- full robot visualization
- `moveJ` in joint space
- `moveL` through online replanning with numerical inverse kinematics

The implementation is for a dummy/showcase robot, not a production controller.

## Current State
- The dummy robot currently uses a fake `xyzrpy[6]` state.
- Those 6 values are currently treated both as joint-like values and as flange pose.
- `moveL` is currently done by direct pose interpolation rather than joint-space tracking from IK.
- Visualization only shows world/TFC/TCP axes and a head sphere.

## Target State
- Primary state becomes `joint_positions[N]`.
- Flange and TCP poses are derived from FK.
- Visualization renders the full articulated robot.
- `moveJ`:
  - target is joint space
  - interpolate in joint space
- `moveL`:
  - define next Cartesian target each tick
  - solve IK seeded with current joint state
  - advance robot to the solved joint state
  - stop if solver fails or hard joint stop is reached

## Constraints And Assumptions
- Numerical IK is acceptable.
- Analytic IK is not required.
- Singularity handling is not required.
- Multiple-solution handling is not required.
- Joint limits do not need to participate in planning initially.
- If a joint limit is exceeded or hit, the robot may simply stop/fail.
- Solver seed is always the current joint state, so convergence should usually be local and fast.

## Recommended Architecture
### 1. Introduce a kinematic model
Define:
- link/joint descriptors
- joint axes
- fixed transforms between joints
- tool/TCP offset

This can be:
- a custom dummy 6-DOF arm
- or a simplified borrowed model layout from `robot_module_kassow`

### 2. Make joints the only mutable robot state
Store:
- `joint_positions`
- optional `joint_velocities`
- active motion target and mode

Derived each tick:
- base pose
- flange pose
- TCP pose
- visualization link poses

### 3. Reuse/adapt Kassow visualization approach
Use the same pattern as `robot_module_kassow::robot_vis::RobotVisualization`:
- register link resources
- compute link poses from FK
- update articulated link geometry
- keep TCP/base axes and trajectory

### 4. Add FK
Need:
- homogeneous transform chaining or equivalent SE(3) chain
- flange pose output
- TCP pose output with tool offset
- Jacobian computation support for IK

### 5. Add numerical IK for local incremental solving
Recommended first solver:
- damped least squares / Levenberg-Marquardt style step
- pose error = position error + orientation error in minimal representation
- seed with current joint positions

Typical loop for one control tick:
1. Compute next desired TCP point on the Cartesian path.
2. Keep target orientation fixed or interpolate by quaternion slerp.
3. Run a few IK iterations from current joint state.
4. If converged, accept the solved joints for this tick.
5. If not converged, fail/stop the motion.

### 6. `moveL` online replanning
For each sim tick:
- compute path progress from speed and elapsed time
- generate next desired Cartesian pose
- solve local IK
- update joint state

This is intentionally simple and appropriate for a dummy robot.

## Solver Details
### FK representation
- Use matrix transforms or SE(3) helper structs.
- Store joint axes in parent or local coordinates consistently.

### Orientation error
- Use quaternion or SO(3) log error, not Euler subtraction.
- Keep shortest-path quaternion interpolation for the Cartesian orientation target.

### IK update
Recommended first version:
- numerical Jacobian or analytic geometric Jacobian if convenient
- damped least squares:
  - `dq = J^T (J J^T + lambda^2 I)^-1 e`
- clamp max joint update per iteration
- run a bounded number of iterations per tick

### Stop criteria
- position error below threshold
- orientation error below threshold
- iteration budget exhausted -> fail current `moveL`

## Motion Model
### `moveJ`
- target joints are known directly
- interpolate joints with speed-based progress
- derive flange/TCP from FK

### `moveL`
- target is TCP pose
- path is generated in Cartesian space
- joints are updated by local IK each tick

## Incremental Implementation Sequence
1. Define the dummy robot kinematic chain and tool offset.
2. Replace fake `xyzrpy` state with real joint state.
3. Implement FK and derive flange/TCP from it.
4. Update status publishing to report real joints and FK-derived poses.
5. Add full robot visualization from FK.
6. Keep `moveJ` working in pure joint space.
7. Add a minimal numerical IK solver.
8. Switch `moveL` to online Cartesian target generation + IK per tick.
9. Add optional joint limit stop behavior.

## Validation
- FK sanity:
  - zero joint pose known and visually correct
  - changing one joint moves only downstream links
- `moveJ`:
  - reaches expected joint targets
  - status and visualization match
- `moveL`:
  - TCP follows an approximately straight path
  - seeded local IK converges at each tick for ordinary moves
  - failure path stops safely when solver fails

## Known Risks
- The largest risk is not speed, but solver tuning and coordinate-frame consistency.
- Most bugs will come from:
  - wrong joint axis convention
  - wrong tool offset direction
  - wrong Jacobian frame
  - mixed flange/TCP semantics

## Prompt Starter
Refactor `default_modules/robot_module_dummy` from the current fake pose-based state into a joint-based dummy robot. Add a simple 6-DOF kinematic chain, forward kinematics for flange and TCP, full articulated visualization, and online numerical IK for `moveL`. Keep `moveJ` in joint space. For `moveL`, each tick compute the next Cartesian TCP target on the linear path, seed the IK solver with the current joint state, solve locally, and advance to that joint solution. Use quaternion/SO(3) orientation handling, not Euler subtraction. Do not add advanced singularity or multi-solution handling; this is a dummy/showcase robot.
