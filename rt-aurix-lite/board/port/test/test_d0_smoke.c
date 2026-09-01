/* D0 walking-skeleton smoke test: proves the HighTec tricore-gcc toolchain
 * + TC37x iLLD register headers + LQFP-176 pin maps compile and link
 * headlessly (no AURIX Studio wizard required).
 *
 * This is a COMPILE/LINK proof, not a runtime test. It verifies:
 *   - DEVICE_TC37X / IFX_PIN_PACKAGE_LQFP176 select the TC375 SFR + pinmap
 *   - IfxCan / IfxPort register headers + the CAN pin table symbols link
 */

#include <stdint.h>

#include "Ifx_Cfg.h"             // DEVICE_TC37X + IFX_PIN_PACKAGE_LQFP176
#include "IfxCan_reg.h"          // TC37x CAN register block
#include "IfxPort_reg.h"         // TC37x port register block
#include "IfxCan_PinMap_TC37x_LQFP176.h"  // CAN0 node0/node2 pin tables

// Reference the pin table symbols so the linker pulls in the .c tables.
extern const IfxCan_Txd_Out *IfxCan_Txd_Out_pinTable[2][4][5];
extern const IfxCan_Rxd_In  *IfxCan_Rxd_In_pinTable[2][4][5];

int main(void)
{
    // Freeze the CAN bindings from the iLLD pinmap (architecture.md §9.1).
    const IfxCan_Txd_Out *low_tx  = &IfxCan_TXD00_P20_8_OUT;   // CAN0 Node0
    const IfxCan_Rxd_In  *low_rx  = &IfxCan_RXD00B_P20_7_IN;   // CAN0 Node0
    const IfxCan_Txd_Out *high_tx = &IfxCan_TXD02_P15_0_OUT;   // CAN0 Node2
    const IfxCan_Rxd_In  *high_rx = &IfxCan_RXD02A_P15_1_IN;   // CAN0 Node2

    // Verify the pin maps resolve (module/nodeId/port/pin).
    if (low_tx->nodeId != IfxCan_NodeId_0 || low_rx->nodeId != IfxCan_NodeId_0 ||
        high_tx->nodeId != IfxCan_NodeId_2 || high_rx->nodeId != IfxCan_NodeId_2) {
        return 1;
    }

    // Refer to the tables to force linkage.
    if (IfxCan_Txd_Out_pinTable == 0 || IfxCan_Rxd_In_pinTable == 0) {
        return 2;
    }

    // Touch the CAN0 module base (read-only) to prove the SFR symbol resolves.
    volatile uint32_t *can_base = (volatile uint32_t *)&MODULE_CAN0;
    (void)can_base;

    return 0;
}
