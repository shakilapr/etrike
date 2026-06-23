/**********************************************************************************************************************
 * \file Cpu1_Main.c
 * \brief CAN Motor Controller - CPU1 (CAN Monitoring / Diagnostic Core)
 *
 * Responsibilities:
 *   - Monitors shared state variables written by CPU0
 *   - Implements a CAN watchdog: if no new CAN frame arrives within
 *     WATCHDOG_TIMEOUT_MS, it signals CPU0 to disable all motor outputs
 *     (safety shutdown by timeout).
 *   - Could be extended for CANopen NMT heartbeat monitoring.
 *
 * Communication with CPU0 via shared memory variables:
 *   g_newFrame    - set TRUE by CPU0's CAN ISR when new data arrives
 *   g_canTimeout  - set TRUE by CPU1 when watchdog expires; CPU0 reads this
 *********************************************************************************************************************/

#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxStm.h"

/* ============================================================
 * SHARED STATE (defined in Cpu0_Main.c)
 * ============================================================ */
extern volatile boolean g_newFrame;

/* ============================================================
 * SHARED WATCHDOG FLAG (read by CPU0 main loop)
 * ============================================================ */
volatile boolean g_canTimeout = FALSE;

/* Timeout threshold: if no CAN frame received for 500ms, shutdown */
#define WATCHDOG_TIMEOUT_MS   500

/* Sync event shared with all cores */
extern IfxCpu_syncEvent cpuSyncEvent;

/* ============================================================
 * CORE 1 MAIN ENTRY POINT
 * ============================================================ */
void core1_main(void)
{
    IfxCpu_enableInterrupts();

    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());

    /* Wait for CPU sync event from CPU0 */
    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    uint32 lastFrameTime = IfxStm_getLower(BSP_DEFAULT_TIMER);

    while (1)
    {
        /* Check if CPU0's CAN ISR has received a new frame */
        if (g_newFrame)
        {
            /* Reset watchdog timer */
            lastFrameTime = IfxStm_getLower(BSP_DEFAULT_TIMER);
            g_canTimeout  = FALSE;
        }
        else
        {
            /* Check elapsed time since last CAN frame */
            uint32 now     = IfxStm_getLower(BSP_DEFAULT_TIMER);
            uint32 elapsed = now - lastFrameTime;
            uint32 timeout = IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER,
                                                              WATCHDOG_TIMEOUT_MS);

            if (elapsed > timeout)
            {
                /* No frame received within timeout window -> signal CPU0 to shut down */
                g_canTimeout = TRUE;
            }
        }

        /* Poll at ~10ms intervals */
        uint32 pollTicks = IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, 10);
        IfxStm_waitTicks(BSP_DEFAULT_TIMER, pollTicks);
    }
}
