# RT ESP32-S3 Architecture — SUPERSEDED

> **This document has been superseded.** The RT and SYS ESP32-S3 have been merged into a single ESP32-S3. See [[achitecture]] for the unified design.

The original RT ESP32-S3 owned vehicle dynamics: tricycle kinematics, speed PID, steering servo, and obstacle-based speed limiting. It ran 7 FreeRTOS tasks and communicated with the SYS ESP32-S3 over a dedicated UART inter-MCU link.

In the merged design, the RT functions now run on **Core 1** of the single ESP32-S3. The inter-MCU UART link and its protocol (`intermcu_protocol.h`, `intermcu_driver.h`) are eliminated. RT-to-SYS setpoints flow through a direct FreeRTOS `setpoint_queue` instead.

Archived content preserved below for reference.
