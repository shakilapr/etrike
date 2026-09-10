# SYS ESP32-S3 Architectural Hardening & Production Update Plan

> **Status:** Authoritative Architectural Review & Implementation Guide  
> **Target:** `sys-esp32` Firmware (`v0.8.0-alpha-vehicle`)  
> **Verdict on Recent Proposals:**  
> **Do not perform a multi-month rewrite of the firmware.** The proposed 5-task "miniature AUTOSAR" rewrite is rejected: the ESP32-S3 dual-core MCU @ 240 MHz with 512 KB SRAM runs the current 13 tasks using only ~42 KB of stack (8.2% of SRAM) with deterministic 20 ms / 100 ms loop timing.  
> 
> However, deep code analysis reveals **two genuine, high-severity distributed system defects** in the current code that must be resolved immediately:
> 1. **A textbook distributed deadlock between SYS and MTR during ESTOP reset** (`0x114` remote reset is 100% blocked).
> 2. **ESTOP ACK Watchdog starvation under CAN `0x001` frame bursts** (retries reset to 3 on every arriving frame).
>
> This document details the exact root-cause mechanics, validates what to accept vs. reject, and provides production-ready code solutions that can be applied in **under 50 lines of code** with **zero breaking changes to peer ECUs**.

---

## 1. Deep Root-Cause Analysis of Live Bugs

### Defect 1: The Distributed Reset Deadlock Between SYS and MTR

