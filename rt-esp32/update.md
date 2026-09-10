# RT ESP32-S3 Firmware Modernization Specification (`update.md`)

> **Status:** Solidified Technical Specification & Implementation Plan  
> **Target Hardware:** ESP32-S3 DevKitC-1-N16R8 (Dual-core Xtensa @ 240 MHz, 16 MB Flash, 8 MB Octal PSRAM)  
> **Framework:** ESP-IDF v5.1.x via PlatformIO (`espressif32@6.12.0`) | FreeRTOS (1000 Hz tick)  
> **Execution Horizon:** 3 to 4 working days  
> **Reference Context:** [`architecture.md`](architecture.md) (current code state), [`bugs.md`](bugs.md) (historical issue log)

---

## 1. Executive Summary & Pragmatic Filter

This specification refactors `rt-esp32` from its current 8-task, queue-heavy, workaround-laden implementation into a **deterministic 3-task appliance**. It rejects multi-month academic rewrites and adopts only verified, high-leverage architectural simplifications:

1. **Physical Bus Isolation (3 Tasks, Not 2):** High CAN (SPI-based MCP2515) and Low CAN (on-chip TWAI) have dedicated worker tasks on Core 0. A slow SPI transaction or High-CAN fault can never block Low-CAN motor or steering actuation.
2. **Deterministic Control Loop:** A single 100 Hz task on Core 1 executes kinematics, obstacle limits, and tested safety supervisors (`SteeringControl`, `SebBrakeFallback`, `MtrHealthSupervisor`) with $< 100\,\mu\text{s}$ jitter.
3. **Snapshot Mailboxes (`xQueueOverwrite`):** Eliminates torn multi-variable atomic reads. Coherent message snapshots pass between tasks using FreeRTOS depth-1 overwrite queues.
4. **Silicon CAN Arbitration:** Deletes the 40-attempt software retry pump (`gw_pump`) and 5 ms slot-reclaim hacks; restores hardware auto-retransmission (`fail_retry_cnt = -1`) for vehicle builds while retaining self-test for solo bench builds.
5. **Single SPI Owner:** Eliminates `g_spi_mutex` entirely. Only `can_high_task` communicates with the MCP2515.
6. **Dead Code Elimination:** Compile-time `#if` guards remove unused floating-point plant lag models (`std::exp`) and shadow PID calculations from production vehicle builds.

---

## 2. Target 3-Task Architecture

```
                   HIGH CAN BUS (500 kbit/s)                             LOW CAN BUS (500 kbit/s)
                     MCP2515 via SPI + INT                                 On-Chip TWAI Controller
                              │                                                       │
                              ▼                                                       ▼
                ┌───────────────────────────┐                           ┌───────────────────────────┐
                │       can_high_task       │                           │       can_low_task        │
                │     (Core 0, Prio 4)      │                           │     (Core 0, Prio 4)      │
                │   Sole MCP2515/SPI Owner  │                           │   Sole TWAI Driver Owner  │
                └─────────────┬─────────────┘                           └─────────────┬─────────────┘
                              │                                                       │
      g_host_cmd_mailbox      │                               g_feedback_mailbox      │
      (Depth-1 Overwrite)     │                               (Depth-1 Overwrite)     │
                              │  g_high_to_low_gw_q (Depth 8)                         │
                              │  (Bounded Transaction FIFO)                           │
                              │                                                       │
                              └───────────────────────┐       ┌───────────────────────┘
                                                      ▼       ▼
                                            ┌───────────────────────────┐
                                            │       control_task        │
                                            │     (Core 1, Prio 5)      │
                                            │   Deterministic 100 Hz    │
                                            │  Safety, Kinematics, Steer│
                                            └─────────────┬─────────────┘
                                                          │
                                                          ▼
                                              g_motion_output_mailbox
                                                (Depth-1 Overwrite)
```

### 2.1. Task Configuration & Allocation Table

| Task Name | Core Pinning | FreeRTOS Priority | Stack Allocation | Wakeup Mechanism | Core Responsibilities |
|---|:---:|:---:|:---:|---|---|
| **`can_high_task`** | Core 0 | 4 | 4096 B | GPIO 47 Task Notification or TX Queue Event | Sole owner of MCP2515 SPI. Drains RXB0/RXB1 (budget = 8), routes gateway frames, transmits High telemetry (`0x121`, `0x210`, `0x310`, `0x311`, `0x501`). |
| **`can_low_task`** | Core 0 | 4 | 4096 B | TWAI RX Event Queue or New TX Signal | Sole owner of TWAI. Drains Low RX, updates feedback mailbox, transmits Low actuator commands (`0x204`, `0x169`, `0x205`, `0x501`, `0x7FD`). |
| **`control_task`** | Core 1 | 5 | 4096 B | Periodic `vTaskDelayUntil` (10.0 ms) | Consumes latest mailboxes, runs safety checks & kinematics, updates output mailbox. Runs 10 Hz watchdog and 2 Hz heartbeats via tick dividers. |

