# Dummy Robot Standalone Web UI Plan

## Goal
Build a standalone web UI for `robot_module_dummy` that is separate from the main `frontend_module` and is intended for showcase/demo use. The UI should feel like a simple machine-control panel: jog controls, direct move forms, live status, and current position display. Scene visualization is explicitly not required.

## Current State
- `robot_module_dummy` already simulates `moveJ` and `moveL`.
- The module already owns the robot state and status publishing loop.
- There is no dedicated operator UI for the dummy robot.
- The repository already uses Wt in `frontend_module`, so a local embedded Wt server is the lowest-risk web stack already present in the codebase.

## Scope
- Add a standalone HTTP UI hosted by `robot_module_dummy`.
- Keep it showcase-focused and self-contained.
- Do not depend on the main frontend application.
- Reuse the dummy module's internal motion/state logic directly instead of routing the UI through the full module request graph.

## Functional Requirements
- Show robot state:
  - motion status: `IDLE`, `MOVING`, `ERROR`
  - active action id if any
  - current flange/head pose
  - current TCP pose
  - current joint values
  - last error text if available
- Provide controls:
  - direct `moveJ` by entering all 6 joints
  - direct `moveL` by entering TCP pose
  - cancel current move
  - jog joints with configurable step size
  - jog Cartesian XYZ with configurable step size
- Poll/refresh automatically in the page.

## Recommended Architecture
### 1. Host the web server inside `robot_module_dummy`
- Start a lightweight Wt app from the dummy module's `threadStart()`.
- Stop it in `threadStop()`.
- Use a dedicated config file in the dummy module data directory.

Why:
- Single-process deployment.
- No extra IPC protocol.
- Minimal setup for a showcase.

### 2. Add a narrow UI-facing API on the module
Expose simple thread-safe methods on `RobotModuleDummy` such as:
- `getUiSnapshot()`
- `startMoveJointUi(...)`
- `startMoveLinearUi(...)`
- `cancelMoveUi()`

These should lock the same simulation mutex already used by the module and reuse the existing motion setup logic.

### 3. Build the page as a simple Wt widget tree
Suggested layout:
- Header with module title and status badge
- Current state section
- Joint jog section
- Cartesian jog section
- Direct `moveJ` form
- Direct `moveL` form
- Cancel button
- Message/feedback area

Use a timer to refresh status every 200-500 ms.

## UI Units
Use operator-friendly units:
- joints: degrees in the UI, radians internally
- XYZ: millimeters in the UI, meters internally
- RPY: degrees in the UI, radians internally
- speed:
  - `moveJ`: deg/s in the UI, rad/s internally
  - `moveL`: mm/s in the UI, m/s internally

## Data Model For UI Snapshot
Suggested snapshot contents:
- `moving`
- `has_error`
- `error_text`
- `action_id`
- `joint_deg[6]`
- `tfc_xyz_mm[3]`
- `tfc_rpy_deg[3]`
- `tcp_xyz_mm[3]`
- `tcp_rpy_deg[3]`

## Implementation Steps
1. Add dummy-module config parsing for Wt server startup.
2. Add module data files:
   - `config.txt`
   - `wt_config.xml`
   - optional static assets directory
3. Add UI snapshot and UI command helper methods to `RobotModuleDummy`.
4. Add a small Wt application class for the dummy UI.
5. Start/stop the Wt server with the module lifecycle.
6. Add refresh timer and operator controls.
7. Verify that UI actions and background sim thread remain thread-safe.

## Validation
- Start core with `robot_module_dummy`.
- Open the dummy UI in a browser.
- Verify:
  - status updates while moving
  - `moveJ` command moves successfully
  - `moveL` command moves successfully
  - cancel interrupts a running move
  - jog controls produce incremental moves
  - values displayed match published status

## Known Risks
- Wt dependency/linking may require the same package setup used by `frontend_module`.
- UI callbacks must never race the sim loop; all state mutation must stay behind the sim mutex.
- If multiple browser sessions are opened, they should all be treated as views/controllers of the same robot state.

## Prompt Starter
Implement the standalone showcase web UI for `default_modules/robot_module_dummy` using an embedded Wt server inside the module. Keep it separate from `frontend_module`. Add a simple machine-control page with live status, current joint/flange/TCP values, direct moveJ/moveL forms, cancel, joint jogging, and Cartesian XYZ jogging. Use operator-friendly UI units (deg/mm) and convert internally to rad/m. Reuse the dummy module's internal motion logic directly through a small thread-safe UI-facing API. Add config/module-data files needed to launch the local web app with the module lifecycle.
