/* MTR STM32 — Motor Controller Firmware
 *
 * EGAS Level 1: Function Controller
 * Dedicated STM32 board that owns all motor-related I/O:
 *   - MCP4725 I2C DAC (0-5V throttle output)
 *   - ADC (throttle grip position, 0-5V)
 *   - TLP281 optoisolator inputs (72V gear sense, active-low)
 *   - Relay outputs (72V gear control)
 *   - ESTOP button GPIO (Level 3 — direct hardware kill, also monitored)
 *   - CAN (low-level bus, bxCAN)
 *
 * FreeRTOS tasks:
 *   Pri 5  task_can_rx    — CAN receive, event-driven
 *   Pri 5  task_safety    — ESTOP GPIO monitor, 0x204 staleness (20 Hz)
 *   Pri 4  task_control   — Main motor control loop (100 Hz)
 *   Pri 3  task_can_tx    — CAN 0x120 @ 100Hz, 0x206 @ 50Hz
 *
 * Cross-task state uses std::atomic (lock-free, no mutexes).
 * Per architecture design principle #1: "Queues over shared state."
 * CAN frames flow through queues; actuation state is atomic.
 */

#include <cstdint>
#include <atomic>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"

/* Shared protocol headers */
#include "can/can_protocol.h"

/* MTR module headers */
#include "config.h"
#include "can_driver.h"
#include "mcp4725_dac.h"
#include "throttle_input.h"
#include "gear_control.h"

/* ── CubeMX-generated stubs — replace with generated code ──────────── */
/* When STM32CubeMX project is configured, these externs link to the   */
/* generated main.c. Until then, stubs satisfy the linker for CI.       */
extern "C" {
CAN_HandleTypeDef hcan = {};
I2C_HandleTypeDef  hi2c1 = {};
ADC_HandleTypeDef  hadc1 = {};
}

/* ── Cross-task state (std::atomic, lock-free) ────────────────────── */

/// Current system mode (written by task_can_rx from 0x110).
std::atomic<can::Mode> g_mode{can::Mode::Manual};

/// ESTOP active flag: set by task_can_rx (0x001) or task_safety (GPIO).
std::atomic<bool> g_estop_active{false};

/// Command speed from CAN 0x204 (written by task_can_rx).
std::atomic<int32_t> g_cmd_speed_mmps{0};

/// Command gear from CAN 0x204 (written by task_can_rx).
std::atomic<uint8_t> g_cmd_gear{0};

/// Timestamp of last CAN 0x204 in FreeRTOS ticks (written by task_can_rx).
std::atomic<uint32_t> g_last_cmd_tick{0};

/// Actual speed in mm/s (written by task_control, read by task_can_tx).
std::atomic<int16_t> g_actual_speed_mmps{0};

/// Current gear state (written by task_control, read by task_can_tx).
std::atomic<uint8_t> g_current_gear{0};

/// Fault flags byte for 0x206 (written by task_control + task_safety).
std::atomic<uint8_t> g_fault_flags{0};

/// Startup grace period flag: true for first 3 s to suppress stale warnings.
std::atomic<bool> g_startup_grace{true};

/* ── Global driver instances (defined in their respective .h files) ── */

namespace mtr {
    Mcp4725Dac  g_dac;
    ThrottleInput g_throttle;
    GearControl g_gear;
    CanDriver   g_can;
}

static bool estop_gpio_pressed() {
    // Stub until the CubeMX GPIO layer is wired into this target.
    return false;
}

/* ── Task function prototypes ─────────────────────────────────────── */

extern "C" {
    void task_can_rx(void* pvParameters);
    void task_safety(void* pvParameters);
    void task_control(void* pvParameters);
    void task_can_tx(void* pvParameters);
}

/* ── CAN frame processing ─────────────────────────────────────────── */

