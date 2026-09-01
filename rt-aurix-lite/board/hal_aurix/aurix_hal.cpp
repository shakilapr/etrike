// AURIX board HAL — implements the portable rta::hal interfaces over iLLD
// for the KIT_A2G_TC375_LITE (TC375, LQFP-176).
//
// Phase D0/D2. This glue tells the iLLD which pins/modules to use:
//   CAN_LOW  : CAN0 Node 0 — P20.8 (TX, alt5), P20.7 (RX, RxSel_b), P20.6 STB
//   CAN_HIGH : CAN0 Node 2 — P15.0 (TX, alt5), P15.1 (RX, RxSel_a)
// Both at 500 kbit/s.

#include "board/hal_aurix/aurix_hal.h"

#include "IfxCan.h"          // CAN0/CAN1 module instances
#include "IfxCan_Can.h"
#include "IfxPort.h"
#include "IfxPort_reg.h"
#include "IfxStm.h"
#include "IfxStm_reg.h"
#include "IfxCpu.h"

namespace rta::board {

namespace {

// One handle per CAN module (CAN0 carries both nodes).
IfxCan_Can g_can0;

// Node handles.
IfxCan_Can_Node g_node_low;   // CAN0 Node 0
IfxCan_Can_Node g_node_high;  // CAN0 Node 2

// ── Clock / STM ─────────────────────────────────────────────────────
const Ifx_STM *g_stm = &MODULE_STM0;

}  // namespace

// ── Clock ───────────────────────────────────────────────────────────
void AurixClock::init() {
    // STM0 runs from the system clock; getLower() gives a 32-bit tick.
    // AURIX Studio template enables STM0 by default; nothing to init beyond
    // ensuring the module is on. g_stm is set above.
    g_stm = &MODULE_STM0;
}

rta::TimeUs AurixClock::monotonic_us() {
    // 32-bit STM tick at f_STM (300 MHz). Convert to microseconds.
    // f_STM is set in IfxScuCcu (default 300 MHz after PLL). This is a
    // coarse conversion; refine with the actual f_STM in D0.
    constexpr uint64_t kFStmHz = 300'000'000ull;
    return static_cast<rta::TimeUs>((uint64_t)IfxStm_getLower((Ifx_STM *)g_stm) * 1'000'000ull / kFStmHz);
}

// ── GPIO ────────────────────────────────────────────────────────────
void AurixGpio::init() {
    // Rider inputs — active-low, with pull-up.
    IfxPort_setPinMode(kEstop.port, kEstop.pinIndex, IfxPort_Mode_inputPullUp);
    IfxPort_setPinMode(kBrakeLever.port, kBrakeLever.pinIndex, IfxPort_Mode_inputPullUp);
    IfxPort_setPinMode(kStart.port, kStart.pinIndex, IfxPort_Mode_inputPullUp);
    IfxPort_setPinMode(kMode.port, kMode.pinIndex, IfxPort_Mode_inputPullUp);
    IfxPort_setPinMode(kSwLeft.port, kSwLeft.pinIndex, IfxPort_Mode_inputPullUp);
    IfxPort_setPinMode(kSwRight.port, kSwRight.pinIndex, IfxPort_Mode_inputPullUp);
    IfxPort_setPinMode(kSwHead.port, kSwHead.pinIndex, IfxPort_Mode_inputPullUp);

    // Relay outputs — push-pull, default low.
    IfxPort_setPinMode(kLightLeft.port, kLightLeft.pinIndex, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinMode(kLightRight.port, kLightRight.pinIndex, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinMode(kBrakeLight.port, kBrakeLight.pinIndex, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinMode(kHeadlight.port, kHeadlight.pinIndex, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinMode(kBulbAuto.port, kBulbAuto.pinIndex, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinMode(kBulbManual.port, kBulbManual.pinIndex, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinMode(kRelay12v.port, kRelay12v.pinIndex, IfxPort_Mode_outputPushPullGeneral);

    // WDI output (TPS3850-Q1).
    IfxPort_setPinMode(kWdtWdi.port, kWdtWdi.pinIndex, IfxPort_Mode_outputPushPullGeneral);

    // CAN_LOW standby (P20.6) — drive LOW to enable the transceiver.
    IfxPort_setPinMode(kCanLowStb.port, kCanLowStb.pinIndex, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinState(kCanLowStb.port, kCanLowStb.pinIndex, IfxPort_State_low);
}

bool AurixGpio::read(rta::hal::InputSignal sig) const {
    switch (sig) {
        case rta::hal::InputSignal::EstopBtn:    return !IfxPort_getPinState(kEstop.port, kEstop.pinIndex);
        case rta::hal::InputSignal::BrakeLever:  return !IfxPort_getPinState(kBrakeLever.port, kBrakeLever.pinIndex);
        case rta::hal::InputSignal::StartBtn:    return !IfxPort_getPinState(kStart.port, kStart.pinIndex);
        case rta::hal::InputSignal::ModeBtn:     return !IfxPort_getPinState(kMode.port, kMode.pinIndex);
        case rta::hal::InputSignal::SwLeftTurn:  return !IfxPort_getPinState(kSwLeft.port, kSwLeft.pinIndex);
        case rta::hal::InputSignal::SwRightTurn: return !IfxPort_getPinState(kSwRight.port, kSwRight.pinIndex);
        case rta::hal::InputSignal::SwHeadlight: return !IfxPort_getPinState(kSwHead.port, kSwHead.pinIndex);
    }
    return false;
}

void AurixGpio::write(rta::hal::OutputSignal sig, bool asserted) {
    IfxPort_State s = asserted ? IfxPort_State_high : IfxPort_State_low;
    switch (sig) {
        case rta::hal::OutputSignal::LightLeft:   IfxPort_setPinState(kLightLeft.port, kLightLeft.pinIndex, s); break;
        case rta::hal::OutputSignal::LightRight:  IfxPort_setPinState(kLightRight.port, kLightRight.pinIndex, s); break;
        case rta::hal::OutputSignal::BrakeLight:  IfxPort_setPinState(kBrakeLight.port, kBrakeLight.pinIndex, s); break;
        case rta::hal::OutputSignal::Headlight:   IfxPort_setPinState(kHeadlight.port, kHeadlight.pinIndex, s); break;
        case rta::hal::OutputSignal::BulbAuto:    IfxPort_setPinState(kBulbAuto.port, kBulbAuto.pinIndex, s); break;
        case rta::hal::OutputSignal::BulbManual:  IfxPort_setPinState(kBulbManual.port, kBulbManual.pinIndex, s); break;
        case rta::hal::OutputSignal::Relay12v:    IfxPort_setPinState(kRelay12v.port, kRelay12v.pinIndex, s); break;
    }
}

// ── CAN ─────────────────────────────────────────────────────────────
void AurixCan::init() {
    // Module config (CAN0).
    IfxCan_Can_Config canCfg;
    IfxCan_Can_initModuleConfig(&canCfg, &MODULE_CAN0);
    IfxCan_Can_initModule(&g_can0, &canCfg);

    // ── CAN_LOW: CAN0 Node 0 ─────────────────────────────────────────
    IfxCan_Can_NodeConfig nodeLowCfg;
    IfxCan_Can_initNodeConfig(&nodeLowCfg, &g_can0);
    nodeLowCfg.nodeId = IfxCan_NodeId_0;
    nodeLowCfg.baudRate.baudrate = 500'000;
    nodeLowCfg.baudRate.samplePoint = 80;   // 80% sample point (percent)
    static const IfxCan_Can_Pins lowPins = {
        .txPin     = &IfxCan_TXD00_P20_8_OUT,   // P20.8 alt5
        .txPinMode = IfxPort_OutputMode_pushPull,
        .rxPin     = &IfxCan_RXD00B_P20_7_IN,   // P20.7 RxSel_b
        .rxPinMode = IfxPort_InputMode_noPullDevice,
        .padDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1
    };
    nodeLowCfg.pins = &lowPins;
    IfxCan_Can_initNode(&g_node_low, &nodeLowCfg);

    // ── CAN_HIGH: CAN0 Node 2 ────────────────────────────────────────
    IfxCan_Can_NodeConfig nodeHighCfg;
    IfxCan_Can_initNodeConfig(&nodeHighCfg, &g_can0);
    nodeHighCfg.nodeId = IfxCan_NodeId_2;
    nodeHighCfg.baudRate.baudrate = 500'000;
    nodeHighCfg.baudRate.samplePoint = 80;
    static const IfxCan_Can_Pins highPins = {
        .txPin     = &IfxCan_TXD02_P15_0_OUT,   // P15.0 alt5
        .txPinMode = IfxPort_OutputMode_pushPull,
        .rxPin     = &IfxCan_RXD02A_P15_1_IN,   // P15.1 RxSel_a
        .rxPinMode = IfxPort_InputMode_noPullDevice,
        .padDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1
    };
    nodeHighCfg.pins = &highPins;
    IfxCan_Can_initNode(&g_node_high, &nodeHighCfg);
}

bool AurixCan::transmit(Bus b, const etrike::protocol::Frame& frame,
                        rta::hal::TxClass /*cls*/) {
    IfxCan_Can_Node *node = (b == Bus::Low) ? &g_node_low : &g_node_high;
    IfxCan_Message msg;
    IfxCan_Can_initMessage(&msg);
    msg.messageId = frame.id;
    msg.messageIdLength = frame.extended ? IfxCan_MessageIdLength_extended
                                         : IfxCan_MessageIdLength_standard;
    msg.dataLengthCode = (IfxCan_DataLengthCode)frame.dlc;
    uint32 data[2] = {0, 0};
    for (uint32 i = 0; i < frame.dlc && i < 8; ++i) {
        data[i / 4] |= (uint32)frame.data[i] << (8 * (i % 4));
    }
    IfxCan_Status st = IfxCan_Can_sendMessage(node, &msg, data);
    return st == IfxCan_Status_ok;
}

bool AurixCan::receive(Bus b, etrike::protocol::Frame& out) {
    IfxCan_Can_Node *node = (b == Bus::Low) ? &g_node_low : &g_node_high;
    // readMessage returns void; poll the RX FIFO fill level.
    if (IfxCan_Can_getRxFifo0FillLevel(node) == 0) return false;
    uint32 data[2] = {0, 0};
    IfxCan_Message msg;
    IfxCan_Can_readMessage(node, &msg, data);
    out.id = msg.messageId;
    out.extended = (msg.messageIdLength == IfxCan_MessageIdLength_extended);
    out.dlc = (uint8)msg.dataLengthCode;
    for (uint32 i = 0; i < out.dlc && i < 8; ++i) {
        out.data[i] = (uint8)((data[i / 4] >> (8 * (i % 4))) & 0xFFu);
    }
    return true;
}

void AurixCan::error_counters(Bus b, std::uint8_t& tec, std::uint8_t& rec) const {
    IfxCan_Can_Node *node = (b == Bus::Low) ? (IfxCan_Can_Node*)&g_node_low : (IfxCan_Can_Node*)&g_node_high;
    // TEC/REC from the Error Counter Register (ECR).
    tec = 0; rec = 0;
    if (node) {
        rec = (uint8)node->node->ECR.B.REC;
        tec = (uint8)node->node->ECR.B.TEC;
    }
}

bool AurixCan::bus_off(Bus b) const {
    std::uint8_t tec = 0, rec = 0;
    error_counters(b, tec, rec);
    return tec >= 255 || rec >= 255;
}

// ── Watchdog ────────────────────────────────────────────────────────
void AurixWatchdog::init() {
    // WDI GPIO configured in AurixGpio::init(); nothing else needed here.
}

void AurixWatchdog::service() {
    // Pulse P33.1 (WDI) low briefly to service the TPS3850-Q1 window watchdog.
    // The exact timing/waveform is set per the TPS3850-Q1 CWD/SET config.
    IfxPort_setPinState(kWdtWdi.port, kWdtWdi.pinIndex, IfxPort_State_low);
    // Short delay (a few us) — refine per TPS3850-Q1 window.
    IfxPort_setPinState(kWdtWdi.port, kWdtWdi.pinIndex, IfxPort_State_high);
}

}  // namespace rta::board
