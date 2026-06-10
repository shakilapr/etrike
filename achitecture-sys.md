# SYS ESP32-S3 Architecture — SUPERSEDED

> **This document has been superseded.** The RT and SYS ESP32-S3 have been merged into a single ESP32-S3. See [[achitecture]] for the unified design.

The original SYS ESP32-S3 owned safety and actuation: E-stop monitoring, brake control, motor PWM, manual throttle ADC, mode switching, heartbeat watchdog, and system diagnostics. It ran 10 FreeRTOS tasks and communicated with the RT ESP32-S3 over a dedicated UART inter-MCU link.

In the merged design, the SYS functions now run on **Core 0** of the single ESP32-S3 (safety + CAN I/O) and **Core 1** (motor, brake, throttle actuation). The inter-MCU UART link and its protocol are eliminated. Safety-critical tasks are pinned to Core 0 at highest priority.

Archived content preserved below for reference.
