# Welcome to the ETrike Wiki!

Welcome to the central repository for ETrike's technical notes, architectural decisions, and hardware references. This wiki is designed to help you understand the systems running the ETrike project, from CAN bus design to distributed safety patterns.

Please use the sidebar on the right to navigate through the topics:

*   **Hardware & Microcontrollers**: Reference materials for the ESP32-S3 and analog interfacing basics.
*   **CAN Bus**: Deep dive into the CAN protocol, physical implementation, our gateway design, and troubleshooting guides.
*   **Control & Safety**: Details about PID control, the atomic sensor pipeline, mode-gated control, and distributed safety patterns to ensure functional safety on the trike.
*   **Software Architecture & RTOS**: Insights into state machine design, RTOS task foundations, and endianness considerations in binary protocols.
*   **Protocol Design**: The strategies and specifications for defining contract-based protocol ownership and message payloads.
*   **Testing & Validation**: Overview of our testing architecture and approaches like HIL (Hardware-In-the-Loop) simulations.
*   **Workflow & Project Structure**: How we handle shared code in our monorepo and general Git workflows.

*For any updates to these pages, please submit your changes through the main repository's notes directory or edit this wiki directly.*