/// Dispatch a received CAN frame to the appropriate atomic state.
/// Called from task_can_rx.
static void process_can_frame(const can::Frame& frame) {
    if (frame.id == can::kIdSafetyEstop) {
        /* 0x001 SAFETY_ESTOP — DLC=0, no payload */
        g_estop_active.store(true, std::memory_order_relaxed);

    } else if (frame.id == can::kIdSysModeCmd) {
        /* 0x110 SYS_MODE_CMD — u8 mode */
        can::SysModeCmd cmd = can::SysModeCmd::from_frame(frame);
        can::Mode new_mode = static_cast<can::Mode>(cmd.mode & 0x03);
        if (new_mode == can::Mode::Manual ||
            new_mode == can::Mode::Auto ||
            new_mode == can::Mode::Estop) {
            g_mode.store(new_mode, std::memory_order_relaxed);
        }

    } else if (frame.id == can::kIdRtDriveCmd) {
        /* 0x204 RT_DRIVE_CMD — i32 speed + u8 gear */
        can::RtDriveCmd cmd = can::RtDriveCmd::from_frame(frame);
        g_cmd_speed_mmps.store(cmd.motor_speed_mmps, std::memory_order_relaxed);
        g_cmd_gear.store(cmd.gear, std::memory_order_relaxed);
        g_last_cmd_tick.store(xTaskGetTickCount(), std::memory_order_relaxed);
    }
}

/* ── Task: CAN Receive (prio 5, event-driven) ─────────────────────── */

/**
 * Polls the CAN peripheral for incoming frames at a high rate.
 * Dispatches each frame via process_can_frame() — no queue needed
 * since the STM32 bxCAN hardware FIFO provides buffering.
 *
 * In a production implementation this task would pend on a semaphore
 * from the CAN RX interrupt (HAL_CAN_RxFifo0MsgPendingCallback).
 * For simplicity the polling variant is shown — the delay is kept short
 * (2 ms) so the bus is serviced faster than the fastest periodic message
 * (100 Hz = 10 ms period).
 */
void task_can_rx(void* pvParameters) {
    (void)pvParameters;
    can::Frame frame;

    for (;;) {
        if (mtr::g_can.receive(frame, 0)) {
            process_can_frame(frame);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/* ── Task: Safety Monitor (prio 5, 20 Hz) ─────────────────────────── */

/**
 * Monitors two independent safety inputs:
 *   1. ESTOP GPIO — NC, active-low. If LOW, forces ESTOP active.
 *   2. CAN 0x204 staleness — if no command received for >200 ms in AUTO,
 *      sets the stale flag so the control loop uses zero speed + neutral.
 *
 * Frequency: 20 Hz (50 ms period) — sufficient for human-scale button
 * response and the 200 ms staleness window.
 */
void task_safety(void* pvParameters) {
    (void)pvParameters;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t startup_end_tick = last_wake + pdMS_TO_TICKS(mtr::kStartupGracePeriodMs);

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000 / mtr::kSafetyCheckHz));

        TickType_t now = xTaskGetTickCount();

        /* ── Startup grace period ── */
        if (now >= startup_end_tick) {
            g_startup_grace.store(false, std::memory_order_relaxed);
        }

        /* ── 1. ESTOP GPIO check ── */
        /* Physical ESTOP button (NC, active-low). When pressed:
         *   - Hardware directly kills throttle/gear (Level 3)
         *   - This firmware also detects it for CAN feedback (0x206)
         */
        if (estop_gpio_pressed()) {
            g_estop_active.store(true, std::memory_order_relaxed);
        }

        /* ── 2. CAN 0x204 staleness check ── */
        can::Mode mode = g_mode.load(std::memory_order_relaxed);
        if (!g_startup_grace.load(std::memory_order_relaxed) &&
            mode == can::Mode::Auto) {
            uint32_t last_tick = g_last_cmd_tick.load(std::memory_order_relaxed);
            uint32_t elapsed = now - last_tick;
            if (elapsed > pdMS_TO_TICKS(mtr::kCmdStaleTimeoutMs)) {
                /* Stale: zero command so control loop sees safe values */
                g_cmd_speed_mmps.store(0, std::memory_order_relaxed);
                g_cmd_gear.store(0, std::memory_order_relaxed);
                /* Set stale fault flag */
                uint8_t ff = g_fault_flags.load(std::memory_order_relaxed);
                ff |= mtr::kFaultCmdTimeout;
                g_fault_flags.store(ff, std::memory_order_relaxed);
            } else {
                /* Clear stale fault flag when commands resume */
                uint8_t ff = g_fault_flags.load(std::memory_order_relaxed);
                ff &= ~mtr::kFaultCmdTimeout;
                g_fault_flags.store(ff, std::memory_order_relaxed);
            }
        }
    }
}

