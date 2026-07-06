/**
 * CAN message index — auto-generated from shared/can/can_high.yaml + can_low.yaml.
 * DO NOT EDIT BY HAND. Regenerate: python shared/can/generate_can_index.py
 */

export type Bus = "high" | "low";

export interface CanSignalDef {
  name: string; byte: number; bit_offset: number; size: number;
  type: "signed" | "unsigned"; factor: number; offset: number;
  unit: string; min: number; max: number;
  values: Record<number, string> | null; comment: string;
}

export interface CanMessageIndex {
  bus: Bus; id: string; name: string; dlc: number;
  sender: string; receivers: string[]; cycle_ms: number;
  comment: string; signals: CanSignalDef[];
}

export const CAN_INDEX: CanMessageIndex[] = [
  {bus:"high", id:"0x001", name:"SAFETY_ESTOP", dlc:0, sender:"Any", receivers:["SYS", "Host", "MTR", "DCDC"], cycle_ms:0, comment:"DLC=0 \u00e2\u20ac\u201d the frame ID itself is the ESTOP signal. Any node can send (RT is nominal). Bridged bidirectionally. Highest priority CAN frame.", signals:[]},
  {bus:"high", id:"0x011", name:"SYS_SAFETY_STS", dlc:3, sender:"SYS", receivers:["RT", "Host"], cycle_ms:200, comment:"Forwarded low\u00e2\u2020\u2019high by RT. Same payload on both buses. DLC=3 adds light state (v0.0.5).", signals:[
    {"SYS_EstopActive", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"SYS_HeartbeatOk", byte:1, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:"0=RT alive counter frozen >1000ms, 1=incrementing"},
    {"SYS_LightState", byte:2, bit_offset:0, size:4, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"bit0=left_turn_active, bit1=right_turn_active, bit2=brake_light_active, bit3=headlight_active"}
  ]},
  {bus:"high", id:"0x120", name:"SYS_THROTTLE_STS", dlc:2, sender:"MTR", receivers:["RT", "Host"], cycle_ms:10, comment:"Current vehicle speed from MTR STM32. Forwarded low\u00e2\u2020\u2019high by RT. SYS_ prefix is historical.", signals:[
    {"SYS_ThrottleSpeed", byte:0, bit_offset:0, size:16, type:"signed", factor:1, offset:0, unit:"mm/s", min:-500, max:3000, values:null, comment:""}
  ]},
  {bus:"high", id:"0x206", name:"MTR_MOTOR_FBK", dlc:4, sender:"MTR", receivers:["RT", "SYS", "Host"], cycle_ms:20, comment:"Motor feedback from STM32. Forwarded low\u00e2\u2020\u2019high by RT per gateway rules.", signals:[
    {"MTR_ActualSpeed", byte:0, bit_offset:0, size:16, type:"signed", factor:1, offset:0, unit:"mm/s", min:-500, max:3000, values:null, comment:""},
    {"MTR_GearState", byte:2, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:3, values:{"0": "N", "1": "D", "2": "S", "3": "R"}, comment:""},
    {"MTR_FaultFlags", byte:3, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:{"1": "EstopActive", "2": "CmdTimeout", "4": "AdcFault", "8": "GearConflict", "16": "StartupReady"}, comment:"bit0=ESTOP, bit1=CMD timeout, bit2=ADC fault, bit3=gear conflict, bit4=MTR startup ready"}
  ]},
  {bus:"high", id:"0x210", name:"RT_STATE_RPT", dlc:6, sender:"RT", receivers:["Host", "SYS"], cycle_ms:100, comment:"RT state report to Host (high bus) and SYS (low bus). SYS monitors safety_state for takeover detection and RT health.", signals:[
    {"RT_Mode", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:2, values:{"0": "Manual", "1": "Auto", "2": "ESTOP"}, comment:""},
    {"RT_SafetyState", byte:1, bit_offset:0, size:2, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:2, values:{"0": "Normal", "1": "InternalEstop", "2": "Fault"}, comment:"RT internal state: 0=Normal, 1=Internal ESTOP (steer ramp/hold), 2=Fault"},
    {"RT_EstopReason", byte:1, bit_offset:4, size:4, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:7, values:{"0": "None", "1": "Button", "2": "Heartbeat", "3": "FollowingError", "4": "Obstacle", "5": "CanEstop", "6": "BusOff", "7": "Internal"}, comment:"Reason for ESTOP state, packed in byte 1 bits 4-7"},
    {"RT_Reversing", byte:2, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"RT_RxOverflow", byte:3, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"MCP2515 RX overflow counter \u00e2\u20ac\u201d telemetry for CAN bus health monitoring"},
    {"RT_TaskHealth", byte:4, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"Bitmask of alive tasks (bits 0-3: control/dispatch/tx_low/tx_high)"},
    {"RT_SteerState", byte:5, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:5, values:{"0": "BootWait", "1": "ListenSync", "2": "Active", "3": "EstopRamp", "4": "EstopHold", "5": "Fault"}, comment:"Steering state machine value"}
  ]},
  {bus:"high", id:"0x220", name:"RT_PID_RPT", dlc:6, sender:"RT", receivers:["Host"], cycle_ms:100, comment:"RESERVED, inactive. PID telemetry for Host debugging.", signals:[
    {"RT_PidSetpoint", byte:0, bit_offset:0, size:16, type:"signed", factor:1, offset:0, unit:"mm/s", min:0, max:0, values:null, comment:""},
    {"RT_PidMeasured", byte:2, bit_offset:0, size:16, type:"signed", factor:1, offset:0, unit:"mm/s", min:0, max:0, values:null, comment:""},
    {"RT_PidOutput", byte:4, bit_offset:0, size:16, type:"signed", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:""}
  ]},
  {bus:"high", id:"0x300", name:"HOST_DRIVE_CMD", dlc:8, sender:"Host", receivers:["RT"], cycle_ms:10, comment:"Host (Jetson Orin) Autoware.Auto drive command -> RT. High bus only.", signals:[
    {"HOST_DriveSpeed", byte:0, bit_offset:0, size:32, type:"signed", factor:1, offset:0, unit:"mm/s", min:-500, max:3000, values:null, comment:"ROS 2: linear.x * 1000"},
    {"HOST_YawRate", byte:4, bit_offset:0, size:24, type:"signed", factor:1, offset:0, unit:"mrad/s", min:-3000, max:3000, values:null, comment:"ROS 2: angular.z * 1000. i24 big-endian at bytes 4-6."},
    {"HOST_Gear", byte:7, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:3, values:{"0": "N", "1": "D", "2": "S", "3": "R"}, comment:""}
  ]},
  {bus:"high", id:"0x301", name:"HOST_BRAKE_REQ", dlc:4, sender:"Host", receivers:["RT"], cycle_ms:0, comment:"On demand. RT arbitrates: max(RT_computed, HOST_request) -> 0x205.", signals:[
    {"HOST_BrakePressure", byte:0, bit_offset:0, size:32, type:"signed", factor:1, offset:0, unit:"kPa", min:0, max:20000, values:null, comment:""}
  ]},
  {bus:"high", id:"0x302", name:"HOST_LIGHT_CMD", dlc:1, sender:"Host", receivers:["RT", "SYS"], cycle_ms:0, comment:"Forwarded transparently high\u00e2\u2020\u2019low by RT.", signals:[
    {"HOST_LeftTurn", byte:0, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"HOST_RightTurn", byte:0, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"HOST_BrakeLight", byte:0, bit_offset:2, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"HOST_Headlight", byte:0, bit_offset:3, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""}
  ]},
  {bus:"high", id:"0x310", name:"STEER_DIAG", dlc:8, sender:"RT", receivers:["Host"], cycle_ms:100, comment:"Steering telemetry to Host. v0.0.4 \u00e2\u20ac\u201d previously missing from DBC.", signals:[
    {"SteerDiag_Angle0_1deg", byte:0, bit_offset:0, size:16, type:"unsigned", factor:0.1, offset:-3000, unit:"deg", min:0, max:0, values:null, comment:"Actual steering angle. physical_deg = raw * 0.1 - 3000. Raw 30000 -> 0deg."},
    {"SteerDiag_Fault", byte:2, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:"0=OK, 1=EPS-C fault"},
    {"SteerDiag_MotorCurrent", byte:3, bit_offset:0, size:16, type:"unsigned", factor:0.01, offset:0, unit:"A", min:0, max:0, values:null, comment:"EPS-C motor current, 0.01A/bit"},
    {"SteerDiag_ECUTemp", byte:5, bit_offset:0, size:16, type:"unsigned", factor:0.1, offset:0, unit:"degC", min:0, max:0, values:null, comment:"EPS-C ECU temperature, 0.1degC/bit"},
    {"SteerDiag_Reserved", byte:7, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:""}
  ]},
  {bus:"high", id:"0x311", name:"BRAKE_DIAG", dlc:8, sender:"RT", receivers:["Host"], cycle_ms:100, comment:"Brake telemetry to Host. v0.0.4 \u00e2\u20ac\u201d previously missing from DBC.", signals:[
    {"BrakeDiag_PressureRaw", byte:0, bit_offset:0, size:16, type:"unsigned", factor:0.05, offset:0, unit:"MPa", min:0, max:0, values:null, comment:"SEB pressure raw, 0.05 MPa/bit"},
    {"BrakeDiag_Fault", byte:2, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:"0=OK, 1=SEB fault"},
    {"BrakeDiag_MotorCurrent", byte:3, bit_offset:0, size:16, type:"unsigned", factor:0.01, offset:0, unit:"A", min:0, max:0, values:null, comment:"SEB motor current, 0.01A/bit"},
    {"BrakeDiag_ECUTemp", byte:5, bit_offset:0, size:16, type:"unsigned", factor:0.1, offset:0, unit:"degC", min:0, max:0, values:null, comment:"SEB ECU temperature, 0.1degC/bit"},
    {"BrakeDiag_Reserved", byte:7, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:""}
  ]},
  {bus:"high", id:"0x400", name:"HOST_OBSTACLE_DIST", dlc:4, sender:"Host", receivers:["RT"], cycle_ms:100, comment:"Host sends min obstacle distance (from LiDAR/camera perception) to RT at 10 Hz. High bus only.", signals:[
    {"HOST_ObstacleDistance", byte:0, bit_offset:0, size:32, type:"unsigned", factor:1, offset:0, unit:"mm", min:0, max:4294967295, values:null, comment:"UINT32_MAX = no reading / timeout"}
  ]},
  {bus:"high", id:"0x600", name:"SYS_DIAG_RPT", dlc:8, sender:"SYS", receivers:["RT", "Host"], cycle_ms:1000, comment:"SYS diagnostics report. Forwarded low\u00e2\u2020\u2019high by RT.", signals:[
    {"SYS_DiagMode", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:2, values:null, comment:""},
    {"SYS_DiagBrakeEngaged", byte:1, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"SYS_DiagBrakeFault", byte:1, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:"SEB L3 fault or brake following-error active"},
    {"SYS_DiagHeartbeatOk", byte:2, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"SYS_DiagEstopActive", byte:3, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"SYS_DiagFreeHeapKb", byte:4, bit_offset:0, size:16, type:"unsigned", factor:1, offset:0, unit:"KB", min:0, max:65535, values:null, comment:""},
    {"SYS_DiagTec", byte:6, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:""},
    {"SYS_DiagRec", byte:7, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:""}
  ]},
  {bus:"high", id:"0x7FC", name:"HOST_HEARTBEAT", dlc:1, sender:"Host", receivers:["RT"], cycle_ms:500, comment:"Not bridged, high bus only. Loss triggers controlled stop, not ESTOP.", signals:[
    {"Host_AliveCtr", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"Timeout 1500ms -> controlled stop. Host is QM, not safety-critical."}
  ]},
  {bus:"high", id:"0x7FD", name:"RT_HEARTBEAT", dlc:2, sender:"RT", receivers:["Host", "SYS"], cycle_ms:500, comment:"RT sends independently on both buses (per-bus, NOT bridged). Separate counters. This is the high-bus instance.", signals:[
    {"RT_AliveCtr", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"High bus timeout 1500ms->Host stops /cmd_vel"},
    {"RT_HealthFlags", byte:1, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"bit0=heartbeat_ok, bit1=estop_active, bit2=mode_auto, bit3=can_ok, bits4-7=reserved"}
  ]},
  {bus:"low", id:"0x001", name:"SAFETY_ESTOP", dlc:0, sender:"Any", receivers:["SYS", "Host", "MTR", "DCDC"], cycle_ms:0, comment:"DLC=0 \u00e2\u20ac\u201d the frame ID itself is the ESTOP signal. Any node can send (RT is nominal). Bridged bidirectionally. Highest priority CAN frame.", signals:[]},
  {bus:"low", id:"0x011", name:"SYS_SAFETY_STS", dlc:3, sender:"SYS", receivers:["RT", "Host"], cycle_ms:200, comment:"Forwarded low\u00e2\u2020\u2019high by RT. Same payload on both buses. DLC=3 adds light state (v0.0.5).", signals:[
    {"SYS_EstopActive", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"SYS_HeartbeatOk", byte:1, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:"0=RT alive counter frozen >1000ms, 1=incrementing"},
    {"SYS_LightState", byte:2, bit_offset:0, size:4, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"bit0=left_turn_active, bit1=right_turn_active, bit2=brake_light_active, bit3=headlight_active"}
  ]},
  {bus:"low", id:"0x012", name:"SYS_DCDC_CMD", dlc:1, sender:"SYS", receivers:["DCDC"], cycle_ms:0, comment:"DC-DC converter control. Low bus only.", signals:[
    {"SYS_DcdcEnable", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:"ESTOP->1(on); maintains 12V for MCUs, CAN transceivers, brake light"}
  ]},
  {bus:"low", id:"0x110", name:"SYS_MODE_CMD", dlc:1, sender:"SYS", receivers:["RT", "MTR"], cycle_ms:0, comment:"0=Manual, 1=Auto, 2=ESTOP. Low bus only. MTR needs mode for pass-through vs CAN control.", signals:[
    {"SYS_Mode", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:2, values:{"0": "Manual", "1": "Auto", "2": "ESTOP"}, comment:""}
  ]},
  {bus:"low", id:"0x120", name:"SYS_THROTTLE_STS", dlc:2, sender:"MTR", receivers:["RT", "Host"], cycle_ms:10, comment:"Current vehicle speed from MTR STM32. Forwarded low\u00e2\u2020\u2019high by RT. SYS_ prefix is historical.", signals:[
    {"SYS_ThrottleSpeed", byte:0, bit_offset:0, size:16, type:"signed", factor:1, offset:0, unit:"mm/s", min:-500, max:3000, values:null, comment:""}
  ]},
  {bus:"low", id:"0x169", name:"VCU_SES_REQ", dlc:8, sender:"RT", receivers:["EPS_C"], cycle_ms:20, comment:"steer-by-wire unit command. 50 Hz continuous. Byte 5 overlap: Speed[15:8] shares with security nibble per CSV.", signals:[
    {"SES_AlignEnable", byte:0, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Initial Alignment Enable. 0=disabled, 1=centering."},
    {"SES_CtrlEnable", byte:0, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Direction Control Enable. 0=Disabled, 1=Enable (Angle Control)."},
    {"SES_TgtStrAngle", byte:2, bit_offset:0, size:16, type:"signed", factor:0.1, offset:-3000, unit:"deg", min:-700, max:700, values:null, comment:"Target Steering Angle. Negative=left. Offset=-3000 per mfr CSV."},
    {"SES_TgtStrAngleSpd", byte:4, bit_offset:0, size:16, type:"unsigned", factor:1, offset:0, unit:"deg/s", min:125, max:525, values:null, comment:"Target Angle Speed. Overlaps security signals at byte 5 per CSV. Effective 10-bit: bits 0-7 in byte 4, bits 8-9 in byte 5 bits 2-3. Bits 10-15 overlaid."},
    {"SES_RollCntEnable", byte:5, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Life Signal Enable \u00e2\u20ac\u201d MUST be 1. Overlaps Speed[15:8]."},
    {"SES_ChecksumEnable", byte:5, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Checksum Enable \u00e2\u20ac\u201d MUST be 1."},
    {"SES_RollCnt", byte:5, bit_offset:4, size:4, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:15, values:null, comment:"Life Signal rolling counter. Increment every frame."},
    {"SES_VehSpd", byte:6, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"km/h", min:0, max:255, values:null, comment:"Vehicle speed populated by RT."},
    {"SES_Checksum", byte:7, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"Checksum = XOR(bytes 0-6) ^ 0xFF."}
  ]},
  {bus:"low", id:"0x201", name:"SES_STATUS", dlc:8, sender:"EPS_C", receivers:["RT"], cycle_ms:10, comment:"steer-by-wire unit status feedback. 100 Hz. Byte 5 overlap: StrAngleSpd[15:8] / Torq share byte 5 per CSV.", signals:[
    {"SES_AngleStatus", byte:0, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Center Finding Status. 0=Finding, 1=Found."},
    {"SES_CtrlModeStatus", byte:0, bit_offset:1, size:2, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:3, values:null, comment:"Control Mode Feedback. 0=Manual, 1=Automatic."},
    {"SES_ErrorStatus", byte:0, bit_offset:6, size:2, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:3, values:{"0": "Normal", "1": "L1_Warning", "2": "L2_General", "3": "L3_Severe"}, comment:"Error Status."},
    {"SES_StrAngle", byte:2, bit_offset:0, size:16, type:"unsigned", factor:0.1, offset:-3000, unit:"deg", min:-700, max:700, values:null, comment:"Steering Angle. Raw 30000->0deg, 23000->-700deg, 37000->700deg."},
    {"SES_TgtStrAngleSpd_FB", byte:4, bit_offset:0, size:16, type:"signed", factor:0.5, offset:0, unit:"deg/s", min:0, max:1480, values:null, comment:"Angle Speed feedback. 16-bit signed. Overlaps Torq at byte 5."},
    {"SES_SteeringTorq", byte:5, bit_offset:0, size:8, type:"unsigned", factor:0.1, offset:-12.1, unit:"Nm", min:-12, max:12, values:null, comment:"Steering Torque. Init 0x79 (121 raw = 0 Nm). Overlaps Speed[15:8]."},
    {"SES_RollCntEnStatus", byte:6, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Life Signal Enable Feedback."},
    {"SES_ChecksumEnStatus", byte:6, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Checksum Enable Feedback."},
    {"SES_RollCntStatus", byte:6, bit_offset:4, size:4, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:15, values:null, comment:"Life Signal Feedback \u00e2\u20ac\u201d echoes rolling counter."},
    {"SES_ChecksumStatus", byte:7, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"Checksum Feedback = XOR(bytes 0-6) ^ 0xFF."}
  ]},
  {bus:"low", id:"0x202", name:"SES_ErrInfo", dlc:8, sender:"EPS_C", receivers:["RT"], cycle_ms:100, comment:"steer-by-wire unit detailed fault flags. 8 L3 faults (redundant sensor loss) -> RT must escalate to ESTOP.", signals:[
    {"SES_ECUUnderVolt", byte:0, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Controller Under Voltage [L2]"},
    {"SES_ECUOverVolt", byte:0, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Controller Over Voltage [L2]"},
    {"SES_CanComErr", byte:0, bit_offset:2, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"CAN Communication Fault [L1]"},
    {"SES_ECUTempErr", byte:0, bit_offset:3, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Controller Temp Fault [L1]"},
    {"SES_DomainSC", byte:0, bit_offset:4, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Domain Drive Short Circuit [L2]"},
    {"SES_DomainV", byte:0, bit_offset:5, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Domain Drive Voltage Fault [L2]"},
    {"SES_DomainT", byte:0, bit_offset:6, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Domain Drive Temperature Fault [L2]"},
    {"SES_TempSensor", byte:0, bit_offset:7, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Temperature Sensor Fault"},
    {"SES_AngleP_OC", byte:1, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Sensor Pri. Open Circuit [L3]"},
    {"SES_AngleP_AF", byte:1, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Sensor Pri. Out of Range [L3]"},
    {"SES_AngleS_OC", byte:1, bit_offset:2, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Sensor Sec. Open Circuit [L3]"},
    {"SES_AngleS_AF", byte:1, bit_offset:3, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Sensor Sec. Out of Range [L3]"},
    {"SES_SensorPow", byte:1, bit_offset:4, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Sensor Power Fault [L2]"},
    {"SES_Alignment", byte:1, bit_offset:5, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Centering Fault [L1]"},
    {"SES_OverAngle", byte:1, bit_offset:6, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Over Angle Fault [L2]"},
    {"SES_StrMtrStall", byte:1, bit_offset:7, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Motor Stall Fault [L1]"},
    {"SES_MtrCurtFault", byte:2, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Motor Current Fault [L2]"},
    {"SES_SensorCL", byte:2, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Sensor 5V Power Fault [L2]"},
    {"SES_TorqT1_OC", byte:2, bit_offset:2, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Torque Sensor T1 Open Circuit [L3]"},
    {"SES_TorqT1_AF", byte:2, bit_offset:3, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Torque Sensor T1 Out of Range [L3]"},
    {"SES_TorqT2_OC", byte:2, bit_offset:4, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Torque Sensor T2 Open Circuit [L3]"},
    {"SES_TorqT2_AF", byte:2, bit_offset:5, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Torque Sensor T2 Out of Range [L3]"},
    {"SES_SentAngle", byte:2, bit_offset:6, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Error [L1]"},
    {"SES_StrMtrIdling", byte:2, bit_offset:7, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Motor Idling Fault [L2]"},
    {"SES_EPROM", byte:3, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"EEPROM Fault [L2]"},
    {"SES_VehSpdSnapshot", byte:7, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"km/h", min:0, max:255, values:null, comment:"Vehicle speed at fault snapshot."}
  ]},
  {bus:"low", id:"0x203", name:"SES_Version", dlc:8, sender:"EPS_C", receivers:["RT"], cycle_ms:1000, comment:"steer-by-wire unit firmware version. Log on boot for compatibility check.", signals:[
    {"SES_SW_Version", byte:0, bit_offset:0, size:8, type:"unsigned", factor:0.01, offset:0, unit:"", min:0, max:2.55, values:null, comment:"Software version (e.g. 0x64 = 1.00)"},
    {"SES_HW_Version", byte:1, bit_offset:0, size:8, type:"unsigned", factor:0.1, offset:0, unit:"", min:0, max:25.5, values:null, comment:"Hardware version (e.g. 0x0D = 1.3)"}
  ]},
  {bus:"low", id:"0x204", name:"RT_DRIVE_CMD", dlc:5, sender:"RT", receivers:["SYS", "MTR"], cycle_ms:10, comment:"MTR receives for motor actuation. SYS receives for EGAS L2 monitoring. ID 0x204 avoids collision with EPS-C 0x202.", signals:[
    {"RT_MotorSpeed", byte:0, bit_offset:0, size:32, type:"signed", factor:1, offset:0, unit:"mm/s", min:-500, max:3000, values:null, comment:""},
    {"RT_Gear", byte:4, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:3, values:{"0": "N", "1": "D", "2": "S", "3": "R"}, comment:""}
  ]},
  {bus:"low", id:"0x205", name:"RT_BRAKE_CMD", dlc:4, sender:"RT", receivers:["SYS"], cycle_ms:20, comment:"RT max-select: max(rt_obstacle, host_0x301) -> SYS SEB cmd.", signals:[
    {"RT_BrakePressure", byte:0, bit_offset:0, size:32, type:"signed", factor:1, offset:0, unit:"kPa", min:0, max:20000, values:null, comment:""}
  ]},
  {bus:"low", id:"0x206", name:"MTR_MOTOR_FBK", dlc:4, sender:"MTR", receivers:["RT", "SYS", "Host"], cycle_ms:20, comment:"Motor feedback from STM32. Forwarded low\u00e2\u2020\u2019high by RT per gateway rules.", signals:[
    {"MTR_ActualSpeed", byte:0, bit_offset:0, size:16, type:"signed", factor:1, offset:0, unit:"mm/s", min:-500, max:3000, values:null, comment:""},
    {"MTR_GearState", byte:2, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:3, values:{"0": "N", "1": "D", "2": "S", "3": "R"}, comment:""},
    {"MTR_FaultFlags", byte:3, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:{"1": "EstopActive", "2": "CmdTimeout", "4": "AdcFault", "8": "GearConflict", "16": "StartupReady"}, comment:"bit0=ESTOP, bit1=CMD timeout, bit2=ADC fault, bit3=gear conflict"}
  ]},
  {bus:"low", id:"0x302", name:"HOST_LIGHT_CMD", dlc:1, sender:"Host", receivers:["RT", "SYS"], cycle_ms:0, comment:"Forwarded transparently high\u00e2\u2020\u2019low by RT.", signals:[
    {"HOST_LeftTurn", byte:0, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"HOST_RightTurn", byte:0, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"HOST_BrakeLight", byte:0, bit_offset:2, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"HOST_Headlight", byte:0, bit_offset:3, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""}
  ]},
  {bus:"low", id:"0x600", name:"SYS_DIAG_RPT", dlc:8, sender:"SYS", receivers:["RT", "Host"], cycle_ms:1000, comment:"SYS diagnostics report. Forwarded low\u00e2\u2020\u2019high by RT.", signals:[
    {"SYS_DiagMode", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:2, values:null, comment:""},
    {"SYS_DiagBrakeEngaged", byte:1, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"SYS_DiagBrakeFault", byte:1, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:"SEB L3 fault or brake following-error active"},
    {"SYS_DiagHeartbeatOk", byte:2, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"SYS_DiagEstopActive", byte:3, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:1, values:null, comment:""},
    {"SYS_DiagFreeHeapKb", byte:4, bit_offset:0, size:16, type:"unsigned", factor:1, offset:0, unit:"KB", min:0, max:65535, values:null, comment:""},
    {"SYS_DiagTec", byte:6, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:""},
    {"SYS_DiagRec", byte:7, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:""}
  ]},
  {bus:"low", id:"0x6FA", name:"SES_Test", dlc:8, sender:"EPS_C", receivers:["RT"], cycle_ms:10, comment:"steer-by-wire unit telemetry. 100 Hz. Bytes 0,7 reserved. Narrower ranges than brake SEB_Test.", signals:[
    {"SES_MtrCurt", byte:1, bit_offset:0, size:16, type:"signed", factor:0.0078125, offset:0, unit:"A", min:0, max:60, values:null, comment:"Motor current. Monitor for mechanical binding / rack damage."},
    {"SES_ECUTemp", byte:3, bit_offset:0, size:16, type:"unsigned", factor:0.5, offset:0, unit:"degC", min:0, max:255, values:null, comment:"ECU temperature. For thermal throttling."},
    {"SES_PowVolt", byte:5, bit_offset:0, size:16, type:"unsigned", factor:0.00390625, offset:0, unit:"V", min:0, max:18, values:null, comment:"Supply voltage. 0-18V range."}
  ]},
  {bus:"low", id:"0x6FB", name:"SEB_Test", dlc:8, sender:"SEB", receivers:["SYS", "RT"], cycle_ms:10, comment:"brake-by-wire unit telemetry. 100 Hz. RT monitors for 0x311 BRAKE_DIAG population.", signals:[
    {"SEB_MtrCurr", byte:1, bit_offset:0, size:16, type:"signed", factor:0.0078125, offset:0, unit:"A", min:-255, max:255, values:null, comment:"Motor current. Monitor for mechanical binding."},
    {"SEB_ECUTemp", byte:3, bit_offset:0, size:16, type:"unsigned", factor:0.5, offset:-40, unit:"degC", min:-40, max:215, values:null, comment:"ECU temperature. Offset=-40 per manufacturer CSV physical range (raw 0 = -40 degC)."},
    {"SEB_PowVolt", byte:5, bit_offset:0, size:16, type:"unsigned", factor:0.00390625, offset:0, unit:"V", min:0, max:32, values:null, comment:"Supply voltage. 0-32V range."}
  ]},
  {bus:"low", id:"0x721", name:"SEB_STATUS", dlc:8, sender:"SEB", receivers:["SYS", "RT"], cycle_ms:10, comment:"brake-by-wire unit status feedback. 100 Hz. RT monitors pressure for 0x311 BRAKE_DIAG. SYS usage: boot sync -> read StrokeValue; active -> confirm AlignStatus==1.", signals:[
    {"SEB_AlignStatus", byte:0, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Alignment Info Feedback. 1=aligned."},
    {"SEB_CtrlEnStatus", byte:0, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Control Enable Feedback."},
    {"SEB_CtrlModeStatus", byte:0, bit_offset:2, size:2, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:3, values:{"0": "None", "1": "Stroke", "2": "Pressure"}, comment:"Control Mode Feedback."},
    {"SEB_AutoBrakeStatus", byte:0, bit_offset:4, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Auto Brake Status Feedback."},
    {"SEB_ErrorStatus", byte:0, bit_offset:6, size:2, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:3, values:{"0": "Normal", "1": "L1_Warning", "2": "L2_General", "3": "L3_Severe"}, comment:"Error Status."},
    {"SEB_StrokeValue", byte:2, bit_offset:0, size:16, type:"unsigned", factor:0.05, offset:-30, unit:"mm", min:-5, max:27, values:null, comment:"Stroke Value Feedback."},
    {"SEB_PressureValue", byte:3, bit_offset:0, size:8, type:"unsigned", factor:0.05, offset:0, unit:"MPa", min:0, max:5, values:null, comment:"Pressure Feedback. Overlaps Stroke[15:8] \u00e2\u20ac\u201d mode-dependent."},
    {"SEB_AngleValue", byte:5, bit_offset:0, size:16, type:"signed", factor:0.5, offset:0, unit:"", min:-150, max:840, values:null, comment:"Angle Feedback. Overlaps security echo at byte 6."},
    {"SEB_RollCntEnStatus", byte:6, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Life Signal Status Feedback."},
    {"SEB_ChecksumEnStatus", byte:6, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Checksum Status Feedback."},
    {"SEB_RollCntStatus", byte:6, bit_offset:4, size:4, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:15, values:null, comment:"Life Signal Feedback \u00e2\u20ac\u201d echoes rolling counter."},
    {"SEB_ChecksumStatus", byte:7, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"Checksum Feedback."}
  ]},
  {bus:"low", id:"0x731", name:"SEB_ErrInfo", dlc:8, sender:"SEB", receivers:["SYS"], cycle_ms:100, comment:"brake-by-wire unit detailed fault flags. 16 of 23 faults are L3 -> SYS must escalate to ESTOP.", signals:[
    {"SEB_ECUUnderVolt", byte:0, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Controller Undervoltage [L2]"},
    {"SEB_ECUOverVolt", byte:0, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Controller Overvoltage [L2]"},
    {"SEB_CanComErr", byte:0, bit_offset:2, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"CAN Communication Fault [L3]"},
    {"SEB_ECUTempErr", byte:0, bit_offset:3, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Controller Temperature Fault [L3]"},
    {"SEB_DomainSC", byte:0, bit_offset:4, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Domain Drive Short Circuit [L3]"},
    {"SEB_DomainV", byte:0, bit_offset:5, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Domain Drive Voltage Fault [L3]"},
    {"SEB_DomainT", byte:0, bit_offset:6, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Domain Drive Temperature Fault [L3]"},
    {"SEB_AngleP_OC", byte:0, bit_offset:7, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Sensor P Open Circuit [L3]"},
    {"SEB_AngleP_AF", byte:1, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Sensor P Mainboard Abnormal [L3]"},
    {"SEB_AngleS_OC", byte:1, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Sensor S Open Circuit [L3]"},
    {"SEB_AngleS_AF", byte:1, bit_offset:2, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Sensor S Sub-board Abnormal [L3]"},
    {"SEB_NoPreSensor", byte:1, bit_offset:3, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Unconnected Oil Pressure Sensor [L3]"},
    {"SEB_SensorUCL", byte:1, bit_offset:5, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Sensor Plausibility Fault [L3]"},
    {"SEB_AlignmentErr", byte:1, bit_offset:6, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Alignment Fault [L2]"},
    {"SEB_AngleOver", byte:1, bit_offset:7, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Angle Out of Bounds [L2]"},
    {"SEB_MtrStall", byte:2, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Motor Stall Fault [L3]"},
    {"SEB_MtrDC", byte:2, bit_offset:2, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Motor Disconnect Fault [L3]"},
    {"SEB_OilErr", byte:2, bit_offset:3, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Oil Pressure Error [L2]"},
    {"SEB_InitOil", byte:2, bit_offset:4, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Initial Oil Pressure Fault [L3]"},
    {"SEB_SentValue", byte:2, bit_offset:5, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Send Value Error [L3]"},
    {"SEB_MtrNoLoad", byte:2, bit_offset:6, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Motor No-load Fault [L3]"},
    {"SEB_PreSensorOver", byte:3, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Oil Pressure Sensor Overvoltage [L2]"},
    {"SEB_LowVoltCharging", byte:3, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Low Voltage Charging Failure [L2]"}
  ]},
  {bus:"low", id:"0x741", name:"SEB_Version", dlc:8, sender:"SEB", receivers:["SYS"], cycle_ms:1000, comment:"brake-by-wire unit firmware version. Log on boot for compatibility check.", signals:[
    {"SEB_SW_Version", byte:0, bit_offset:0, size:8, type:"unsigned", factor:0.01, offset:0, unit:"", min:0, max:2.55, values:null, comment:"Software version (e.g. 0xC8 = 2.00)"},
    {"SEB_HW_Version", byte:1, bit_offset:0, size:8, type:"unsigned", factor:0.1, offset:0, unit:"", min:0, max:25.5, values:null, comment:"Hardware version (e.g. 0x0D = 1.3)"}
  ]},
  {bus:"low", id:"0x7B9", name:"VCU_SEB_REQ", dlc:8, sender:"SYS", receivers:["SEB"], cycle_ms:20, comment:"brake-by-wire unit brake command. 50 Hz continuous. Byte 3 mode-mux: Stroke[15:8] in Mode 0, Pressure in Mode 1.", signals:[
    {"SEB_AlignEnable", byte:0, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Calibration enable."},
    {"SEB_CtrlEnable", byte:0, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Active control enable."},
    {"SEB_CtrlMode", byte:0, bit_offset:2, size:1, type:"unsigned", factor:1, offset:0, unit:"enum", min:0, max:0, values:{"0": "Stroke", "1": "Pressure"}, comment:"0=Stroke (position), 1=Pressure (hydraulic)."},
    {"SEB_AutoBrake", byte:0, bit_offset:3, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Auto-brake / emergency trigger."},
    {"SEB_StrokeReq", byte:2, bit_offset:0, size:16, type:"unsigned", factor:0.05, offset:-30, unit:"mm", min:-5, max:27, values:null, comment:"Stroke position. Full 16-bit in Stroke mode. Overlaps with PressureReq at byte 3."},
    {"SEB_PressureReq", byte:3, bit_offset:0, size:8, type:"unsigned", factor:0.05, offset:0, unit:"MPa", min:0, max:5, values:null, comment:"Pressure in Mode 1. Overlaps Stroke[15:8] \u00e2\u20ac\u201d mode-dependent."},
    {"SEB_RollCntEnable", byte:6, bit_offset:0, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Life Signal Validity \u00e2\u20ac\u201d MUST be 1."},
    {"SEB_ChecksumEnable", byte:6, bit_offset:1, size:1, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:0, values:null, comment:"Checksum Validity \u00e2\u20ac\u201d MUST be 1."},
    {"SEB_RollCnt", byte:6, bit_offset:4, size:4, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:15, values:null, comment:"Life Signal rolling counter. Increment every frame."},
    {"SEB_Checksum", byte:7, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"Checksum = XOR(bytes 0-6) ^ 0xFF."}
  ]},
  {bus:"low", id:"0x7FD", name:"RT_HEARTBEAT", dlc:2, sender:"RT", receivers:["Host", "SYS"], cycle_ms:500, comment:"RT sends independently on both buses (per-bus, NOT bridged). Separate counters. This is the low-bus instance.", signals:[
    {"RT_AliveCtr", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"Low bus timeout 1000ms->SYS ESTOP"},
    {"RT_HealthFlags", byte:1, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"bit0=heartbeat_ok, bit1=estop_active, bit2=mode_auto, bit3=can_ok, bits4-7=reserved"}
  ]},
  {bus:"low", id:"0x7FE", name:"SYS_HEARTBEAT", dlc:2, sender:"SYS", receivers:["RT"], cycle_ms:100, comment:"Low bus only, never leaves low bus.", signals:[
    {"SYS_AliveCtr", byte:0, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"10 Hz / 200ms timeout -> RT brake takeover + ESTOP"},
    {"SYS_HealthFlags", byte:1, bit_offset:0, size:8, type:"unsigned", factor:1, offset:0, unit:"", min:0, max:255, values:null, comment:"bit0=heartbeat_ok, bit1=estop_active, bits2-3=reserved"}
  ]}
];