---

## 3. Communication Primitives & Data Contracts

### 3.1. State Snapshot Mailboxes (Latest-Value Semantics)

State data uses FreeRTOS depth-1 queues with `xQueueOverwrite()`. This guarantees **100% semantic coherence** without multi-variable atomic race conditions or spinlock overhead:

```cpp
#pragma once
#include <cstdint>

// 1. Host Drive Command Snapshot (High -> Control)
struct HostDriveSnapshot {
    int32_t speed_mmps;              // Command speed [-500, 3000]
    int32_t yaw_rate_mrad_s;         // Command yaw rate [-3000, 3000]
    int32_t direct_steer_0_1deg;     // Command direct steer angle (0.1 deg)
    int64_t timestamp_us;            // Monotonic arrival timestamp (esp_timer_get_time)
    uint32_t obstacle_distance_mm;   // Closest obstacle distance
    uint8_t gear_override;           // 0=None, 1=D, 2=S, 3=R
    bool direct_steer_valid;         // 0x303 validity flag
    bool drive_cmd_valid;            // 0x300 validity flag
};

// 2. Actuator & Bus Feedback Snapshot (Low -> Control)
struct ActuatorFeedbackSnapshot {
    int32_t mtr_command_speed_mmps;  // Echoed speed from 0x206
    uint8_t mtr_gear_state;          // Gear state from 0x206
    int16_t ses_angle_0_1deg;        // Measured steering angle from 0x201
    uint8_t ses_angle_status;        // 0=centering, 1=aligned
    uint8_t ses_error_status;        // L3 error byte
    uint16_t seb_pressure_raw;       // Brake pressure raw from 0x721
    uint8_t seb_error_status;        // SEB error status
    int64_t last_mtr_us;             // 0x206 arrival timestamp
    int64_t last_ses_us;             // 0x201 arrival timestamp
    int64_t last_sys_hb_us;          // 0x7FE arrival timestamp
    int64_t last_sys_safety_sts_us;  // 0x011 arrival timestamp
    int64_t last_0x7b9_rx_us;        // Observed 0x7B9 arrival timestamp
    uint8_t sys_mode;                // Mode from 0x110
    bool sys_mode_valid;             // 0x110 rolling-counter validity
};

// 3. Motion Output Snapshot (Control -> Low/High CAN)
struct MotionOutputSnapshot {
    int32_t motor_speed_mmps;        // Commanded motor setpoint for 0x204
    uint8_t motor_gear;              // Gear command for 0x204
    int16_t steer_angle_0_1deg;      // Commanded steer angle for 0x169
    uint16_t steer_slew_rate_deg_s;  // Dynamic steer slew rate for 0x169
    int32_t brake_kpa;               // Commanded brake pressure for 0x205
    uint8_t current_mode;            // 0=Manual, 1=Auto, 2=Estop
    uint8_t estop_reason;            // Active ESTOP reason code
    uint8_t safety_state;            // 0=Normal, 1=InternalEstop, 2=Fault
    bool seb_emergency_takeover;     // True if RT owns 0x7B9 emergency transmission
    bool steer_command_enable;       // True if 0x169 should transmit
    bool reversing;                  // Vehicle reversing state
};
```

### 3.2. Bounded Gateway FIFO (Transactional Events)

Gateway commands requiring guaranteed single-delivery (`0x111 HMI_MODE_REQ`, `0x112 HMI_PWR_REQ`, `0x114 HOST_ESTOP_RESET_REQ`, `0x302 HOST_LIGHT_CMD`) use a bounded FIFO with a drop counter:

```cpp
struct GatewayFrame {
    can::Frame frame;
    int64_t enqueued_us;
};

// Depth-8 queue eliminates unbounded memory growth
QueueHandle_t g_high_to_low_gw_q = nullptr;
std::atomic<uint32_t> g_gw_drop_count{0};

inline bool post_gateway_frame(const can::Frame& fr) {
    GatewayFrame gw{fr, esp_timer_get_time()};
    if (xQueueSend(g_high_to_low_gw_q, &gw, 0) != pdTRUE) {
        g_gw_drop_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
```