/* ── Task: Motor Control (prio 4, 100 Hz) ──────────────────────────── */

/**
 * Main motor control loop — runs at 100 Hz (10 ms period).
 *
 * Mode-gated behavior (§4.0 architecture.md):
 *
 *   MANUAL (mode=0):
 *     - Read throttle ADC → MCP4725 DAC (pass-through)
 *     - Read TLP281 gear sense → gear relays (pass-through)
 *
 *   AUTO (mode=1):
 *     - Follow CAN 0x204 RT_MotorSpeed → MCP4725 DAC
 *     - Follow CAN 0x204 RT_Gear → gear relays
 *     - If 0x204 stale (>200 ms since last frame): speed=0, gear=N
 *
 *   ESTOP (mode=2):
 *     - DAC = 0 (cut throttle)
 *     - All gear relays off
 */
void task_control(void* pvParameters) {
    (void)pvParameters;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000 / mtr::kControlLoopHz));

        bool     estop   = g_estop_active.load(std::memory_order_relaxed);
        can::Mode mode   = g_mode.load(std::memory_order_relaxed);

        /* ── Handle ESTOP ── */
        if (estop || mode == can::Mode::Estop) {
            mtr::g_dac.write(0);                  // Cut throttle
            mtr::g_gear.all_off();                // All relays off → N

            g_actual_speed_mmps.store(0, std::memory_order_relaxed);
            g_current_gear.store(static_cast<uint8_t>(can::Gear::N),
                                 std::memory_order_relaxed);

            /* Set ESTOP_ACTIVE fault flag */
            uint8_t ff = g_fault_flags.load(std::memory_order_relaxed);
            ff |= mtr::kFaultEstopActive;
            g_fault_flags.store(ff, std::memory_order_relaxed);

            continue;
        }

        /* ── Clear ESTOP fault bit when not in ESTOP ── */
        {
            uint8_t ff = g_fault_flags.load(std::memory_order_relaxed);
            ff &= ~mtr::kFaultEstopActive;
            g_fault_flags.store(ff, std::memory_order_relaxed);
        }

        /* ── Manual mode: pass-through ── */
        if (mode == can::Mode::Manual) {
            /* Read throttle ADC → compute speed */
            uint16_t raw_adc = mtr::g_throttle.read_raw();
            int16_t speed = mtr::g_throttle.tick(raw_adc);

            /* Write to DAC */
            mtr::g_dac.set_speed_mmps(speed);

            /* Read TLP281 gear sense → mirror to relays */
            mtr::g_gear.pass_through();

            /* Publish for CAN TX tasks */
            g_actual_speed_mmps.store(speed, std::memory_order_relaxed);
            g_current_gear.store(
                static_cast<uint8_t>(mtr::g_gear.current_gear()),
                std::memory_order_relaxed);

            continue;
        }

        /* ── Auto mode: follow CAN 0x204 ── */
        if (mode == can::Mode::Auto) {
            int32_t  cmd_speed = g_cmd_speed_mmps.load(std::memory_order_relaxed);
            uint8_t  cmd_gear  = g_cmd_gear.load(std::memory_order_relaxed);
            uint32_t last_tick = g_last_cmd_tick.load(std::memory_order_relaxed);
            uint32_t now       = xTaskGetTickCount();
            bool stale         = (!g_startup_grace.load(std::memory_order_relaxed))
                               && (now - last_tick > pdMS_TO_TICKS(mtr::kCmdStaleTimeoutMs));

            if (stale) {
                cmd_speed = 0;
                cmd_gear  = static_cast<uint8_t>(can::Gear::N);
            }

            /* Write DAC */
            mtr::g_dac.set_speed_mmps(cmd_speed);

            /* Set gear relays */
            mtr::g_gear.set_relays(static_cast<can::Gear>(cmd_gear & 0x03));

            /* Publish for CAN TX tasks */
            g_actual_speed_mmps.store(
                static_cast<int16_t>(cmd_speed > 32767 ? 32767 :
                                    (cmd_speed < -32768 ? -32768 : cmd_speed)),
                std::memory_order_relaxed);
            g_current_gear.store(cmd_gear & 0x03, std::memory_order_relaxed);

            continue;
        }
    }
}