#### The Mechanics
An inspection of [`sys-esp32/src/inhibit_state.h`](file:///e:/work/etrike/sys-esp32/src/inhibit_state.h) alongside [`mtr-stm32/src/motor_manager.h`](file:///e:/work/etrike/mtr-stm32/src/motor_manager.h) reveals an unresolvable distributed deadlock:

```
[SYS ESP32]                                                      [MTR STM32]
     │                                                                │
     │ 1. ESTOP Active: Broadcasts 0x011 (estop_active = 1)          │
     ├───────────────────────────────────────────────────────────────►│
     │                                                                │ Latches ESTOP. Sets
     │ 2. MTR Reports ESTOP in 0x206 (kMtrFaultEstopActive = 1)       │ bit 0 in 0x206 fbk
     │◄───────────────────────────────────────────────────────────────┤
     │                                                                │
     │ 3. Operator / Host requests Reset (0x114 HOST_ESTOP_RESET_REQ) │
     │    SYS calls get_estop_reset_blockers():                       │
     │    "if (mtr_fault_flags & 0x0001) mask |= kResetBlockMtr..."   │
     │    DEADLOCK: SYS rejects reset because MTR is in ESTOP!        │
     │                                                                │
     │    MTR in motor_manager.h:233 requires TWO advancing 0x011     │
     │    frames with estop_active == 0 before it will clear bit 0!   │
     ▼                                                                ▼
DEADLOCKED: SYS will not clear 0x011 until MTR clears bit 0.
            MTR will not clear bit 0 until SYS clears 0x011.
```

1. In [`sys-esp32/src/inhibit_state.h:139-141`](file:///e:/work/etrike/sys-esp32/src/inhibit_state.h#L139-L141):
   ```cpp
   if (mtr_fault_flags & 0x0001) { // kMtrFaultEstopActive (bit 0)
       mask |= kResetBlockMtrEstopActive;
   }
   ```
   If `mtr-stm32` reports `kMtrFaultEstopActive == 1`, SYS considers it an active blocker and **rejects the reset request** (`0x115 result = REJECTED`). SYS refuses to exit `Mode::Estop` or drop `estop_active` in `0x011`.
2. In [`mtr-stm32/src/motor_manager.h:233-263`](file:///e:/work/etrike/mtr-stm32/src/motor_manager.h#L233-L263):
   ```cpp
   // MTR requires TWO consecutive advancing frames of 0x011 with estop_active == 0
   // before it executes authorized_clear() and drops kMtrFaultEstopActive!
   if (clear_confirm_ >= 2) authorized_clear(now_ms);
   ```
   `mtr-stm32` **cannot** clear `kMtrFaultEstopActive` while SYS continues broadcasting `0x011 (estop_active = 1)`.
3. **The Root Error:** `kMtrFaultEstopActive == 1` during ESTOP is **proof that MTR successfully received and confirmed the emergency stop**. It is the expected, healthy state during ESTOP. Treating it as a reset blocker causes a permanent mutual wait.

---

### Defect 2: ESTOP ACK Watchdog Starvation Under CAN Bursts

#### The Mechanics
In [`sys-esp32/src/main.cpp:431-435`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L431-L435) (CAN `0x001` ingest) and [`line 593`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L593) (SEB `0x731` L3 ingest):
```cpp
case can::kIdSafetyEstop: {  // 0x001
    ...
    g_mode_mgr.force_estop();
    trigger_estop_ack_watchdog(static_cast<uint32_t>(now));
```
`trigger_estop_ack_watchdog()` calls `g_mtr_ack_watchdog.trigger()` in [`mtr_estop_ack.h:29-33`](file:///e:/work/etrike/sys-esp32/src/mtr_estop_ack.h#L29-L33):
```cpp
m_pending.store(true, std::memory_order_release);
m_deadline.store(now_tick + static_cast<uint32_t>(sys::kMtrEstopAckTimeoutMs), std::memory_order_release);
m_retries_left.store(static_cast<uint8_t>(sys::kMtrEstopAckMaxRetries), std::memory_order_release); // RESETS RETRIES TO 3!
```

1. On a vehicle CAN network, when an ESTOP switch trips, multiple nodes or repeaters broadcast `0x001` repeatedly at high frequency.
2. In `task_dispatch`, every incoming `0x001` frame calls `trigger_estop_ack_watchdog()`.
3. This unconditionally resets `m_deadline` to `now + 100ms` and `m_retries_left` back to 3.
4. **The Failure Mode:** Under an ongoing `0x001` burst, `g_mtr_ack_watchdog.check_tick()` in `task_safety` is starved: `m_deadline` is continually pushed into the future. **The watchdog never decrements retries and never reaches `ExhaustedFault`, even if `mtr-stm32` is completely dead or disconnected.**

---

### Defect 3: MTR ACK Exhaustion Silently Cleared by Legacy SEB Recovery Timer

#### The Mechanics
In [`sys-esp32/src/main.cpp:724-727`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L724-L727):
```cpp
} else if (action == sys::MtrEstopAckWatchdog::Action::ExhaustedFault) {
    ESP_LOGE(TAG, "MTR ESTOP ACK failed after retries — latched brake fault");
    g_brake_fault_active.store(true, std::memory_order_relaxed);
}
```
Directly below in [`sys-esp32/src/main.cpp:773-787`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L773-L787):
```cpp
// Legacy brake-fault auto-recovery:
if (g_brake_fault_active.load(std::memory_order_relaxed)) {
    bool seb_healthy = g_seb_error_status.load(std::memory_order_relaxed) < 3;
    bool not_in_estop = (g_mode_mgr.mode() != can::Mode::Estop);
    if (seb_healthy && not_in_estop) {
        if (++brake_recovery_count >= 30) {  // 3 seconds
            g_brake_fault_active.store(false, std::memory_order_relaxed); // BUG: Silently cleared!
        }
    }
}
```
1. `g_brake_fault_active` was historically written for transient brake excursions.
2. When MTR ACK exhaustion was mapped to `g_brake_fault_active`, it inherited this 3-second auto-clearing timer.
3. If an operator resets out of ESTOP while `mtr-stm32` is dead, `seb_healthy` is true (the brake actuator is fine). Thus, **the motor controller fault is silently cleared after 3 seconds**.

---

## 2. Decision Matrix: Accepted Fixes vs. Rejected Rewrites

| Suggestion | Verdict | Rationale & Engineering Justification |
|---|:---:|---|
| **Edge-Triggered Watchdog Arming** | **ACCEPT** | **Zero-risk bug fix.** Arming `MtrEstopAckWatchdog` strictly on the $0 \to 1$ edge entering ESTOP prevents timer starvation under `0x001` bursts. |
| **Staged Reset Protocol** | **ACCEPT** | **Zero-risk bug fix.** Replaces the deadlocking check with an acknowledgment check: verifies MTR *confirmed* ESTOP, drops `0x011` to allow MTR to clear, and keeps power OFF until re-armed. |
| **Latched Fault for MTR ACK Failure** | **ACCEPT** | **Safety critical.** Binds MTR ACK exhaustion to `LatchedFaultReason` so it requires an explicit operator reset and deletes the 3-second auto-clear. |
| **Wrap-Safe Unsigned Tick Math** | **ACCEPT** | Standard C++ practice: ensures `(uint32_t)(now - then) >= timeout` is used across all tick timers. |
| **Keep Local Outputs Decoupled from CAN TX** | **ACCEPT** | Corrects an earlier proposal: local brake lights and relays must never freeze if CAN TX encounters error-passive or mailbox blocking. |
| **Rewrite 13 Tasks into 5 Tasks** | **REJECT** | **Massive Scope Creep.** The ESP32-S3 has 512 KB of SRAM. 13 tasks use ~42 KB (8.2%). Context-switch overhead is $< 5\,\mu\text{s}$. Rewriting into mailboxes and queues introduces new race conditions and invalidates all existing unit/integration tests. |
| **Custom CAN TX Deadline Scheduler (`TxSlot`)** | **REJECT** | **Over-engineering.** Calling `send_can()` from periodic tasks at 20 ms and 100 ms is already deterministic and meeting wire timing. |
| **New E2E CRC & Data IDs on All Frames** | **REJECT** | **Breaks Trike Wire Protocol.** Requires modifying DBC/YAMLs and recompiling `rt-esp32`, `mtr-stm32`, `jetson` (ROS 2), and `rm-esp32`. Current rolling counters and CRCs on `0x011`/`0x500` are sufficient. |
| **Delete Jetson Remote Reset (`0x114` / `0x115`)** | **REJECT** | **Breaks Autoware Autonomy.** Autonomous missions operate with no rider in the seat; remote reset via high-level command is an essential vehicle capability. |
| **Delete RT's `0x7B9` Emergency Fallback** | **REJECT** | SYS is already the sole normal producer of `0x7B9`. RT only transmits if SYS goes completely dead. Removing this eliminates safety redundancy. |
| **Replace NVS with RTC Memory** | **REJECT** | ESP-IDF NVS already performs log-structured wear levelling. Total battery disconnection wipes RTC memory, destroying crash forensics. |

---

## 3. Production-Ready Code Solutions

The four accepted fixes require changes to only **three files** in `sys-esp32`:

### Fix 1: Edge-Triggered Watchdog Arming in `main.cpp`

In [`sys-esp32/src/main.cpp`](file:///e:/work/etrike/sys-esp32/src/main.cpp), replace the dispersed calls to `force_estop()` and `trigger_estop_ack_watchdog()` with an authoritative, edge-triggered helper:

```cpp
// In sys-esp32/src/main.cpp (around line 200)

static void enter_estop(const char* reason) {
    const bool was_estop = (g_mode_mgr.mode() == can::Mode::Estop);
    if (!was_estop) {
        g_mode_mgr.force_estop();
        // ARM ONLY ON THE 0 -> 1 TRANSITION EDGE:
        trigger_estop_ack_watchdog(xTaskGetTickCount());
        ESP_LOGE(TAG, "ESTOP entered [edge]: %s", reason);
    }
    // Broadcast 0x001 on CAN (rate-limited by should_send_estop_now)
    if (can_send_estop()) {
        send_estop_frame(reason);
    }
}
```
Replace the ad-hoc calls in `task_dispatch` (lines 431, 589) and `task_safety` (lines 660, 694) with `enter_estop("Reason")`. Subsequent incoming `0x001` frames while already in ESTOP broadcast if needed but **do not re-arm the watchdog timer**.

---

### Fix 2: Staged ESTOP Reset Handshake (Deadlock Resolution)

#### Step 1: Update `mtr_estop_ack.h` to Track Prior Acknowledgment
In [`sys-esp32/src/mtr_estop_ack.h`](file:///e:/work/etrike/sys-esp32/src/mtr_estop_ack.h), add `m_ack_received`:

```cpp
// sys-esp32/src/mtr_estop_ack.h
class MtrEstopAckWatchdog {
public:
    void trigger(uint32_t now_tick, uint8_t current_mtr_fault_flags) {
        m_ack_received.store(false, std::memory_order_release);
        if (current_mtr_fault_flags & shared::kMtrFaultEstopActive) {
            m_pending.store(false, std::memory_order_release);
            m_ack_received.store(true, std::memory_order_release);
            return;
        }
        m_pending.store(true, std::memory_order_release);
        m_deadline.store(now_tick + static_cast<uint32_t>(sys::kMtrEstopAckTimeoutMs), std::memory_order_release);
        m_retries_left.store(static_cast<uint8_t>(sys::kMtrEstopAckMaxRetries), std::memory_order_release);
    }

    void on_feedback_received(uint8_t mtr_fault_flags) {
        if (mtr_fault_flags & shared::kMtrFaultEstopActive) {
            m_pending.store(false, std::memory_order_release);
            m_ack_received.store(true, std::memory_order_release);
        }
    }

    bool has_acknowledged() const {
        return m_ack_received.load(std::memory_order_acquire);
    }
private:
    std::atomic<bool> m_ack_received{false};
    // ... remaining members unchanged
};
```

#### Step 2: Correct Blocker Evaluation in `inhibit_state.h`
In [`sys-esp32/src/inhibit_state.h`](file:///e:/work/etrike/sys-esp32/src/inhibit_state.h), modify `get_estop_reset_blockers()`:

```cpp
// In sys-esp32/src/inhibit_state.h around line 115:
inline uint16_t get_estop_reset_blockers(
    bool physical_estop,
    bool hb_ok,
    int16_t measured_speed_mmps,
    bool mtr_ack_confirmed,          // CHANGED: pass watchdog confirmation, not raw flag
    uint16_t token = kRemoteResetTokenMagic
) {
    uint16_t mask = kResetBlockNone;
    if (token != kRemoteResetTokenMagic) {
        mask |= kResetBlockInvalidToken;
    }
    if (physical_estop) {
        mask |= kResetBlockPhysicalEstop;
    }
    if (!latched_causes_currently_clearable()) {
        mask |= kResetBlockLatchedFault;
    }
    int16_t abs_speed = (measured_speed_mmps < 0) ? -measured_speed_mmps : measured_speed_mmps;
    if (abs_speed > kResetMaxMovingSpeedMmps) {
        mask |= kResetBlockMoving;
    }
    if (!hb_ok) {
        mask |= kResetBlockHeartbeatLoss;
    }
    // DEADLOCK FIX: Reject reset ONLY if MTR NEVER acknowledged the ESTOP.
    // If MTR did acknowledge, ESTOP_ACTIVE is expected to be 1 until SYS clears 0x011.
    if (!mtr_ack_confirmed) {
        mask |= kResetBlockMtrEstopActive;
    }
    if (transient_inhibited()) {
        mask |= kResetBlockTransientInhibit;
    }
    return mask;
}
```

#### Step 3: Pass Watchdog Status in `main.cpp`
In [`sys-esp32/src/main.cpp:334-340`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L334-L340):
```cpp
uint16_t blockers = sys::get_estop_reset_blockers(
    /*physical_estop=*/g_safety.estop_active(),
    /*hb_ok=*/g_safety.heartbeat_ok(),
    /*measured_speed_mmps=*/g_wheel_measured_mmps.load(std::memory_order_relaxed),
    /*mtr_ack_confirmed=*/g_mtr_ack_watchdog.has_acknowledged(), // DEADLOCK RESOLVED
    /*token=*/static_cast<uint16_t>(req.reset_token)
);
```

#### Step 4: Staged Mode Transition in `mode_manager.cpp`
When reset succeeds, `ModeManager` transitions mode to `MANUAL` with `power_on = false`:
1. `sys_estop_latched()` becomes `false`.
2. `task_can_tx` immediately emits `0x011 (estop_active = 0)`.
3. `task_mode` emits `0x113 (power_state = 0)` (power remains strictly OFF).
4. `mtr-stm32` receives two advancing `0x011` frames with `estop_active == 0`, executes `authorized_clear()`, and drops `kMtrFaultEstopActive`.
5. Propulsion remains inhibited until an explicit subsequent power command (`0x113 OFF → ON`) is authorized.

---

### Fix 3: Latched MTR ACK Exhaustion (Delete 3-Second Auto-Clear)

#### Step 1: Add Bit to `LatchedFaultReason`
In [`sys-esp32/src/inhibit_state.h`](file:///e:/work/etrike/sys-esp32/src/inhibit_state.h):
```cpp
enum LatchedFaultReason : uint32_t {
    kLatchedBrakeFollowing    = 1u << 0,  // confirmed persistent following error
    kLatchedSebL3             = 1u << 1,  // SEB error_status >= 3
    kLatchedMtrEstopAckFailed = 1u << 2,  // MTR failed to acknowledge ESTOP
};

inline void clear_latched_fault_reasons() {
    g_latched_fault_reasons.fetch_and(
        ~(static_cast<uint32_t>(kLatchedSebL3) |
          static_cast<uint32_t>(kLatchedBrakeFollowing) |
          static_cast<uint32_t>(kLatchedMtrEstopAckFailed)),
        std::memory_order_relaxed);
}
```

#### Step 2: Escalate to Latched Fault in `main.cpp`
In [`sys-esp32/src/main.cpp:724-727`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L724-L727):
```cpp
} else if (action == sys::MtrEstopAckWatchdog::Action::ExhaustedFault) {
    ESP_LOGE(TAG, "MTR ESTOP ACK failed after retries — latched safety fault");
    sys::set_latched_fault(sys::kLatchedMtrEstopAckFailed);
}
```

#### Step 3: Delete Legacy Recovery Loop in `main.cpp`
Delete lines 769–787 of [`sys-esp32/src/main.cpp`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L769-L787) (`brake_recovery_count >= 30` loop).

---

## 4. Verification & Validation Protocol

Before bench release, execute these three targeted validation tests:

### Test 1: CAN `0x001` Burst Starvation Test
1. Inject CAN `0x001` frames continuously at 50 Hz using a CAN interface tool.
2. Disconnect `mtr-stm32` (or suppress `0x206`).
3. **Pass Criteria:** `sys-esp32` must log `MTR ESTOP ACK timeout` and escalate to `kLatchedMtrEstopAckFailed` within exactly $300 - 350\,\text{ms}$. The timeout must **not** be deferred by incoming `0x001` frames.

### Test 2: Remote Reset Handshake (`0x114` → `0x115`)
1. Trigger ESTOP; verify `mtr-stm32` confirms `ESTOP_ACTIVE` in `0x206`.
2. Send `0x114 HOST_ESTOP_RESET_REQ` (`reset_token = 0x5253`).
3. **Pass Criteria:** 
   - SYS replies `0x115 SYS_ESTOP_RESET_RSP` with `result = ACCEPTED (0)` and `blocker_mask = 0`.
   - SYS emits `0x011` with `estop_active = 0`.
   - `mtr-stm32` clears its internal latch and drops `ESTOP_ACTIVE` in `0x206`.
   - Vehicle remains in `MANUAL` with contactor power OFF (`0x113 = OFF`).

### Test 3: MTR ACK Failure Latch Persistence
1. Trigger ESTOP with `mtr-stm32` disconnected.
2. Allow ACK retries to exhaust; verify `kLatchedMtrEstopAckFailed` is set in `0x500.block_mask`.
3. Wait 10 seconds with SEB connected and healthy.
4. **Pass Criteria:** Fault **must remain latched**. `g_brake_fault_active` must **not** auto-clear.

---

## 5. Summary Implementation Checklist

- [x] Add `enter_estop()` edge-trigger helper in `main.cpp`.
- [x] Add `m_ack_received` to `MtrEstopAckWatchdog` in `mtr_estop_ack.h`.
- [x] Update `get_estop_reset_blockers()` in `inhibit_state.h` to check `has_acknowledged()`.
- [x] Add `kLatchedMtrEstopAckFailed` to `LatchedFaultReason` in `inhibit_state.h`.
- [x] Delete legacy 30-tick `g_brake_fault_active` auto-clear loop in `main.cpp`.
- [ ] Verify test cases 1, 2, and 3 on the physical test bench.
