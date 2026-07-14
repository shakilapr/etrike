# Mode-Gated Control and EGAS 3-Level Safety

In complex vehicle systems, especially those supporting both manual inputs and autonomous commands, managing "who is in control" and "how do we stop safely" is critical. The E-Trike architecture employs two related concepts: **Mode-Gated Control** and **EGAS 3-level safety**.

---

## 1. Mode-Gated Dual Control

The vehicle operates in distinct modes, usually managed by a centralized **Mode and Safety Authority** (in this project, the SYS ECU). All other nodes on the network follow the Mode Authority's broadcasted state.

### The Three States
1. **MANUAL:** The rider has full control via physical inputs (throttle, brake lever, steering wheel). Autonomous commands are ignored.
2. **AUTO:** An autonomous computer (e.g., Jetson Orin) drives the vehicle via the real-time (RT) ECU.
3. **ESTOP:** An emergency safety state overlaid on the current mode. Actuators transition to a safe state (e.g., maximum braking, steering ramps to zero, motor power cut).

### Why "Mode-Gated"?
Actuators do not inherently know what mode the system is in. The ECUs act as "gates":
- In **MANUAL**, the SYS ECU reads the physical brake lever and sends the command to the brake actuator. The RT ECU suppresses autonomous commands.
- In **AUTO**, the RT ECU calculates kinematics from Jetson commands and sends them to the actuators. The SYS ECU suppresses manual commands to avoid conflicting signals (dual-sender collision).

This setup prevents a single MCU failure from taking out both actuators and enforces a clear chain of command based on the operating mode.

---

## 2. EGAS 3-Level Motor Safety Concept

The EGAS (Electronic Gas) concept is an industry-standard safety architecture originally developed for drive-by-wire throttles. It ensures that unintended acceleration cannot happen due to a single point of failure (like a frozen microcontroller or stuck ADC).

The architecture uses three independent monitoring levels:

### Level 1: Function Controller
- **Who:** The Motor ECU (MTR).
- **What it does:** Reads the intended speed setpoint (from CAN or analog throttle) and executes the motor control loop. 
- **Failure handled:** Basic hardware bounds (e.g., ensuring speed doesn't exceed mechanical limits).

### Level 2: Function Monitor
- **Who:** The System/Safety ECU (SYS).
- **What it does:** Monitors the commanded setpoint (Level 1's input) against the actual vehicle speed or motor feedback. If the discrepancy is too large for too long (e.g., commanded 0 but moving at 20 km/h), Level 2 triggers an ESTOP.
- **Failure handled:** Level 1 software crash, frozen control loop, or corrupt CAN command.

### Level 3: Hardware Monitor
- **Who:** Hardware Watchdogs and Physical Wiring.
- **What it does:** A physical ESTOP button cuts power or a hardwired signal independent of microcontrollers. External watchdog timers (like the TPS3850) require the software to "feed" them regularly. If a task stalls, the watchdog resets the ECU or directly triggers a hardware kill state.
- **Failure handled:** Total MCU freeze, complete software crash, or power failure.

By combining Mode-Gated Control with EGAS 3-level safety, the vehicle guarantees that no single software bug or frozen task can result in uncontrolled movement.
