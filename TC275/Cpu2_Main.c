/**********************************************************************************************************************
 * \file Cpu2_Main.c
 * \brief CAN Motor Controller - CPU2 (Hardware Safety Monitor)
 *
 * Responsibilities:
 *   - Reads the CAN timeout flag from CPU1 (g_canTimeout)
 *   - On timeout: forces the 74HC139 ENABLE pin HIGH (disabling ALL
 *     opto-isolated high-side switches) as a hardware-level safety action.
 *   - This is a redundant safety layer that does NOT rely on CPU0's
 *     main loop being functional.
 *
 * Note: CPU2 directly drives the HC139 ENABLE pin as an independent
 * safety override, bypassing CPU0's normal control path.
 *********************************************************************************************************************/

#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxPort.h"
#include "IfxStm.h"

/* ============================================================
 * HARDWARE PIN (mirrors CPU0 definition)
 * ============================================================ */
#define HC139_EN_PORT       &MODULE_P00
#define HC139_EN_PIN        7    /* 1G# - active LOW - drive HIGH to disable all channels */

/* ============================================================
 * SHARED STATE (from CPU1)
 * ============================================================ */
extern volatile boolean g_canTimeout;
extern IfxCpu_syncEvent cpuSyncEvent;

/* ============================================================
 * CORE 2 MAIN ENTRY POINT
 * ============================================================ */
void core2_main(void)
{
    IfxCpu_enableInterrupts();

    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());

    /* Wait for CPU sync event from CPU0 */
    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    while (1)
    {
        if (g_canTimeout)
        {
            /* === SAFETY SHUTDOWN ===
             * CAN bus silence detected. Immediately disable all
             * motor outputs by pulling HC139 ENABLE HIGH.
             * This is a hardware interlock action. */
            IfxPort_setPinModeOutput(HC139_EN_PORT, HC139_EN_PIN,
                                     IfxPort_OutputMode_pushPull,
                                     IfxPort_OutputIdx_general);
            IfxPort_setPinState(HC139_EN_PORT, HC139_EN_PIN,
                                IfxPort_State_high); /* Disable all outputs */
        }

        /* Check every 5ms */
        uint32 pollTicks = IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, 5);
        IfxStm_waitTicks(BSP_DEFAULT_TIMER, pollTicks);
    }
}
