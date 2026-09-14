# Hardware Bench Setup

## Essential Pin Connections
To boot cleanly without faults in `hardware_bench` mode (Mode 1), wire the following:

* **SYS GPIO 1 (ESTOP):** Connect to **3.3V** to clear the ESTOP fault.
* **SYS GPIO 42 (Bypass):** Connect to **GND** to bypass peer/sync safety checks.
* **RT GPIO 42 (Bypass):** Connect to **GND** to bypass peer/sync safety checks.
* **SYS GPIO 2 (Brake):** Leave **floating** (internal pull-up keeps it unpressed).
* **SYS Buttons (GPIO 11, 41, 9, 6):** Leave **floating** (internal pull-ups enabled).

## Build and Upload Commands
Run these commands from within the respective `sys-esp32` or `rt-esp32` directory:

```bash
# Build and upload (auto-detects port)
pio run -e hardware_bench -t upload

# Upload to specific port
pio run -e hardware_bench -t upload --upload-port COM6

# Build only (no upload)
pio run -e hardware_bench
```
