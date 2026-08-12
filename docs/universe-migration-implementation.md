# Autoware Universe migration implementation record

This document records the implementation completed from
[`update-universe.md`](update-universe.md). The migration is additive: existing CAN
layouts and safety ownership are preserved, while the bridge and the mandatory Phase 2
motion path are extended for the pinned Autoware Universe interfaces.

## Ownership and scope

| Component | Result |
|---|---|
| SYS | Unchanged. SYS remains the authority for physical AUTO/MANUAL mode, safety status, lamps, and ESTOP recovery. |
| MTR | Unchanged. Existing propulsion, gear, feedback, timeout, and ESTOP behavior remains in force. |
| RT | Added direct steering-angle arbitration and coherent motion reporting; existing PID, encoder, resolver, diagnostic, bypass, and gateway features were retained. |
| Jetson bridge | Ported to Universe messages/services, corrected signs and gears, added lifecycle handling, mode/engage/emergency gates, and feedback conversion. |
| Protocol | Added `0x303 HOST_STEER_CMD` and `0x121 RT_MOTION_RPT`; regenerated all artifacts and vectors. |
| Tools and tests | Updated control-toolkit, simulation, native RT tests, debug-tool, and vt-console for both new frames. |

HMI and Autoware/Jetson may both issue the existing `0x111` mode request. The bridge does
not become the mode authority; it sends the request and reports mode only after RT/SYS
feedback confirms it.

## Phase 2 CAN additions

### `0x303 HOST_STEER_CMD` (Host → RT, 10 ms)

- Signed steering angle in 0.1-degree units, limited to ±45 degrees.
- `angle_valid` flag, reserved bits fixed at zero, and an independent rolling counter.
- RT uses a fresh valid frame as the authoritative steering target, including at zero speed.
- A stale or invalid direct-angle command falls back to the existing `0x300` path.

### `0x121 RT_MOTION_RPT` (RT → Host, 10 ms)

- Measured speed, estimated yaw rate, physical gear, three validity flags, reserved bits,
  and a rolling counter.
- RT computes yaw from measured speed and fresh steering angle.
- The bridge uses this coherent report for Universe velocity, heading-rate, and gear status.

The bridge still emits legacy `0x300` yaw for compatibility, with a symmetric
`abs(speed) < 0.05 m/s` guard. The guard applies only to derived yaw; `0x303` remains valid
at standstill. Universe positive steering is left, while the trike wire convention is
right-positive, so the bridge flips the sign in both directions.

## Bridge safety behavior

Motion transmission requires all of the following:

- Autoware engage is enabled.
- RT feedback confirms AUTO and a normal safety state.
- SYS safety status is fresh, healthy, and not in ESTOP.
- RT heartbeat is present and advancing.
- RT state and valid `0x121` motion reports are fresh.
- A fresh control command is available.

Loss of any gate sends neutral drive plus invalid steering and clears the cached control
command. Recovery requires a fresh post-fault control command. Software emergency input
sends the existing stop frame and cannot clear a physical SYS ESTOP. PARK is represented by
neutral gear plus the brake-hold command.

The bridge uses the pinned Universe topics and service, including durable depth-one QoS for
control, engage, and emergency inputs. Lifecycle launch configures and activates the node.

## ROS/package additions

- `autoware_vehicle_bridge`: bridge executable, parameters, conversion tests, and launch.
- `etrike_protocol`: installed generated C++ protocol headers for ROS consumers.
- `etrike_vehicle_launch`: vehicle-interface launch wrapper.
- `etrike_vehicle_description`: initial Xacro and vehicle parameters for the trike.

## Verification completed

- Protocol validation: 34 messages and 44 instances; generated artifacts are current.
- C++ protocol compatibility and generated-vector tests pass with GCC 10.3.
- RT native tests: router 25/25, dispatch 26/26, Phase 2 motion 16/16.
- Bridge conversion tests cover zero, ±0.049, ±0.05, and ±0.051 m/s, sign conversion,
  and both generated Phase 2 frames.
- Control-toolkit: 18 tests passed.
- Simulation: 46 targeted tests passed.
- Debug-tool: shared 100 tests and physics 6 tests passed.
- vt-console Phase 2 tests: 8 passed.
- Launch Python and XML/Xacro syntax checks passed.
- Pinned Autoware Universe message, service, topic, and launch definitions were checked in
  `E:\work\av_project\autoware\src`.

The local machine does not provide ROS 2/`colcon`, SocketCAN `vcan`, or the physical trike,
so ROS build, bus integration, and staged hardware validation remain deployment steps. The
existing unrelated vt-console heartbeat assertions remain stale against the current
canonical heartbeat contract and were not changed as part of this migration.

## Implementation commits

The implementation was committed incrementally. The main sequence begins with
`a0584f6 docs: finalize Autoware Universe migration plan` and ends with
`b4509b1 docs: align feedback mapping with Phase 2`; the final bridge safety and QoS fixes
are in `b6c0465`, `bc59757`, `a3574c0`, and `ac4ab55`.
