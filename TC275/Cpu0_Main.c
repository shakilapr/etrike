/**********************************************************************************************************************
 * \file Cpu0_Main.c
 * \brief CAN Motor Controller - CPU0 (Master Core)
 *
 * Responsibilities:
 *   - System initialization: Clock, Watchdog, GPIO, QSPI, MultiCAN
 *   - DAC8562 SPI write via QSPI module (16-bit, 3-wire, MSB first)
 *   - 74HC139 2-to-4 decoder control (channel select + enable)
 *   - CAN frame reception (ID 0x100, 500 kbps) via TLE9251VSJ
 *   - LED heartbeat blink on P00.1
 *
 * CAN Frame Format (8 bytes, ID: 0x100):
 *   Byte 0   : Channel select (0=CH0, 1=CH1, 2=CH2, 3=CH3)
 *   Byte 1   : DAC value HIGH byte (bits [15:8])
 *   Byte 2   : DAC value LOW byte  (bits [7:0])
 *   Bytes 3-7: Reserved
 *
 * DAC8562 Command format (24-bit SPI):
 *   [23:20] = 0b0011 (Write & Update DAC A)
 *   [19:4]  = 16-bit DAC value
 *   [3:0]   = 0b0000
 *
 * Hardware Pin Map (TC275 LQFP-176):
 *   QSPI SCLK   : P10.2 (Pin 170)
 *   QSPI MOSI   : P10.3 (Pin 171)
 *   QSPI CS/SYNC: P10.5 (Pin 173)  -> TXB0104 A2 -> DAC8562 SYNC
 *   CAN_TX      : P20.8 (Pin 126)  -> TLE9251VSJ TXD
 *   CAN_RX      : P20.7 (Pin 125)  -> TLE9251VSJ RXD
 *   CAN_STBY    : P20.6 (Pin 124)  -> TLE9251VSJ STB (active HIGH = standby)
 *   HC139_EN    : P00.7 (Pin 18)   -> SN74HC139 1G# (active LOW enable)
 *   HC139_A     : P00.6 (Pin 17)   -> SN74HC139 1A
 *   HC139_B     : P00.5 (Pin 16)   -> SN74HC139 1B
 *   LED         : P00.1 (Pin 4)    -> 330R -> LED -> GND
 *********************************************************************************************************************/

#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxPort.h"
#include "IfxQspi.h"
#include "IfxQspi_SpiMaster.h"
#include "IfxMultican.h"
#include "IfxMultican_Can.h"
#include "IfxStm.h"

/* ============================================================
 * HARDWARE PIN DEFINITIONS
 * ============================================================ */

/* Status LED */
#define LED_PORT            &MODULE_P00
#define LED_PIN             1

/* 74HC139 Decoder Pins */
#define HC139_EN_PORT       &MODULE_P00
#define HC139_EN_PIN        7    /* 1G# - active LOW */
#define HC139_A_PORT        &MODULE_P00
#define HC139_A_PIN         6    /* 1A */
#define HC139_B_PORT        &MODULE_P00
#define HC139_B_PIN         5    /* 1B */

/* CAN Standby Pin */
#define CAN_STBY_PORT       &MODULE_P20
#define CAN_STBY_PIN        6    /* LOW = normal operation */

/* DAC8562 SPI Command Words */
#define DAC8562_CMD_WRITE_UPDATE_A   (0x30)   /* Write & Update DAC-A: bits[23:20]=0011 */
#define DAC8562_CMD_WRITE_UPDATE_B   (0x34)   /* Write & Update DAC-B: bits[23:20]=0011, addr=01 */
#define DAC8562_CMD_RESET            (0x70)   /* Software reset */
#define DAC8562_ENABLE_INTERNAL_REF  (0x80)   /* Enable internal 2.5V reference (gain=2 -> 5V FS) */

/* CAN Configuration */
#define CAN_RX_ID           0x100
#define CAN_BAUD_RATE       500000UL   /* 500 kbps */
#define CAN_NODE            0          /* Use CAN node 0 (MultiCAN module node 0) */

/* ============================================================
 * GLOBAL STATE & HANDLES
 * ============================================================ */

IfxCpu_syncEvent cpuSyncEvent = 0;

/* QSPI SPI Master handle */
static IfxQspi_SpiMaster         g_spiMaster;
static IfxQspi_SpiMaster_Channel g_spiChannel;