---

## 4. Driver Specifications & Hardware Policies

### 4.1. TWAI Driver (Low Bus)
* **Configuration:**
  In [`src/can_driver_twai.cpp`](src/can_driver_twai.cpp):
  ```cpp
  twai_onchip_node_config_t config{};
  config.io_cfg.tx = static_cast<gpio_num_t>(m_config.tx_gpio);
  config.io_cfg.rx = static_cast<gpio_num_t>(m_config.rx_gpio);
  config.bit_timing.bitrate = m_config.bitrate_hz;
  config.tx_queue_depth = 5;

  #if ETRIKE_RT_TWAI_SELF_TEST
      // Bench solo mode: self-test eliminates ACK requirement, preventing Bus-Off
      config.fail_retry_cnt = 0;
      config.flags.enable_self_test = 1;
  #else
      // Vehicle mode: standard CAN hardware auto-retransmission
      config.fail_retry_cnt = -1;
      config.flags.enable_self_test = 0;
  #endif
  ```
* **Deletions:**
  - Delete `gw_pump()` and the `GwTxFrame` 40-attempt retry mechanism in `main.cpp`.
  - Delete the 5 ms in-flight slot reclamation timer (`kTxReclaimUs`) in `can_driver_twai.cpp`.
* **Discipline:** Low CAN transmits latest-value frames (`0x204`, `0x169`, `0x205`). If the bus experiences a temporary outage, old frames are dropped—**no stale backlog is ever replayed upon recovery**.

### 4.2. MCP2515 Driver (High Bus)
* **Exclusive Ownership:** `can_high_task` is the **sole caller** of `mcp.send()`, `mcp.receive()`, and register reads.
* **Deletion:** Delete `g_spi_mutex`, `spi_lock()`, and `spi_unlock()`.
* **Bounded RX Processing & Hardware Rollover:**
  ```cpp
  // Configured in init:
  // Enable rollover: RXB0 overflows into RXB1 if full (BUKT=1)
  modify_reg(kRegRxb0Ctrl, 0x04, 0x04);

  // Execution inside can_high_task:
  constexpr unsigned kRxBudget = 8;
  for (unsigned i = 0; i < kRxBudget; ++i) {
      can::Frame fr;
      if (!g_can_high.receive(fr, 0)) break;
      process_high_frame(fr);
  }
  service_high_tx(); // Transmit pending telemetry
  ```
* **Hardware TX Allocation:**
  - `TXB2` (Highest priority): Used exclusively for `0x001 SAFETY_ESTOP`.
  - `TXB0`: Used for normal periodic telemetry (`0x121`, `0x210`, `0x310`, `0x311`, `0x501`, `0x620`).

---

## 5. Task Implementation Skeletons

### 5.1. `can_high_task` (Core 0, Priority 4)
```cpp
[[noreturn]] void can_high_task(void*) {
    can::Frame fr;
    TickType_t last_10hz = xTaskGetTickCount();
    uint8_t motion_counter = 0;
    uint8_t diag_counter = 0;

    while (true) {
        // 1. Sleep waiting for MCP2515 INT pin (GPIO 47) or 10 ms periodic timeout
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));

        // 2. Bounded RX Drain
        for (unsigned i = 0; i < 8; ++i) {
            if (!g_can_high.receive(fr, 0)) break;
            route_high_can_frame(fr);
        }

        // 3. 100 Hz High Motion Report (0x121)
        MotionOutputSnapshot out{};
        if (xQueuePeek(g_motion_output_mailbox, &out, 0) == pdTRUE) {
            can::Frame motion_fr;
            ActuatorFeedbackSnapshot fbk{};
            xQueuePeek(g_feedback_mailbox, &fbk, 0);
            auto rpt = rt::make_motion_report(
                esp_timer_get_time(), fbk.mtr_command_speed_mmps, fbk.mtr_gear_state,
                fbk.last_mtr_us, fbk.ses_angle_0_1deg, fbk.ses_angle_status,
                fbk.last_ses_us, motion_counter++);
            if (can::encode_frame(rpt, motion_fr) == can::gen::CodecStatus::Ok) {
                g_can_high.send(motion_fr, 0);
            }
        }

        // 4. 10 Hz Telemetry (0x210, 0x310, 0x311, 0x501)
        if (xTaskGetTickCount() - last_10hz >= pdMS_TO_TICKS(100)) {
            last_10hz = xTaskGetTickCount();
            send_high_periodic_telemetry(out);
        }
    }
}
```

