# HMI (Human-Machine Interface) Specification

This document defines the new `HMI` node architecture that will be integrated into the E-Trike CAN standard. This allows physical displays and our CAN Controller UI to natively command the vehicle without resorting to spoofing ECU heartbeats or hacking GPIO pins.

## 1. The HMI Node
- **Name:** `HMI`
- **Location:** Resides virtually in the CAN Controller UI (and eventually in a physical dashboard touchscreen).
- **Bus Target:** High Bus (forwarded to Low Bus by RT), or Both Buses. 

## 2. New CAN Messages: `0x111` and `0x112`
Because these commands dictate the state of the vehicle, they *do* require high priority to ensure they aren't delayed during heavy bus loads. However, to prevent them from causing Priority Inversion (spamming and blocking steering/braking), they must be **separated** and sent at a very slow **periodic rate (1 Hz / 1000ms)**. This guarantees reliable state synchronization without impacting high-frequency 50Hz control loops.

### 2.1 Mode Command (`0x111 HMI_MODE_REQ`)
| ID | Name | DLC | Rate | Sender | Receiver |
|---|---|---|---|---|---|
| `0x111` | `HMI_MODE_REQ` | 2 | 1000ms | `HMI` | `SYS`, `Host` |

**Signals:**
1. **`HMI_ReqMode` (Byte 0):** `0x00` (MANUAL), `0x01` (AUTO), `0x02` (PURE SIM)
2. **`HMI_ModeAlive` (Byte 1):** 0-255 Rolling counter incremented only when transmitting.

### 2.2 Power/Ignition Command (`0x112 HMI_PWR_REQ`)
| ID | Name | DLC | Rate | Sender | Receiver |
|---|---|---|---|---|---|
| `0x112` | `HMI_PWR_REQ` | 2 | 1000ms | `HMI` | `SYS` |

**Signals:**
1. **`HMI_ReqStart` (Byte 0):** `0x00` (Vehicle OFF), `0x01` (Vehicle ON)
2. **`HMI_PwrAlive` (Byte 1):** 0-255 Rolling counter incremented only when transmitting.

## 3. Safety & Architectural Constraints
- **Anti-Spam (Transmission Rate):** These frames must NOT be blasted at high frequencies. The UI will transmit them continuously at **1 Hz (every 1000ms)**. This periodic heartbeat auto-syncs the SYS ECU in case of a reboot and eliminates the need for complex acknowledgment (ACK) logic in the UI, while using negligible bus bandwidth (<0.02%).
- **ESTOP Separation:** Emergency stops are separated entirely from these routine HMI requests. The HMI will directly blast the universal `0x001 SAFETY_ESTOP` frame for emergencies.
- **Physical Override:** Hardwired physical safety switches always override HMI software requests in the SYS state machine.
- **Software Kill-Switch (`ETRIKE_SYS_ENABLE_CAN_HMI`):** The HMI control must be toggleable via software within the SYS ECU (e.g., via `#define ETRIKE_SYS_ENABLE_CAN_HMI` or a runtime variable). When disabled, the SYS ECU completely ignores `0x111` and `0x112` frames and relies only on physical buttons, ensuring safe local bench testing.

## 4. Interaction with SYS ECU
Currently, the SYS ECU reads physical buttons and broadcasts `0x110 Mode Command`. By introducing `0x111` and `0x112`:
- The CAN Controller UI provides a clean "Mode/Power" panel.
- When the user clicks "AUTO", the UI transmits `0x111 HMI_MODE_REQ` with `HMI_ReqMode = 0x01`.
- The SYS ECU (once updated) reads `0x111` and updates its internal state machine exactly as if the physical Mode button was pressed.

## 5. Implementation
This `HMI` node and the `0x111`/`0x112` messages are formally defined in `can_high.yaml` and `can_low.yaml`. 
The entire CAN ecosystem is generated using the unified CLI tool in `protocol/tools/protocol.py`:
- `protocol.py generate dbc` (optional DBC export for third-party tooling)
- `protocol.py generate headers` (C/C++ headers for SYS and RT firmware)
- `protocol.py generate ts` (TypeScript typings for the frontend UI)
- `protocol.py generate docs` (and related doc generators for documentation)