/* MultiCAN handles */
static IfxMultican_Can            g_can;
static IfxMultican_Can_Node       g_canNode;
static IfxMultican_Can_MsgObj     g_rxMsgObj;

/* Shared data from CAN frame */
static volatile uint16            g_dacValue    = 0;      /* 0–65535, maps to 0–5V DAC output */
static volatile uint8             g_channelSel  = 0;      /* 0–3: which opto channel is active */
static volatile boolean           g_newFrame    = FALSE;  /* Set by ISR when new CAN frame arrives */

/* ============================================================
 * FORWARD DECLARATIONS
 * ============================================================ */
static void  initGpio(void);
static void  initQspiDac(void);
static void  initMultican(void);
static void  dac8562Write(uint16 value);
static void  hc139SetChannel(uint8 channel);
static void  hc139Enable(boolean enable);
static void  delayMs(uint32 ms);

/* ============================================================
 * CAN RX INTERRUPT
 * Triggered by MultiCAN when a frame matching ID 0x100 arrives.
 * ============================================================ */
IFX_INTERRUPT(canRxIsr, 0, 10)
{
    IfxMultican_Message rxMsg;
    IfxMultican_Status  status;

    status = IfxMultican_Can_MsgObj_readMessage(&g_rxMsgObj, &rxMsg);

    if (status == IfxMultican_Status_newData)
    {
        /* CAN frame payload (8 bytes packed into two 32-bit words by iLLD):
         *   rxMsg.data[0] lower byte = Byte 0 (channel select)
         *   rxMsg.data[0] next byte  = Byte 1 (DAC high byte)
         *   rxMsg.data[0] next byte  = Byte 2 (DAC low byte)
         */
        uint8  chSel   = (uint8)((rxMsg.data[0] >>  0) & 0xFF);
        uint8  dacHigh = (uint8)((rxMsg.data[0] >>  8) & 0xFF);
        uint8  dacLow  = (uint8)((rxMsg.data[0] >> 16) & 0xFF);

        g_channelSel = (chSel > 3) ? 0 : chSel;
        g_dacValue   = ((uint16)dacHigh << 8) | (uint16)dacLow;
        g_newFrame   = TRUE;
    }
}

/* ============================================================
 * CORE 0 MAIN ENTRY POINT
 * ============================================================ */
void core0_main(void)
{
    IfxCpu_enableInterrupts();

    /* Disable watchdogs (re-enable and service in production) */
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    /* Sync all 3 cores before starting init */
    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    /* --- Peripheral Initialization --- */
    initGpio();
    initQspiDac();
    initMultican();

    /* --- DAC8562 Startup Sequence --- */
    /* 1. Software reset */
    dac8562Write(DAC8562_CMD_RESET << 8);
    delayMs(1);

    /* 2. Enable internal 2.5V reference with gain=2 (full-scale = 5V) */
    dac8562Write(DAC8562_ENABLE_INTERNAL_REF << 8);
    delayMs(1);

    /* 3. Set DAC output to 0V on startup */
    dac8562Write(0x0000);

    /* --- Safe initial state --- */
    hc139Enable(FALSE);          /* Disable all outputs on startup */
    hc139SetChannel(0);          /* Default to channel 0 */
    IfxPort_setPinState(LED_PORT, LED_PIN, IfxPort_State_low);

    /* --- Main Loop --- */
    uint32 ledCounter = 0;

    while (1)
    {
        /* Process incoming CAN command */
        if (g_newFrame)
        {
            g_newFrame = FALSE;

            /* 1. Disable outputs during switching (break-before-make) */
            hc139Enable(FALSE);
            delayMs(1);

            /* 2. Update DAC voltage output */
            dac8562Write(g_dacValue);

            /* 3. Select the correct opto channel */
            hc139SetChannel(g_channelSel);

            /* 4. Re-enable output */
            hc139Enable(TRUE);
        }

        /* LED heartbeat: toggle every ~500ms */
        ledCounter++;
        if (ledCounter >= 5000)
        {
            ledCounter = 0;
            IfxPort_togglePin(LED_PORT, LED_PIN);
        }

        delayMs(1);
    }
}

/* ============================================================
 * GPIO INITIALIZATION
 * Configures all output pins to safe default states.
 * ============================================================ */