### 5.2. `can_low_task` (Core 0, Priority 4)
```cpp
[[noreturn]] void can_low_task(void*) {
    can::Frame fr;
    TickType_t last_100hz = xTaskGetTickCount();
    TickType_t last_50hz = xTaskGetTickCount();
    auto* drv = rt::can_low_driver();

    while (true) {
        // 1. Drain incoming Low CAN frames from TWAI queue (1 ms timeout)
        while (drv->receive(fr, 1)) {
            route_low_can_frame(fr);
        }

        // 2. Forward Gateway Frames (High -> Low)
        GatewayFrame gw;
        const int64_t now_us = esp_timer_get_time();
        while (xQueueReceive(g_high_to_low_gw_q, &gw, 0) == pdTRUE) {
            // Drop stale gateway frames older than 500 ms
            if (now_us - gw.enqueued_us <= 500'000) {
                drv->send(gw.frame, 0);
            }
        }

        // 3. 100 Hz Actuator Output: 0x204 RT_DRIVE_CMD
        MotionOutputSnapshot out{};
        if (xTaskGetTickCount() - last_100hz >= pdMS_TO_TICKS(10)) {
            last_100hz = xTaskGetTickCount();
            if (xQueuePeek(g_motion_output_mailbox, &out, 0) == pdTRUE) {
                can::gen::RtDriveCmd msg{out.motor_speed_mmps, out.motor_gear};
                if (can::encode_frame(msg, fr) == can::gen::CodecStatus::Ok) drv->send(fr, 0);
            }
        }

        // 4. 50 Hz Actuator Outputs: 0x169 SES, 0x205 BRAKE, 0x7B9 EMERGENCY
        if (xTaskGetTickCount() - last_50hz >= pdMS_TO_TICKS(20)) {
            last_50hz = xTaskGetTickCount();
            if (out.current_mode != uint8_t(can::Mode::Manual)) {
                // 0x205 Brake Command
                can::gen::RtBrakeCmd bmsg{out.brake_kpa};
                if (can::encode_frame(bmsg, fr) == can::gen::CodecStatus::Ok) drv->send(fr, 0);

                // 0x169 Steering Request
                if (out.steer_command_enable) {
                    can::custom::ses::Command smsg{};
                    // populated from SteeringControl
                    if (can::custom::ses::encode_command(smsg, fr) == can::gen::CodecStatus::Ok) drv->send(fr, 0);
                }

                // 0x7B9 SEB Emergency Takeover (Only when SYS 0x7B9 has vanished)
                if (out.seb_emergency_takeover) {
                    static uint8_t seb_roll = 0;
                    send_seb_req(*drv, fr, rt::make_seb_takeover_req(), seb_roll);
                }
            }
        }
    }
}
```

### 5.3. `control_task` (Core 1, Priority 5)
```cpp
[[noreturn]] void control_task(void*) {
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick_counter = 0;

    // Safety state instances (kept intact as verified singletons)
    rt::g_brake_fallback.init(esp_timer_get_time());
    rt::g_safety_authority.reset(esp_timer_get_time());

    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
        tick_counter++;
        const int64_t now = esp_timer_get_time();

        // 1. Read input snapshots
        HostDriveSnapshot host{};
        ActuatorFeedbackSnapshot fbk{};
        xQueuePeek(g_host_cmd_mailbox, &host, 0);
        xQueuePeek(g_feedback_mailbox, &fbk, 0);

        // 2. Safety Authority Acquisition (Issue #10) & Standstill Watchdog (Issue #8)
        evaluate_safety_streams(now, host, fbk);

        // 3. Resolve Kinematics & Limits
        MotionOutputSnapshot out = execute_motion_pipeline(now, host, fbk);

        // 4. Publish Output Snapshot
        xQueueOverwrite(g_motion_output_mailbox, &out);

        // 5. Tick-Divided 10 Hz Staleness Check (Every 10 ticks)
        if (tick_counter % 10 == 0) {
            check_host_command_staleness(now, host);
        }

        // 6. Tick-Divided 2 Hz Heartbeats (Every 50 ticks)
        if (tick_counter % 50 == 0) {
            emit_dual_heartbeats(now, out);
        }
    }
}
```

---

## 6. Dead Code & Feature Gating

