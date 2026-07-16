# Shared vs Protocol Folders Architecture

In the e-trike monorepo architecture, the codebase is split among multiple independent microcontrollers (e.g., `rt-esp32`, `sys-esp32`, `mtr-stm32`). To maintain a single source of truth without duplicating code, we extract common definitions into external directories. 

Recently, the architecture was split into two distinct folders for shared logic: **`protocol/`** and **`shared/`**.

## 1. The `protocol/` Folder (Communication Grammar)
**Purpose**: Defines the CAN matrix, payload structures, and the exact language the ECUs use to communicate over the CAN bus.

The `protocol/` folder acts as the sole source of truth for all CAN communications. It is generally driven by a DBC file or Python matrix generator that automatically generates C/C++ bindings.

**What belongs here**:
- Python CAN matrix definitions.
- Generated C/C++ struct definitions for CAN payloads.
- Message IDs (e.g., `0x205` for Brake Command).
- Enum definitions strictly related to the network (e.g., `Gear::Drive`, `Mode::Autonomous`).
- Encode/Decode mapping functions.

**Why it's separate**: 
Protocol files are often generated automatically from higher-level definitions (like Python or DBC files). By keeping them in their own directory, it is easy to re-generate the C++ headers without risking the deletion or modification of manually written application constants.

## 2. The `shared/` Folder (Physics & Application Logic)
**Purpose**: Defines physical constants, system boundaries, and safety configurations that all ECUs must agree upon to function safely. 

While the protocol defines *how* the ECUs talk, the `shared/` folder defines the *laws of physics and safety* they must obey.

**What belongs here**:
- **Vehicle Geometry**: `kWheelbaseMM`, steering ratios.
- **Safety Limits**: `kMaxSpeedFwdMmps`, `kMaxBrakeKpa`, speed thresholds.
- **System Timeouts**: `kHostCmdStaleTimeoutMs`, Heartbeat intervals.
- **Run Modes**: Definitions like `SYSTEM_RUN_MODE` to uniformly bypass hardware checks across all ECUs during HIL (Hardware-In-the-Loop) or bench testing.

**Why it's separate**:
These constants are manually authored by engineers and govern the physical and safety-critical tuning of the trike. If `sys-esp32` and `rt-esp32` used different speed limit constants, the system could enter a conflicting safety state. Placing them in `shared/` ensures 100% uniformity across all independent microcontrollers.

---

## How it works under the hood
Since the ECUs do not share a file system or memory, these folders act as **Header-Only Shared Libraries**. 

1. There are no `.cpp` files to link. Everything is `constexpr`, `inline`, or macros.
2. In each project's `platformio.ini` (or CMakeLists), we simply add an include path pointing to these external directories.
3. At compile time, the compiler resolves `#include "shared_config.h"` by looking at the `../shared` directory and bakes the exact same constants into both the `rt-esp32` and `sys-esp32` firmware binaries independently.