/* ── Task: CAN Transmit (prio 3, 100 Hz base) ─────────────────────── */

/**
 * Periodically transmits two CAN messages at different rates:
 *   - 0x120 SYS_THROTTLE_STS @ 100 Hz (every cycle)
 *   - 0x206 MTR_MOTOR_FBK  @ 50 Hz  (every other cycle)
 *
 * Loops at 100 Hz. A cycle counter selects which messages to send.
 */
void task_can_tx(void* pvParameters) {
    (void)pvParameters;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t cycle = 0;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000 / mtr::kCanTxLoopHz));

        /* Read shared state once per cycle */
        int16_t actual_speed = g_actual_speed_mmps.load(std::memory_order_relaxed);
        uint8_t gear_state   = g_current_gear.load(std::memory_order_relaxed);
        uint8_t fault_flags  = g_fault_flags.load(std::memory_order_relaxed);

        can::Frame tx;

        /* ── 0x120 SYS_THROTTLE_STS @ 100 Hz (every cycle) ── */
        can::SysThrottleSts throttle_sts;
        throttle_sts.speed_mmps = actual_speed;
        throttle_sts.to_frame(tx);
        mtr::g_can.send(tx);

        /* ── 0x206 MTR_MOTOR_FBK @ 50 Hz (every 2nd cycle) ── */
        if ((cycle & 1) == 0) {
            can::MtrMotorFbk fbk;
            fbk.actual_speed_mmps = actual_speed;
            fbk.gear_state        = gear_state;
            fbk.fault_flags       = fault_flags;
            fbk.to_frame(tx);
            mtr::g_can.send(tx);
        }

        cycle++;
    }
}

/* ── FreeRTOS task handles ────────────────────────────────────────── */

static TaskHandle_t s_task_can_rx   = nullptr;
static TaskHandle_t s_task_safety   = nullptr;
static TaskHandle_t s_task_control  = nullptr;
static TaskHandle_t s_task_can_tx   = nullptr;

/* ── Application entry point ──────────────────────────────────────── */

/**
 * STM32 + FreeRTOS entry point.
 *
 * Prerequisites (set up by STM32CubeMX-generated code before main):
 *   - HAL_Init()
 *   - SystemClock_Config()
 *   - MX_GPIO_Init()
 *   - MX_I2C1_Init()
 *   - MX_ADC1_Init()
 *   - MX_CAN_Init()
 *
 * This function initialises the MTR-specific drivers and creates all
 * FreeRTOS tasks before starting the scheduler.
 */
int main(void) {
    /* ── STM32 HAL + peripheral init (CubeMX-generated) ── */
    /* HAL_Init(); */
    /* SystemClock_Config(); */
    /* MX_GPIO_Init(); */
    /* MX_I2C1_Init(); */
    /* MX_ADC1_Init(); */
    /* MX_CAN_Init(); */

    /* ── MTR module init ── */
    mtr::g_dac.init();           // DAC starts at 0 V
    mtr::g_throttle.init();      // ADC ready
    mtr::g_gear.init();          // All relays OFF
    mtr::g_can.init();           // bxCAN started + RX interrupt armed

    /* ── Create FreeRTOS tasks ── */

    xTaskCreate(
        task_can_rx,             // task function
        "can_rx",                // name
        256,                     // stack size (words)
        nullptr,                 // parameter
        5,                       // priority
        &s_task_can_rx           // handle
    );

    xTaskCreate(
        task_safety,
        "safety",
        192,
        nullptr,
        5,
        &s_task_safety
    );

    xTaskCreate(
        task_control,
        "control",
        256,
        nullptr,
        4,
        &s_task_control
    );

    xTaskCreate(
        task_can_tx,
        "can_tx",
        256,
        nullptr,
        3,
        &s_task_can_tx
    );

    /* ── Start the FreeRTOS scheduler ── */
    vTaskStartScheduler();

    /* Scheduler should never return; if it does, infinite loop */
    for (;;) { }
}