In [`platformio.ini`](platformio.ini), production vehicle builds set:
```ini
-D ETRIKE_RT_PID_MODE=0
-D ETRIKE_RT_SPEED_FEEDBACK_SOURCE=0
-D ETRIKE_RT_ENCODERS=0
```

The codebase will wrap these features with preprocessor guards:
1. **Calculated Speed Plant Model:**
   ```cpp
   #if ETRIKE_RT_SPEED_FEEDBACK_SOURCE == 3
       g_calc_speed.update(sp.motor_speed_mmps, 0.01f);
   #endif
   ```
2. **Shadow PID & 0x220 Telemetry:**
   ```cpp
   #if ETRIKE_RT_PID_MODE > 0
       g_speed_ctrl.update_shadow_pid(sp.motor_speed_mmps, measured_speed_mmps, 0.01f, pid_out);
       // emit 0x220 RT_PID_RPT
   #endif
   ```
3. **PCNT Wheel Encoders:**
   In [`src/encoder_pcnt.cpp`](src/encoder_pcnt.cpp), wrap entire file in `#if ETRIKE_RT_ENCODERS == 1`.
4. **Relocate Simulator Core:**
   Move [`src/core/rt_core.cpp`](src/core/rt_core.cpp) and [`rt_core.h`](src/core/rt_core.h) to `simulation/full_system/vehicle/`.

---

## 7. Staged 4-Day Implementation Roadmap

```
Day 1: Transport & Driver Clean ──► Days 2–3: 3-Task Collapse ──► Day 4: Dead Code & Test
```

### Day 1: Driver Sanitization & TWAI Auto-Retransmit
- [ ] Configure `config.fail_retry_cnt = -1` for `[env:vehicle]` in `can_driver_twai.cpp`.
- [ ] Confine `fail_retry_cnt = 0` and `enable_self_test = 1` strictly to `[env:hardware_bench]`.
- [ ] Delete `gw_pump()`, `GwTxFrame`, and the 5 ms in-flight slot-reclaim timer.
- [ ] Delete `g_spi_mutex`; verify only `can_high` touches the MCP2515.
- [ ] Implement the 8-frame bounded RX budget in MCP2515 receive loops.
- [ ] *Verification:* Compile with `pio run` and verify native tests pass.

### Days 2–3: Collapse 8 Tasks to 3 Tasks
- [ ] Create `g_host_cmd_mailbox`, `g_feedback_mailbox`, and `g_motion_output_mailbox` using `xQueueCreate(1, ...)`.
- [ ] Implement `can_high_task` (Core 0, Prio 4) merging `rx_high` and `tx_high`.
- [ ] Implement `can_low_task` (Core 0, Prio 4) merging `rx_low`, `tx_low`, and `dispatch`.
- [ ] Implement `control_task` (Core 1, Prio 5) running at 100 Hz via `vTaskDelayUntil`.
- [ ] Move 10 Hz staleness checks and 2 Hz heartbeats into `control_task` tick dividers (`% 10`, `% 50`).
- [ ] Delete `t_watchdog`, `t_heartbeat`, `g_cmd_q`, `g_setpoint_q`, and `g_alive_*` atomic alive counters.
- [ ] *Verification:* Verify on physical hardware with CANalyst-II:
  - Jitter on `0x204` is $< 150\,\mu\text{s}$.
  - End-to-end command-to-actuation latency is $< 10\,\text{ms}$.

### Day 4: Dead Code Elimination & Bench Validation
- [ ] Preprocessor-guard `g_calc_speed.update()`, `g_speed_ctrl.update_shadow_pid()`, and `0x220 RT_PID_RPT`.
- [ ] Move `src/core/rt_core.*` to `simulation/full_system/vehicle/`.
- [ ] Run 2-hour bench soak test verifying:
  - Zero memory leaks.
  - Zero task stalls.
  - Clean ESTOP assert and asymmetric 2-advancing-zero clear sequence.
  - Clean `0x7B9` emergency takeover and handback under simulated SYS disconnect.

---

## 8. Verification & Acceptance Commands

```bash
# 1. Native protocol, safety, and kinematics unit test suite
cd e:\work\etrike\rt-esp32\test
pio test -e native

# 2. Compile vehicle target
cd e:\work\etrike\rt-esp32
pio run -e vehicle

# 3. Compile bench target
pio run -e bench

# 4. Flash to DevKitC-1 and monitor
pio run -e vehicle -t upload
pio device monitor
```