static void initGpio(void)
{
    /* LED output - push-pull, initially OFF */
    IfxPort_setPinModeOutput(LED_PORT, LED_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    IfxPort_setPinState(LED_PORT, LED_PIN, IfxPort_State_low);

    /* 74HC139 ENABLE pin (active LOW) - HIGH on startup = all outputs disabled */
    IfxPort_setPinModeOutput(HC139_EN_PORT, HC139_EN_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    IfxPort_setPinState(HC139_EN_PORT, HC139_EN_PIN, IfxPort_State_high); /* Disable */

    /* 74HC139 Address pins */
    IfxPort_setPinModeOutput(HC139_A_PORT, HC139_A_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(HC139_B_PORT, HC139_B_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    IfxPort_setPinState(HC139_A_PORT, HC139_A_PIN, IfxPort_State_low);
    IfxPort_setPinState(HC139_B_PORT, HC139_B_PIN, IfxPort_State_low);

    /* CAN Standby pin - LOW = normal (active) mode */
    IfxPort_setPinModeOutput(CAN_STBY_PORT, CAN_STBY_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    IfxPort_setPinState(CAN_STBY_PORT, CAN_STBY_PIN, IfxPort_State_low); /* Wake transceiver */
}

/* ============================================================
 * QSPI SPI MASTER INIT (for DAC8562)
 * QSPI2 module: SCLK=P10.2, MOSI=P10.3, CS=P10.5
 * Mode 1 (CPOL=0, CPHA=1), 16-bit per word, MSB first, ~10MHz
 * ============================================================ */
static void initQspiDac(void)
{
    /* QSPI Master configuration */
    IfxQspi_SpiMaster_Config masterConfig;
    IfxQspi_SpiMaster_initModuleConfig(&masterConfig, &MODULE_QSPI2);

    /* Clock config: fQSPI = 200MHz / 20 = 10MHz */
    masterConfig.base.mode             = SpiIf_Mode_master;
    masterConfig.base.maximumBaudrate  = 10000000;

    /* DMA not used - CPU polling mode for simplicity */
    masterConfig.dma.useDma = FALSE;

    IfxQspi_SpiMaster_initModule(&g_spiMaster, &masterConfig);

    /* Channel configuration for DAC8562 */
    IfxQspi_SpiMaster_ChannelConfig chConfig;
    IfxQspi_SpiMaster_initChannelConfig(&chConfig, &g_spiMaster);

    chConfig.base.baudrate      = 10000000;   /* 10 MHz */
    chConfig.base.transferStart = SpiIf_TransferStart_withinFifo;
    chConfig.base.mode.clockPolarity = SpiIf_ClockPolarity_idleLow;   /* CPOL=0 */
    chConfig.base.mode.shiftClock    = SpiIf_ShiftClock_fallingEdge;  /* CPHA=1 */
    chConfig.base.mode.dataWidth     = 8;   /* 8-bit per transfer; 3 transfers = 24-bit DAC word */
    chConfig.base.mode.csLeadDelay  = SpiIf_SlsoDelay_4;
    chConfig.base.mode.csTrailDelay = SpiIf_SlsoDelay_4;
    chConfig.base.mode.csInactiveDelay = SpiIf_SlsoDelay_4;

    /* Pin assignments: QSPI2 SCLK=P10.2, MOSI=P10.3, CS0=P10.5 */
    chConfig.sclk.pin    = &IfxQspi2_SCLK_P10_2_OUT;
    chConfig.pin.mosi.pin = &IfxQspi2_MTSR_P10_3_OUT;
    chConfig.pin.slso[0].pin = &IfxQspi2_SLSO0_P10_5_OUT;

    chConfig.pin.mosi.driver = IfxPort_PadDriver_cmosAutomotiveSpeed3;
    chConfig.pin.slso[0].driver = IfxPort_PadDriver_cmosAutomotiveSpeed3;

    IfxQspi_SpiMaster_initChannel(&g_spiChannel, &chConfig);
}

/* ============================================================
 * MULTICAN INITIALIZATION (CAN Node 0, 500 kbps)
 * CAN_TX=P20.8, CAN_RX=P20.7
 * ============================================================ */
static void initMultican(void)
{
    /* CAN module config */
    IfxMultican_Can_Config canConfig;
    IfxMultican_Can_initModuleConfig(&canConfig, &MODULE_CAN);
    IfxMultican_Can_initModule(&g_can, &canConfig);

    /* CAN node 0 config */
    IfxMultican_Can_NodeConfig nodeConfig;
    IfxMultican_Can_Node_initConfig(&nodeConfig, &g_can);

    nodeConfig.nodeId          = IfxMultican_NodeId_0;
    nodeConfig.baudrate        = CAN_BAUD_RATE;
    nodeConfig.rxPin           = &IfxMultican_RXD0B_P20_7_IN;    /* P20.7 */
    nodeConfig.rxPinMode       = IfxPort_InputMode_pullUp;
    nodeConfig.txPin           = &IfxMultican_TXD0_P20_8_OUT;    /* P20.8 */
    nodeConfig.txPinMode       = IfxPort_OutputMode_pushPull;

    IfxMultican_Can_Node_init(&g_canNode, &nodeConfig);

    /* RX Message Object: accept only CAN ID 0x100 */
    IfxMultican_Can_MsgObjConfig rxMsgConfig;
    IfxMultican_Can_MsgObj_initConfig(&rxMsgConfig, &g_canNode);

    rxMsgConfig.msgObjId       = 0;
    rxMsgConfig.messageId      = CAN_RX_ID;
    rxMsgConfig.frame          = IfxMultican_Frame_receive;
    rxMsgConfig.acceptanceMask = 0x7FF;     /* Exact match on all 11-bit ID bits */
    rxMsgConfig.control.messageLen = IfxMultican_DataLengthCode_8;

    /* Attach ISR */
    rxMsgConfig.rxInterrupt.enabled = TRUE;
    rxMsgConfig.rxInterrupt.srcId   = IfxMultican_SrcId_0;
    rxMsgConfig.rxInterrupt.isrPriority = 10;
    rxMsgConfig.rxInterrupt.isrProvider = IfxSrc_Tos_cpu0;

    IfxMultican_Can_MsgObj_init(&g_rxMsgObj, &rxMsgConfig);
}

/* ============================================================
 * DAC8562 WRITE
 * Sends a 24-bit command over SPI (SYNC low during transfer).
 * To write & update DAC-A channel:
 *   Byte0 = 0x30 | addr(0), Byte1 = value[15:8], Byte2 = value[7:0]
 * ============================================================ */
static void dac8562Write(uint16 value)
{
    uint8 txBuf[3];
    txBuf[0] = DAC8562_CMD_WRITE_UPDATE_A; /* Command: Write & Update DAC-A */
    txBuf[1] = (uint8)((value >> 8) & 0xFF); /* High byte */
    txBuf[2] = (uint8)((value >> 0) & 0xFF); /* Low byte  */

    /* Block until SPI bus is free, then transmit */
    while (IfxQspi_SpiMaster_getStatus(&g_spiChannel) == SpiIf_Status_busy) {}
    IfxQspi_SpiMaster_exchange(&g_spiChannel, txBuf, NULL_PTR, 3);
    while (IfxQspi_SpiMaster_getStatus(&g_spiChannel) == SpiIf_Status_busy) {}
}

/* ============================================================
 * 74HC139 CHANNEL SELECT
 * channel: 0–3 maps to address lines 1A, 1B
 *   0 -> A=0, B=0  -> 1Y0 LOW (opto channel 0 ON)
 *   1 -> A=1, B=0  -> 1Y1 LOW (opto channel 1 ON)
 *   2 -> A=0, B=1  -> 1Y2 LOW (opto channel 2 ON)
 *   3 -> A=1, B=1  -> 1Y3 LOW (opto channel 3 ON)
 * ============================================================ */
static void hc139SetChannel(uint8 channel)
{
    IfxPort_setPinState(HC139_A_PORT, HC139_A_PIN,
                        (channel & 0x01) ? IfxPort_State_high : IfxPort_State_low);
    IfxPort_setPinState(HC139_B_PORT, HC139_B_PIN,
                        (channel & 0x02) ? IfxPort_State_high : IfxPort_State_low);
}

/* ============================================================
 * 74HC139 ENABLE / DISABLE
 * The 1G# pin is active LOW: drive LOW to enable outputs.
 * ============================================================ */
static void hc139Enable(boolean enable)
{
    IfxPort_setPinState(HC139_EN_PORT, HC139_EN_PIN,
                        enable ? IfxPort_State_low : IfxPort_State_high);
}

/* ============================================================
 * MILLISECOND DELAY (STM-based, CPU0)
 * Uses System Timer Module (STM0) ticks.
 * At 200MHz CPU: 1ms = 200,000 ticks
 * ============================================================ */
static void delayMs(uint32 ms)
{
    uint32 ticks = IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, ms);
    IfxStm_waitTicks(BSP_DEFAULT_TIMER, ticks);
}
