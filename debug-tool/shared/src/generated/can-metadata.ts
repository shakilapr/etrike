/**
 * CAN message metadata — auto-generated from shared/can/can_high.yaml + can_low.yaml.
 * DO NOT EDIT BY HAND. Regenerate: python shared/can/generate_can_ts.py
 */

import type { CanMessageDef, CanField, Bus, FieldKind } from "../can";

export const PROTOCOL_HASH = "ae0c37f0f234dd774049410d014be3af544affb656356b3496e48438523ef72f";

export interface InternalCanField extends CanField {
  _byte: number;
  _bit_offset: number;
  _size: number;
  _type: "signed" | "unsigned";
  _factor: number;
  _offset: number;
  multiplexed?: boolean;
}

export interface InternalCanMessageDef extends CanMessageDef {
  byteOrder: "motorola" | "intel";
  fields: InternalCanField[];
}

export const ID_SAFETY_ESTOP = "0x001";
export const ID_SYS_SAFETY_STS = "0x011";
export const SIG_SYS_SAFETY_STS_ESTOP_ACTIVE = "estop_active";
export const SIG_SYS_SAFETY_STS_HEARTBEAT_OK = "heartbeat_ok";
export const SIG_SYS_SAFETY_STS_LIGHT_LEFT = "light_left";
export const SIG_SYS_SAFETY_STS_LIGHT_RIGHT = "light_right";
export const SIG_SYS_SAFETY_STS_LIGHT_BRAKE = "light_brake";
export const SIG_SYS_SAFETY_STS_LIGHT_HEAD = "light_head";
export const ID_HMI_MODE_REQ = "0x111";
export const SIG_HMI_MODE_REQ_HMI_REQMODE = "HMI_ReqMode";
export const SIG_HMI_MODE_REQ_ROLLING_COUNTER = "rolling_counter";
export const ID_HMI_PWR_REQ = "0x112";
export const SIG_HMI_PWR_REQ_HMI_REQSTART = "HMI_ReqStart";
export const SIG_HMI_PWR_REQ_ROLLING_COUNTER = "rolling_counter";
export const ID_SYS_THROTTLE_STS = "0x120";
export const SIG_SYS_THROTTLE_STS_SPEED_MMPS = "speed_mmps";
export const ID_MTR_MOTOR_FBK = "0x206";
export const SIG_MTR_MOTOR_FBK_ACTUAL_SPEED_MMPS = "actual_speed_mmps";
export const SIG_MTR_MOTOR_FBK_GEAR_STATE = "gear_state";
export const SIG_MTR_MOTOR_FBK_FAULT_FLAGS = "fault_flags";
export const ID_RT_STATE_RPT = "0x210";
export const SIG_RT_STATE_RPT_MODE = "mode";
export const SIG_RT_STATE_RPT_SAFETY_STATE = "safety_state";
export const SIG_RT_STATE_RPT_ESTOP_REASON = "estop_reason";
export const SIG_RT_STATE_RPT_REVERSING = "reversing";
export const SIG_RT_STATE_RPT_RX_OVERFLOW = "rx_overflow";
export const SIG_RT_STATE_RPT_TASK_HEALTH = "task_health";
export const SIG_RT_STATE_RPT_STEER_STATE = "steer_state";
export const ID_RT_PID_RPT = "0x220";
export const SIG_RT_PID_RPT_SPEED_SETPOINT = "speed_setpoint";
export const SIG_RT_PID_RPT_SPEED_MEASURED = "speed_measured";
export const SIG_RT_PID_RPT_PID_OUTPUT = "pid_output";
export const ID_HOST_DRIVE_CMD = "0x300";
export const SIG_HOST_DRIVE_CMD_SPEED_MMPS = "speed_mmps";
export const SIG_HOST_DRIVE_CMD_YAW_RATE_MRAD_S = "yaw_rate_mrad_s";
export const SIG_HOST_DRIVE_CMD_GEAR = "gear";
export const ID_HOST_BRAKE_REQ = "0x301";
export const SIG_HOST_BRAKE_REQ_BRAKE_PRESSURE_KPA = "brake_pressure_kpa";
export const ID_HOST_LIGHT_CMD = "0x302";
export const SIG_HOST_LIGHT_CMD_LEFT_TURN = "left_turn";
export const SIG_HOST_LIGHT_CMD_RIGHT_TURN = "right_turn";
export const SIG_HOST_LIGHT_CMD_BRAKE_LIGHT = "brake_light";
export const SIG_HOST_LIGHT_CMD_HEADLIGHT = "headlight";
export const ID_STEER_DIAG = "0x310";
export const SIG_STEER_DIAG_STEERDIAG_ANGLE0_1DEG = "SteerDiag_Angle0_1deg";
export const SIG_STEER_DIAG_STEERDIAG_FAULT = "SteerDiag_Fault";
export const SIG_STEER_DIAG_STEERDIAG_MOTORCURRENT = "SteerDiag_MotorCurrent";
export const SIG_STEER_DIAG_STEERDIAG_ECUTEMP = "SteerDiag_ECUTemp";
export const SIG_STEER_DIAG_STEERDIAG_RESERVED = "SteerDiag_Reserved";
export const ID_BRAKE_DIAG = "0x311";
export const SIG_BRAKE_DIAG_BRAKEDIAG_PRESSURERAW = "BrakeDiag_PressureRaw";
export const SIG_BRAKE_DIAG_BRAKEDIAG_FAULT = "BrakeDiag_Fault";
export const SIG_BRAKE_DIAG_BRAKEDIAG_MOTORCURRENT = "BrakeDiag_MotorCurrent";
export const SIG_BRAKE_DIAG_BRAKEDIAG_ECUTEMP = "BrakeDiag_ECUTemp";
export const SIG_BRAKE_DIAG_BRAKEDIAG_RESERVED = "BrakeDiag_Reserved";
export const ID_HOST_OBSTACLE_DIST = "0x400";
export const SIG_HOST_OBSTACLE_DIST_DISTANCE_MM = "distance_mm";
export const ID_SYS_DIAG_RPT = "0x600";
export const SIG_SYS_DIAG_RPT_SYS_DIAGMODE = "SYS_DiagMode";
export const SIG_SYS_DIAG_RPT_SYS_DIAGBRAKEENGAGED = "SYS_DiagBrakeEngaged";
export const SIG_SYS_DIAG_RPT_SYS_DIAGBRAKEFAULT = "SYS_DiagBrakeFault";
export const SIG_SYS_DIAG_RPT_SYS_DIAGHEARTBEATOK = "SYS_DiagHeartbeatOk";
export const SIG_SYS_DIAG_RPT_SYS_DIAGESTOPACTIVE = "SYS_DiagEstopActive";
export const SIG_SYS_DIAG_RPT_SYS_DIAGFREEHEAPKB = "SYS_DiagFreeHeapKb";
export const SIG_SYS_DIAG_RPT_SYS_DIAGTEC = "SYS_DiagTec";
export const SIG_SYS_DIAG_RPT_SYS_DIAGREC = "SYS_DiagRec";
export const ID_HOST_HEARTBEAT = "0x7FC";
export const SIG_HOST_HEARTBEAT_ALIVE_CTR = "alive_ctr";
export const SIG_HOST_HEARTBEAT_HEALTH_FLAGS = "health_flags";
export const ID_RT_HEARTBEAT = "0x7FD";
export const SIG_RT_HEARTBEAT_ALIVE_CTR = "alive_ctr";
export const SIG_RT_HEARTBEAT_HEALTH_FLAGS = "health_flags";
export const ID_SYS_DCDC_CMD = "0x012";
export const SIG_SYS_DCDC_CMD_SYS_DCDCENABLE = "SYS_DcdcEnable";
export const ID_SYS_MODE_CMD = "0x110";
export const SIG_SYS_MODE_CMD_MODE = "mode";
export const ID_VCU_SES_REQ = "0x169";
export const SIG_VCU_SES_REQ_ALIGNMENT_ENABLE = "alignment_enable";
export const SIG_VCU_SES_REQ_CONTROL_ENABLE = "control_enable";
export const SIG_VCU_SES_REQ_TARGET_ANGLE = "target_angle";
export const SIG_VCU_SES_REQ_TARGET_SPEED = "target_speed";
export const SIG_VCU_SES_REQ_SES_ROLLCNTENABLE = "SES_RollCntEnable";
export const SIG_VCU_SES_REQ_SES_CHECKSUMENABLE = "SES_ChecksumEnable";
export const SIG_VCU_SES_REQ_ROLLING_COUNTER = "rolling_counter";
export const SIG_VCU_SES_REQ_SES_VEHSPD = "SES_VehSpd";
export const SIG_VCU_SES_REQ_CHECKSUM = "checksum";
export const ID_SES_STATUS = "0x201";
export const SIG_SES_STATUS_ANGLE_STATUS = "angle_status";
export const SIG_SES_STATUS_SES_CTRLMODESTATUS = "SES_CtrlModeStatus";
export const SIG_SES_STATUS_ERROR_STATUS = "error_status";
export const SIG_SES_STATUS_STR_ANGLE = "str_angle";
export const SIG_SES_STATUS_TGT_ANGLE_SPD = "tgt_angle_spd";
export const SIG_SES_STATUS_SES_STEERINGTORQ = "SES_SteeringTorq";
export const SIG_SES_STATUS_SES_ROLLCNTENSTATUS = "SES_RollCntEnStatus";
export const SIG_SES_STATUS_SES_CHECKSUMENSTATUS = "SES_ChecksumEnStatus";
export const SIG_SES_STATUS_ROLLING_COUNTER = "rolling_counter";
export const SIG_SES_STATUS_CHECKSUM = "checksum";
export const ID_SES_ErrInfo = "0x202";
export const SIG_SES_ErrInfo_SES_ECUUNDERVOLT = "SES_ECUUnderVolt";
export const SIG_SES_ErrInfo_SES_ECUOVERVOLT = "SES_ECUOverVolt";
export const SIG_SES_ErrInfo_SES_CANCOMERR = "SES_CanComErr";
export const SIG_SES_ErrInfo_SES_ECUTEMPERR = "SES_ECUTempErr";
export const SIG_SES_ErrInfo_SES_DOMAINSC = "SES_DomainSC";
export const SIG_SES_ErrInfo_SES_DOMAINV = "SES_DomainV";
export const SIG_SES_ErrInfo_SES_DOMAINT = "SES_DomainT";
export const SIG_SES_ErrInfo_SES_TEMPSENSOR = "SES_TempSensor";
export const SIG_SES_ErrInfo_SES_ANGLEP_OC = "SES_AngleP_OC";
export const SIG_SES_ErrInfo_SES_ANGLEP_AF = "SES_AngleP_AF";
export const SIG_SES_ErrInfo_SES_ANGLES_OC = "SES_AngleS_OC";
export const SIG_SES_ErrInfo_SES_ANGLES_AF = "SES_AngleS_AF";
export const SIG_SES_ErrInfo_SES_SENSORPOW = "SES_SensorPow";
export const SIG_SES_ErrInfo_SES_ALIGNMENT = "SES_Alignment";
export const SIG_SES_ErrInfo_SES_OVERANGLE = "SES_OverAngle";
export const SIG_SES_ErrInfo_SES_STRMTRSTALL = "SES_StrMtrStall";
export const SIG_SES_ErrInfo_SES_MTRCURTFAULT = "SES_MtrCurtFault";
export const SIG_SES_ErrInfo_SES_SENSORCL = "SES_SensorCL";
export const SIG_SES_ErrInfo_SES_TORQT1_OC = "SES_TorqT1_OC";
export const SIG_SES_ErrInfo_SES_TORQT1_AF = "SES_TorqT1_AF";
export const SIG_SES_ErrInfo_SES_TORQT2_OC = "SES_TorqT2_OC";
export const SIG_SES_ErrInfo_SES_TORQT2_AF = "SES_TorqT2_AF";
export const SIG_SES_ErrInfo_SES_SENTANGLE = "SES_SentAngle";
export const SIG_SES_ErrInfo_SES_STRMTRIDLING = "SES_StrMtrIdling";
export const SIG_SES_ErrInfo_SES_EPROM = "SES_EPROM";
export const SIG_SES_ErrInfo_SES_VEHSPDSNAPSHOT = "SES_VehSpdSnapshot";
export const ID_SES_Version = "0x203";
export const SIG_SES_Version_SES_SW_VERSION = "SES_SW_Version";
export const SIG_SES_Version_SES_HW_VERSION = "SES_HW_Version";
export const ID_RT_DRIVE_CMD = "0x204";
export const SIG_RT_DRIVE_CMD_MOTOR_SPEED_MMPS = "motor_speed_mmps";
export const SIG_RT_DRIVE_CMD_GEAR = "gear";
export const ID_RT_BRAKE_CMD = "0x205";
export const SIG_RT_BRAKE_CMD_BRAKE_PRESSURE_KPA = "brake_pressure_kpa";
export const ID_SES_Test = "0x6FA";
export const SIG_SES_Test_SES_MTRCURT = "SES_MtrCurt";
export const SIG_SES_Test_SES_ECUTEMP = "SES_ECUTemp";
export const SIG_SES_Test_SES_POWVOLT = "SES_PowVolt";
export const ID_SEB_Test = "0x6FB";
export const SIG_SEB_Test_SEB_MTRCURR = "SEB_MtrCurr";
export const SIG_SEB_Test_SEB_ECUTEMP = "SEB_ECUTemp";
export const SIG_SEB_Test_SEB_POWVOLT = "SEB_PowVolt";
export const ID_SEB_STATUS = "0x721";
export const SIG_SEB_STATUS_ALIGNMENT_STATUS = "alignment_status";
export const SIG_SEB_STATUS_CONTROL_ENABLE_STS = "control_enable_sts";
export const SIG_SEB_STATUS_CONTROL_MODE_STS = "control_mode_sts";
export const SIG_SEB_STATUS_SEB_AUTOBRAKESTATUS = "SEB_AutoBrakeStatus";
export const SIG_SEB_STATUS_ERROR_STATUS = "error_status";
export const SIG_SEB_STATUS_STROKE_VALUE = "stroke_value";
export const SIG_SEB_STATUS_PRESSURE_VALUE = "pressure_value";
export const SIG_SEB_STATUS_ANGLE_VALUE = "angle_value";
export const SIG_SEB_STATUS_SEB_ROLLCNTENSTATUS = "SEB_RollCntEnStatus";
export const SIG_SEB_STATUS_SEB_CHECKSUMENSTATUS = "SEB_ChecksumEnStatus";
export const SIG_SEB_STATUS_ROLLING_COUNTER = "rolling_counter";
export const SIG_SEB_STATUS_CHECKSUM = "checksum";
export const ID_SEB_ErrInfo = "0x731";
export const SIG_SEB_ErrInfo_SEB_ECUUNDERVOLT = "SEB_ECUUnderVolt";
export const SIG_SEB_ErrInfo_SEB_ECUOVERVOLT = "SEB_ECUOverVolt";
export const SIG_SEB_ErrInfo_SEB_CANCOMERR = "SEB_CanComErr";
export const SIG_SEB_ErrInfo_SEB_ECUTEMPERR = "SEB_ECUTempErr";
export const SIG_SEB_ErrInfo_SEB_DOMAINSC = "SEB_DomainSC";
export const SIG_SEB_ErrInfo_SEB_DOMAINV = "SEB_DomainV";
export const SIG_SEB_ErrInfo_SEB_DOMAINT = "SEB_DomainT";
export const SIG_SEB_ErrInfo_SEB_ANGLEP_OC = "SEB_AngleP_OC";
export const SIG_SEB_ErrInfo_SEB_ANGLEP_AF = "SEB_AngleP_AF";
export const SIG_SEB_ErrInfo_SEB_ANGLES_OC = "SEB_AngleS_OC";
export const SIG_SEB_ErrInfo_SEB_ANGLES_AF = "SEB_AngleS_AF";
export const SIG_SEB_ErrInfo_SEB_NOPRESENSOR = "SEB_NoPreSensor";
export const SIG_SEB_ErrInfo_SEB_SENSORUCL = "SEB_SensorUCL";
export const SIG_SEB_ErrInfo_SEB_ALIGNMENTERR = "SEB_AlignmentErr";
export const SIG_SEB_ErrInfo_SEB_ANGLEOVER = "SEB_AngleOver";
export const SIG_SEB_ErrInfo_SEB_MTRSTALL = "SEB_MtrStall";
export const SIG_SEB_ErrInfo_SEB_MTRDC = "SEB_MtrDC";
export const SIG_SEB_ErrInfo_SEB_OILERR = "SEB_OilErr";
export const SIG_SEB_ErrInfo_SEB_INITOIL = "SEB_InitOil";
export const SIG_SEB_ErrInfo_SEB_SENTVALUE = "SEB_SentValue";
export const SIG_SEB_ErrInfo_SEB_MTRNOLOAD = "SEB_MtrNoLoad";
export const SIG_SEB_ErrInfo_SEB_PRESENSOROVER = "SEB_PreSensorOver";
export const SIG_SEB_ErrInfo_SEB_LOWVOLTCHARGING = "SEB_LowVoltCharging";
export const ID_SEB_Version = "0x741";
export const SIG_SEB_Version_SEB_SW_VERSION = "SEB_SW_Version";
export const SIG_SEB_Version_SEB_HW_VERSION = "SEB_HW_Version";
export const ID_VCU_SEB_REQ = "0x7B9";
export const SIG_VCU_SEB_REQ_ALIGN_ENABLE = "align_enable";
export const SIG_VCU_SEB_REQ_CONTROL_ENABLE = "control_enable";
export const SIG_VCU_SEB_REQ_CONTROL_MODE = "control_mode";
export const SIG_VCU_SEB_REQ_AUTO_BRAKE = "auto_brake";
export const SIG_VCU_SEB_REQ_STROKE_REQ = "stroke_req";
export const SIG_VCU_SEB_REQ_PRESSURE_REQ = "pressure_req";
export const SIG_VCU_SEB_REQ_SEB_ROLLCNTENABLE = "SEB_RollCntEnable";
export const SIG_VCU_SEB_REQ_SEB_CHECKSUMENABLE = "SEB_ChecksumEnable";
export const SIG_VCU_SEB_REQ_ROLLING_COUNTER = "rolling_counter";
export const SIG_VCU_SEB_REQ_CHECKSUM = "checksum";
export const SIG_RT_HEARTBEAT_RT_ALIVECTR = "RT_AliveCtr";
export const SIG_RT_HEARTBEAT_RT_HEALTHFLAGS = "RT_HealthFlags";
export const ID_SYS_HEARTBEAT = "0x7FE";
export const SIG_SYS_HEARTBEAT_SYS_ALIVECTR = "SYS_AliveCtr";
export const SIG_SYS_HEARTBEAT_SYS_HEALTHFLAGS = "SYS_HealthFlags";

export const CAN_MESSAGES: InternalCanMessageDef[] = [
  {
    "bus": "high",
    "id": "0x001",
    "name": "SAFETY_ESTOP",
    "sender": "Any",
    "receivers": [
      "SYS",
      "Host",
      "MTR",
      "DCDC"
    ],
    "comment": "DLC=0 \u00e2\u20ac\u201d the frame ID itself is the ESTOP signal. Any node can send (RT is nominal). Bridged bidirectionally. Highest priority CAN frame.",
    "dlc": 0,
    "period": "0ms",
    "injectable": true,
    "byteOrder": "motorola",
    "fields": []
  },
  {
    "bus": "high",
    "id": "0x011",
    "name": "SYS_SAFETY_STS",
    "sender": "SYS",
    "receivers": [
      "RT",
      "Host"
    ],
    "comment": "Forwarded low\u00e2\u2020\u2019high by RT. Same payload on both buses. DLC=3 adds light state (v0.0.5).",
    "dlc": 3,
    "period": "200ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "estop_active",
        "label": "SYS_EstopActive",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "heartbeat_ok",
        "label": "SYS_HeartbeatOk",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "light_left",
        "label": "SYS_LightLeft",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "light_right",
        "label": "SYS_LightRight",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "light_brake",
        "label": "SYS_LightBrake",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "light_head",
        "label": "SYS_LightHead",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x111",
    "name": "HMI_MODE_REQ",
    "sender": "HMI",
    "receivers": [
      "SYS",
      "Host"
    ],
    "comment": "HMI mode request. 1Hz periodic heartbeat. Forwarded high\u00e2\u2020\u2019low by RT.",
    "dlc": 2,
    "period": "1000ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "HMI_ReqMode",
        "label": "HMI_ReqMode",
        "kind": "enum",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 2,
        "options": [
          {
            "value": 0,
            "label": "MANUAL"
          },
          {
            "value": 1,
            "label": "AUTO"
          },
          {
            "value": 2,
            "label": "PURE_SIM"
          }
        ]
      },
      {
        "key": "rolling_counter",
        "label": "HMI_ModeAlive",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x112",
    "name": "HMI_PWR_REQ",
    "sender": "HMI",
    "receivers": [
      "SYS"
    ],
    "comment": "HMI power request. 1Hz periodic heartbeat. Forwarded high\u00e2\u2020\u2019low by RT.",
    "dlc": 2,
    "period": "1000ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "HMI_ReqStart",
        "label": "HMI_ReqStart",
        "kind": "enum",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1,
        "options": [
          {
            "value": 0,
            "label": "False"
          },
          {
            "value": 1,
            "label": "True"
          }
        ]
      },
      {
        "key": "rolling_counter",
        "label": "HMI_PwrAlive",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x120",
    "name": "SYS_THROTTLE_STS",
    "sender": "MTR",
    "receivers": [
      "RT",
      "Host"
    ],
    "comment": "Current vehicle speed from MTR STM32. Forwarded low\u00e2\u2020\u2019high by RT. SYS_ prefix is historical.",
    "dlc": 2,
    "period": "10ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "speed_mmps",
        "label": "SYS_ThrottleSpeed",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 1,
        "_offset": 0,
        "unit": "mm/s",
        "min": -500,
        "max": 3000
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x206",
    "name": "MTR_MOTOR_FBK",
    "sender": "MTR",
    "receivers": [
      "RT",
      "SYS",
      "Host"
    ],
    "comment": "Motor feedback from STM32. Forwarded low\u00e2\u2020\u2019high by RT per gateway rules.",
    "dlc": 4,
    "period": "20ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "actual_speed_mmps",
        "label": "MTR_ActualSpeed",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 1,
        "_offset": 0,
        "unit": "mm/s",
        "min": -500,
        "max": 3000
      },
      {
        "key": "gear_state",
        "label": "MTR_GearState",
        "kind": "number",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 3
      },
      {
        "key": "fault_flags",
        "label": "MTR_FaultFlags",
        "kind": "number",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x210",
    "name": "RT_STATE_RPT",
    "sender": "RT",
    "receivers": [
      "Host",
      "SYS"
    ],
    "comment": "RT state report to Host (high bus) and SYS (low bus). SYS monitors safety_state for takeover detection and RT health.",
    "dlc": 6,
    "period": "100ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "mode",
        "label": "RT_Mode",
        "kind": "enum",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 2,
        "options": [
          {
            "value": 0,
            "label": "MANUAL"
          },
          {
            "value": 1,
            "label": "AUTO"
          },
          {
            "value": 2,
            "label": "ESTOP"
          }
        ]
      },
      {
        "key": "safety_state",
        "label": "RT_SafetyState",
        "kind": "enum",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 2,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 2,
        "options": [
          {
            "value": 0,
            "label": "Normal"
          },
          {
            "value": 1,
            "label": "Warning"
          },
          {
            "value": 2,
            "label": "Fault"
          }
        ]
      },
      {
        "key": "estop_reason",
        "label": "RT_EstopReason",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 4,
        "_size": 4,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 7
      },
      {
        "key": "reversing",
        "label": "RT_Reversing",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "rx_overflow",
        "label": "RT_RxOverflow",
        "kind": "number",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      },
      {
        "key": "task_health",
        "label": "RT_TaskHealth",
        "kind": "number",
        "_byte": 4,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      },
      {
        "key": "steer_state",
        "label": "RT_SteerState",
        "kind": "number",
        "_byte": 5,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 5
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x220",
    "name": "RT_PID_RPT",
    "sender": "RT",
    "receivers": [
      "Host"
    ],
    "comment": "RESERVED, inactive. PID telemetry for Host debugging.",
    "dlc": 6,
    "period": "100ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "speed_setpoint",
        "label": "RT_PidSetpoint",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 1.0,
        "_offset": 0.0,
        "unit": "mm/s"
      },
      {
        "key": "speed_measured",
        "label": "RT_PidMeasured",
        "kind": "number",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 1.0,
        "_offset": 0.0,
        "unit": "mm/s"
      },
      {
        "key": "pid_output",
        "label": "RT_PidOutput",
        "kind": "number",
        "_byte": 4,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 1.0,
        "_offset": 0.0
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x300",
    "name": "HOST_DRIVE_CMD",
    "sender": "Host",
    "receivers": [
      "RT"
    ],
    "comment": "Host (Jetson Orin) Autoware.Auto drive command -> RT. High bus only.",
    "dlc": 8,
    "period": "10ms",
    "injectable": true,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "speed_mmps",
        "label": "HOST_DriveSpeed",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 32,
        "_type": "signed",
        "_factor": 1,
        "_offset": 0,
        "unit": "mm/s",
        "min": -500,
        "max": 3000
      },
      {
        "key": "yaw_rate_mrad_s",
        "label": "HOST_YawRate",
        "kind": "number",
        "_byte": 4,
        "_bit_offset": 0,
        "_size": 24,
        "_type": "signed",
        "_factor": 1,
        "_offset": 0,
        "unit": "mrad/s",
        "min": -3000,
        "max": 3000
      },
      {
        "key": "gear",
        "label": "HOST_Gear",
        "kind": "enum",
        "_byte": 7,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "unit": "enum",
        "min": 0,
        "max": 3,
        "options": [
          {
            "value": 0,
            "label": "N"
          },
          {
            "value": 1,
            "label": "D"
          },
          {
            "value": 2,
            "label": "S"
          },
          {
            "value": 3,
            "label": "R"
          }
        ]
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x301",
    "name": "HOST_BRAKE_REQ",
    "sender": "Host",
    "receivers": [
      "RT"
    ],
    "comment": "On demand. RT arbitrates: max(RT_computed, HOST_request) -> 0x205.",
    "dlc": 4,
    "period": "0ms",
    "injectable": true,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "brake_pressure_kpa",
        "label": "HOST_BrakePressure",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 32,
        "_type": "signed",
        "_factor": 1,
        "_offset": 0,
        "unit": "kPa",
        "min": 0,
        "max": 20000
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x302",
    "name": "HOST_LIGHT_CMD",
    "sender": "Host",
    "receivers": [
      "RT",
      "SYS"
    ],
    "comment": "Forwarded transparently high\u00e2\u2020\u2019low by RT.",
    "dlc": 1,
    "period": "0ms",
    "injectable": true,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "left_turn",
        "label": "HOST_LeftTurn",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "right_turn",
        "label": "HOST_RightTurn",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "brake_light",
        "label": "HOST_BrakeLight",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "headlight",
        "label": "HOST_Headlight",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x310",
    "name": "STEER_DIAG",
    "sender": "RT",
    "receivers": [
      "Host"
    ],
    "comment": "Steering telemetry to Host. v0.0.4 \u00e2\u20ac\u201d previously missing from DBC.",
    "dlc": 8,
    "period": "100ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "SteerDiag_Angle0_1deg",
        "label": "SteerDiag_Angle0_1deg",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.1,
        "_offset": -3000,
        "unit": "deg"
      },
      {
        "key": "SteerDiag_Fault",
        "label": "SteerDiag_Fault",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "SteerDiag_MotorCurrent",
        "label": "SteerDiag_MotorCurrent",
        "kind": "number",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.01,
        "_offset": 0,
        "unit": "A"
      },
      {
        "key": "SteerDiag_ECUTemp",
        "label": "SteerDiag_ECUTemp",
        "kind": "number",
        "_byte": 5,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.1,
        "_offset": 0,
        "unit": "degC"
      },
      {
        "key": "SteerDiag_Reserved",
        "label": "SteerDiag_Reserved",
        "kind": "number",
        "_byte": 7,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 0
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x311",
    "name": "BRAKE_DIAG",
    "sender": "RT",
    "receivers": [
      "Host"
    ],
    "comment": "Brake telemetry to Host. v0.0.4 \u00e2\u20ac\u201d previously missing from DBC.",
    "dlc": 8,
    "period": "100ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "BrakeDiag_PressureRaw",
        "label": "BrakeDiag_PressureRaw",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.05,
        "_offset": 0,
        "unit": "MPa"
      },
      {
        "key": "BrakeDiag_Fault",
        "label": "BrakeDiag_Fault",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "BrakeDiag_MotorCurrent",
        "label": "BrakeDiag_MotorCurrent",
        "kind": "number",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.01,
        "_offset": 0,
        "unit": "A"
      },
      {
        "key": "BrakeDiag_ECUTemp",
        "label": "BrakeDiag_ECUTemp",
        "kind": "number",
        "_byte": 5,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.1,
        "_offset": 0,
        "unit": "degC"
      },
      {
        "key": "BrakeDiag_Reserved",
        "label": "BrakeDiag_Reserved",
        "kind": "number",
        "_byte": 7,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 0
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x400",
    "name": "HOST_OBSTACLE_DIST",
    "sender": "Host",
    "receivers": [
      "RT"
    ],
    "comment": "Host sends min obstacle distance (from LiDAR/camera perception) to RT at 10 Hz. High bus only.",
    "dlc": 4,
    "period": "100ms",
    "injectable": true,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "distance_mm",
        "label": "HOST_ObstacleDistance",
        "kind": "enum",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 32,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "unit": "mm",
        "min": 0,
        "max": 4294967295,
        "options": [
          {
            "value": 4294967295,
            "label": "clear"
          }
        ]
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x600",
    "name": "SYS_DIAG_RPT",
    "sender": "SYS",
    "receivers": [
      "RT",
      "Host"
    ],
    "comment": "SYS diagnostics report. Forwarded low\u00e2\u2020\u2019high by RT.",
    "dlc": 8,
    "period": "1000ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "SYS_DiagMode",
        "label": "SYS_DiagMode",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 2
      },
      {
        "key": "SYS_DiagBrakeEngaged",
        "label": "SYS_DiagBrakeEngaged",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "SYS_DiagBrakeFault",
        "label": "SYS_DiagBrakeFault",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "SYS_DiagHeartbeatOk",
        "label": "SYS_DiagHeartbeatOk",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "SYS_DiagEstopActive",
        "label": "SYS_DiagEstopActive",
        "kind": "boolean",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "SYS_DiagFreeHeapKb",
        "label": "SYS_DiagFreeHeapKb",
        "kind": "number",
        "_byte": 4,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "unit": "KB",
        "min": 0,
        "max": 65535
      },
      {
        "key": "SYS_DiagTec",
        "label": "SYS_DiagTec",
        "kind": "number",
        "_byte": 6,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      },
      {
        "key": "SYS_DiagRec",
        "label": "SYS_DiagRec",
        "kind": "number",
        "_byte": 7,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x7FC",
    "name": "HOST_HEARTBEAT",
    "sender": "Host",
    "receivers": [
      "RT"
    ],
    "comment": "Not bridged, high bus only. Loss triggers controlled stop, not ESTOP.",
    "dlc": 2,
    "period": "500ms",
    "injectable": true,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "alive_ctr",
        "label": "Host_AliveCtr",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      },
      {
        "key": "health_flags",
        "label": "Host_HealthFlags",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "high",
    "id": "0x7FD",
    "name": "RT_HEARTBEAT",
    "sender": "RT",
    "receivers": [
      "Host",
      "SYS"
    ],
    "comment": "RT sends independently on both buses (per-bus, NOT bridged). Separate counters. This is the high-bus instance.",
    "dlc": 2,
    "period": "500ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "alive_ctr",
        "label": "RT_AliveCtr",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      },
      {
        "key": "health_flags",
        "label": "RT_HealthFlags",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x001",
    "name": "SAFETY_ESTOP",
    "sender": "Any",
    "receivers": [
      "SYS",
      "Host",
      "MTR",
      "DCDC"
    ],
    "comment": "DLC=0 \u00e2\u20ac\u201d the frame ID itself is the ESTOP signal. Any node can send (RT is nominal). Bridged bidirectionally. Highest priority CAN frame.",
    "dlc": 0,
    "period": "0ms",
    "injectable": true,
    "byteOrder": "motorola",
    "fields": []
  },
  {
    "bus": "low",
    "id": "0x011",
    "name": "SYS_SAFETY_STS",
    "sender": "SYS",
    "receivers": [
      "RT",
      "Host"
    ],
    "comment": "Forwarded low\u00e2\u2020\u2019high by RT. Same payload on both buses. DLC=3 adds light state (v0.0.5).",
    "dlc": 3,
    "period": "200ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "estop_active",
        "label": "SYS_EstopActive",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "heartbeat_ok",
        "label": "SYS_HeartbeatOk",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "light_left",
        "label": "SYS_LightLeft",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "light_right",
        "label": "SYS_LightRight",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "light_brake",
        "label": "SYS_LightBrake",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "light_head",
        "label": "SYS_LightHead",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x012",
    "name": "SYS_DCDC_CMD",
    "sender": "SYS",
    "receivers": [
      "DCDC"
    ],
    "comment": "DC-DC converter control. Low bus only.",
    "dlc": 1,
    "period": "0ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "SYS_DcdcEnable",
        "label": "SYS_DcdcEnable",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x110",
    "name": "SYS_MODE_CMD",
    "sender": "SYS",
    "receivers": [
      "RT",
      "MTR"
    ],
    "comment": "0=Manual, 1=Auto, 2=ESTOP. Low bus only. MTR needs mode for pass-through vs CAN control.",
    "dlc": 1,
    "period": "0ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "mode",
        "label": "SYS_Mode",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 2
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x111",
    "name": "HMI_MODE_REQ",
    "sender": "HMI",
    "receivers": [
      "SYS",
      "Host"
    ],
    "comment": "HMI mode request. 1Hz periodic heartbeat. Forwarded high\u00e2\u2020\u2019low by RT.",
    "dlc": 2,
    "period": "1000ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "HMI_ReqMode",
        "label": "HMI_ReqMode",
        "kind": "enum",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 2,
        "options": [
          {
            "value": 0,
            "label": "MANUAL"
          },
          {
            "value": 1,
            "label": "AUTO"
          },
          {
            "value": 2,
            "label": "PURE_SIM"
          }
        ]
      },
      {
        "key": "rolling_counter",
        "label": "HMI_ModeAlive",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x112",
    "name": "HMI_PWR_REQ",
    "sender": "HMI",
    "receivers": [
      "SYS"
    ],
    "comment": "HMI power request. 1Hz periodic heartbeat. Forwarded high\u00e2\u2020\u2019low by RT.",
    "dlc": 2,
    "period": "1000ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "HMI_ReqStart",
        "label": "HMI_ReqStart",
        "kind": "enum",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1,
        "options": [
          {
            "value": 0,
            "label": "False"
          },
          {
            "value": 1,
            "label": "True"
          }
        ]
      },
      {
        "key": "rolling_counter",
        "label": "HMI_PwrAlive",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x120",
    "name": "SYS_THROTTLE_STS",
    "sender": "MTR",
    "receivers": [
      "RT",
      "Host"
    ],
    "comment": "Current vehicle speed from MTR STM32. Forwarded low\u00e2\u2020\u2019high by RT. SYS_ prefix is historical.",
    "dlc": 2,
    "period": "10ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "speed_mmps",
        "label": "SYS_ThrottleSpeed",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 1,
        "_offset": 0,
        "unit": "mm/s",
        "min": -500,
        "max": 3000
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x169",
    "name": "VCU_SES_REQ",
    "sender": "RT",
    "receivers": [
      "EPS_C"
    ],
    "comment": "steer-by-wire unit command. 50 Hz continuous. Byte 5 overlap: Speed[15:8] shares with security nibble per CSV.",
    "dlc": 8,
    "period": "20ms",
    "injectable": false,
    "byteOrder": "intel",
    "fields": [
      {
        "key": "alignment_enable",
        "label": "SES_AlignEnable",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "control_enable",
        "label": "SES_CtrlEnable",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "target_angle",
        "label": "SES_TgtStrAngle",
        "kind": "number",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 0.1,
        "_offset": -3000,
        "unit": "deg",
        "min": -700,
        "max": 700
      },
      {
        "key": "target_speed",
        "label": "SES_TgtStrAngleSpd",
        "kind": "number",
        "_byte": 4,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true,
        "unit": "deg/s",
        "min": 125,
        "max": 525
      },
      {
        "key": "SES_RollCntEnable",
        "label": "SES_RollCntEnable",
        "kind": "boolean",
        "_byte": 5,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true
      },
      {
        "key": "SES_ChecksumEnable",
        "label": "SES_ChecksumEnable",
        "kind": "boolean",
        "_byte": 5,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true
      },
      {
        "key": "rolling_counter",
        "label": "SES_RollCnt",
        "kind": "number",
        "_byte": 5,
        "_bit_offset": 4,
        "_size": 4,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true,
        "min": 0,
        "max": 15
      },
      {
        "key": "SES_VehSpd",
        "label": "SES_VehSpd",
        "kind": "number",
        "_byte": 6,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "unit": "km/h",
        "min": 0,
        "max": 255
      },
      {
        "key": "checksum",
        "label": "SES_Checksum",
        "kind": "number",
        "_byte": 7,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x201",
    "name": "SES_STATUS",
    "sender": "EPS_C",
    "receivers": [
      "RT"
    ],
    "comment": "steer-by-wire unit status feedback. 100 Hz. Byte 5 overlap: StrAngleSpd[15:8] / Torq share byte 5 per CSV.",
    "dlc": 8,
    "period": "10ms",
    "injectable": false,
    "byteOrder": "intel",
    "fields": [
      {
        "key": "angle_status",
        "label": "SES_AngleStatus",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_CtrlModeStatus",
        "label": "SES_CtrlModeStatus",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 1,
        "_size": 2,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 3
      },
      {
        "key": "error_status",
        "label": "SES_ErrorStatus",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 6,
        "_size": 2,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 3
      },
      {
        "key": "str_angle",
        "label": "SES_StrAngle",
        "kind": "number",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.1,
        "_offset": -3000,
        "unit": "deg",
        "min": -700,
        "max": 700
      },
      {
        "key": "tgt_angle_spd",
        "label": "SES_TgtStrAngleSpd_FB",
        "kind": "number",
        "_byte": 4,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 0.5,
        "_offset": 0,
        "multiplexed": true,
        "unit": "deg/s",
        "min": 0,
        "max": 1480
      },
      {
        "key": "SES_SteeringTorq",
        "label": "SES_SteeringTorq",
        "kind": "number",
        "_byte": 5,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 0.1,
        "_offset": -12.1,
        "multiplexed": true,
        "unit": "Nm",
        "min": -12,
        "max": 12
      },
      {
        "key": "SES_RollCntEnStatus",
        "label": "SES_RollCntEnStatus",
        "kind": "boolean",
        "_byte": 6,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_ChecksumEnStatus",
        "label": "SES_ChecksumEnStatus",
        "kind": "boolean",
        "_byte": 6,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "rolling_counter",
        "label": "SES_RollCntStatus",
        "kind": "number",
        "_byte": 6,
        "_bit_offset": 4,
        "_size": 4,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 15
      },
      {
        "key": "checksum",
        "label": "SES_ChecksumStatus",
        "kind": "number",
        "_byte": 7,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x202",
    "name": "SES_ErrInfo",
    "sender": "EPS_C",
    "receivers": [
      "RT"
    ],
    "comment": "steer-by-wire unit detailed fault flags. 8 L3 faults (redundant sensor loss) -> RT must escalate to ESTOP.",
    "dlc": 8,
    "period": "100ms",
    "injectable": false,
    "byteOrder": "intel",
    "fields": [
      {
        "key": "SES_ECUUnderVolt",
        "label": "SES_ECUUnderVolt",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_ECUOverVolt",
        "label": "SES_ECUOverVolt",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_CanComErr",
        "label": "SES_CanComErr",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_ECUTempErr",
        "label": "SES_ECUTempErr",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_DomainSC",
        "label": "SES_DomainSC",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 4,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_DomainV",
        "label": "SES_DomainV",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 5,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_DomainT",
        "label": "SES_DomainT",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 6,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_TempSensor",
        "label": "SES_TempSensor",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 7,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_AngleP_OC",
        "label": "SES_AngleP_OC",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_AngleP_AF",
        "label": "SES_AngleP_AF",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_AngleS_OC",
        "label": "SES_AngleS_OC",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_AngleS_AF",
        "label": "SES_AngleS_AF",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_SensorPow",
        "label": "SES_SensorPow",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 4,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_Alignment",
        "label": "SES_Alignment",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 5,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_OverAngle",
        "label": "SES_OverAngle",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 6,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_StrMtrStall",
        "label": "SES_StrMtrStall",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 7,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_MtrCurtFault",
        "label": "SES_MtrCurtFault",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_SensorCL",
        "label": "SES_SensorCL",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_TorqT1_OC",
        "label": "SES_TorqT1_OC",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_TorqT1_AF",
        "label": "SES_TorqT1_AF",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_TorqT2_OC",
        "label": "SES_TorqT2_OC",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 4,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_TorqT2_AF",
        "label": "SES_TorqT2_AF",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 5,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_SentAngle",
        "label": "SES_SentAngle",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 6,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_StrMtrIdling",
        "label": "SES_StrMtrIdling",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 7,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_EPROM",
        "label": "SES_EPROM",
        "kind": "boolean",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SES_VehSpdSnapshot",
        "label": "SES_VehSpdSnapshot",
        "kind": "number",
        "_byte": 7,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "unit": "km/h",
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x203",
    "name": "SES_Version",
    "sender": "EPS_C",
    "receivers": [
      "RT"
    ],
    "comment": "steer-by-wire unit firmware version. Log on boot for compatibility check.",
    "dlc": 8,
    "period": "1000ms",
    "injectable": false,
    "byteOrder": "intel",
    "fields": [
      {
        "key": "SES_SW_Version",
        "label": "SES_SW_Version",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 0.01,
        "_offset": 0,
        "min": 0,
        "max": 2.55
      },
      {
        "key": "SES_HW_Version",
        "label": "SES_HW_Version",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 0.1,
        "_offset": 0,
        "min": 0,
        "max": 25.5
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x204",
    "name": "RT_DRIVE_CMD",
    "sender": "RT",
    "receivers": [
      "SYS",
      "MTR"
    ],
    "comment": "MTR receives for motor actuation. SYS receives for EGAS L2 monitoring. ID 0x204 avoids collision with EPS-C 0x202.",
    "dlc": 5,
    "period": "10ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "motor_speed_mmps",
        "label": "RT_MotorSpeed",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 32,
        "_type": "signed",
        "_factor": 1,
        "_offset": 0,
        "unit": "mm/s",
        "min": -500,
        "max": 3000
      },
      {
        "key": "gear",
        "label": "RT_Gear",
        "kind": "enum",
        "_byte": 4,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "unit": "enum",
        "min": 0,
        "max": 3,
        "options": [
          {
            "value": 0,
            "label": "N"
          },
          {
            "value": 1,
            "label": "D"
          },
          {
            "value": 2,
            "label": "S"
          },
          {
            "value": 3,
            "label": "R"
          }
        ]
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x205",
    "name": "RT_BRAKE_CMD",
    "sender": "RT",
    "receivers": [
      "SYS"
    ],
    "comment": "RT max-select: max(rt_obstacle, host_0x301) -> SYS SEB cmd.",
    "dlc": 4,
    "period": "20ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "brake_pressure_kpa",
        "label": "RT_BrakePressure",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 32,
        "_type": "signed",
        "_factor": 1,
        "_offset": 0,
        "unit": "kPa",
        "min": 0,
        "max": 20000
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x206",
    "name": "MTR_MOTOR_FBK",
    "sender": "MTR",
    "receivers": [
      "RT",
      "SYS",
      "Host"
    ],
    "comment": "Motor feedback from STM32. Forwarded low\u00e2\u2020\u2019high by RT per gateway rules.",
    "dlc": 4,
    "period": "20ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "actual_speed_mmps",
        "label": "MTR_ActualSpeed",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 1,
        "_offset": 0,
        "unit": "mm/s",
        "min": -500,
        "max": 3000
      },
      {
        "key": "gear_state",
        "label": "MTR_GearState",
        "kind": "number",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 3
      },
      {
        "key": "fault_flags",
        "label": "MTR_FaultFlags",
        "kind": "number",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x302",
    "name": "HOST_LIGHT_CMD",
    "sender": "Host",
    "receivers": [
      "RT",
      "SYS"
    ],
    "comment": "Forwarded transparently high\u00e2\u2020\u2019low by RT.",
    "dlc": 1,
    "period": "0ms",
    "injectable": true,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "left_turn",
        "label": "HOST_LeftTurn",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "right_turn",
        "label": "HOST_RightTurn",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "brake_light",
        "label": "HOST_BrakeLight",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "headlight",
        "label": "HOST_Headlight",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x600",
    "name": "SYS_DIAG_RPT",
    "sender": "SYS",
    "receivers": [
      "RT",
      "Host"
    ],
    "comment": "SYS diagnostics report. Forwarded low\u00e2\u2020\u2019high by RT.",
    "dlc": 8,
    "period": "1000ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "SYS_DiagMode",
        "label": "SYS_DiagMode",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 2
      },
      {
        "key": "SYS_DiagBrakeEngaged",
        "label": "SYS_DiagBrakeEngaged",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "SYS_DiagBrakeFault",
        "label": "SYS_DiagBrakeFault",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "SYS_DiagHeartbeatOk",
        "label": "SYS_DiagHeartbeatOk",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "SYS_DiagEstopActive",
        "label": "SYS_DiagEstopActive",
        "kind": "boolean",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 1
      },
      {
        "key": "SYS_DiagFreeHeapKb",
        "label": "SYS_DiagFreeHeapKb",
        "kind": "number",
        "_byte": 4,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "unit": "KB",
        "min": 0,
        "max": 65535
      },
      {
        "key": "SYS_DiagTec",
        "label": "SYS_DiagTec",
        "kind": "number",
        "_byte": 6,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      },
      {
        "key": "SYS_DiagRec",
        "label": "SYS_DiagRec",
        "kind": "number",
        "_byte": 7,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x6FA",
    "name": "SES_Test",
    "sender": "EPS_C",
    "receivers": [
      "RT"
    ],
    "comment": "steer-by-wire unit telemetry. 100 Hz. Bytes 0,7 reserved. Narrower ranges than brake SEB_Test.",
    "dlc": 8,
    "period": "10ms",
    "injectable": false,
    "byteOrder": "intel",
    "fields": [
      {
        "key": "SES_MtrCurt",
        "label": "SES_MtrCurt",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 0.0078125,
        "_offset": 0,
        "unit": "A",
        "min": 0,
        "max": 60
      },
      {
        "key": "SES_ECUTemp",
        "label": "SES_ECUTemp",
        "kind": "number",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.5,
        "_offset": 0,
        "unit": "degC",
        "min": 0,
        "max": 255
      },
      {
        "key": "SES_PowVolt",
        "label": "SES_PowVolt",
        "kind": "number",
        "_byte": 5,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.00390625,
        "_offset": 0,
        "unit": "V",
        "min": 0,
        "max": 18
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x6FB",
    "name": "SEB_Test",
    "sender": "SEB",
    "receivers": [
      "SYS",
      "RT"
    ],
    "comment": "brake-by-wire unit telemetry. 100 Hz. RT monitors for 0x311 BRAKE_DIAG population.",
    "dlc": 8,
    "period": "10ms",
    "injectable": false,
    "byteOrder": "intel",
    "fields": [
      {
        "key": "SEB_MtrCurr",
        "label": "SEB_MtrCurr",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 0.0078125,
        "_offset": 0,
        "unit": "A",
        "min": -255,
        "max": 255
      },
      {
        "key": "SEB_ECUTemp",
        "label": "SEB_ECUTemp",
        "kind": "number",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.5,
        "_offset": -40,
        "unit": "degC",
        "min": -40,
        "max": 215
      },
      {
        "key": "SEB_PowVolt",
        "label": "SEB_PowVolt",
        "kind": "number",
        "_byte": 5,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 0.00390625,
        "_offset": 0,
        "unit": "V",
        "min": 0,
        "max": 32
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x721",
    "name": "SEB_STATUS",
    "sender": "SEB",
    "receivers": [
      "SYS",
      "RT"
    ],
    "comment": "brake-by-wire unit status feedback. 100 Hz. RT monitors pressure for 0x311 BRAKE_DIAG. SYS usage: boot sync -> read StrokeValue; active -> confirm AlignStatus==1.",
    "dlc": 8,
    "period": "10ms",
    "injectable": false,
    "byteOrder": "intel",
    "fields": [
      {
        "key": "alignment_status",
        "label": "SEB_AlignStatus",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "control_enable_sts",
        "label": "SEB_CtrlEnStatus",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "control_mode_sts",
        "label": "SEB_CtrlModeStatus",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 2,
        "_size": 2,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 3
      },
      {
        "key": "SEB_AutoBrakeStatus",
        "label": "SEB_AutoBrakeStatus",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 4,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "error_status",
        "label": "SEB_ErrorStatus",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 6,
        "_size": 2,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 3
      },
      {
        "key": "stroke_value",
        "label": "SEB_StrokeValue",
        "kind": "number",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true,
        "unit": "raw",
        "min": 0,
        "max": 65535
      },
      {
        "key": "pressure_value",
        "label": "SEB_PressureValue",
        "kind": "number",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true,
        "unit": "raw",
        "min": 0,
        "max": 100
      },
      {
        "key": "angle_value",
        "label": "SEB_AngleValue",
        "kind": "number",
        "_byte": 5,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "signed",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true,
        "min": -32768,
        "max": 32767
      },
      {
        "key": "SEB_RollCntEnStatus",
        "label": "SEB_RollCntEnStatus",
        "kind": "boolean",
        "_byte": 6,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true
      },
      {
        "key": "SEB_ChecksumEnStatus",
        "label": "SEB_ChecksumEnStatus",
        "kind": "boolean",
        "_byte": 6,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true
      },
      {
        "key": "rolling_counter",
        "label": "SEB_RollCntStatus",
        "kind": "number",
        "_byte": 6,
        "_bit_offset": 4,
        "_size": 4,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true,
        "min": 0,
        "max": 15
      },
      {
        "key": "checksum",
        "label": "SEB_ChecksumStatus",
        "kind": "number",
        "_byte": 7,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x731",
    "name": "SEB_ErrInfo",
    "sender": "SEB",
    "receivers": [
      "SYS"
    ],
    "comment": "brake-by-wire unit detailed fault flags. 16 of 23 faults are L3 -> SYS must escalate to ESTOP.",
    "dlc": 8,
    "period": "100ms",
    "injectable": false,
    "byteOrder": "intel",
    "fields": [
      {
        "key": "SEB_ECUUnderVolt",
        "label": "SEB_ECUUnderVolt",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_ECUOverVolt",
        "label": "SEB_ECUOverVolt",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_CanComErr",
        "label": "SEB_CanComErr",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_ECUTempErr",
        "label": "SEB_ECUTempErr",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_DomainSC",
        "label": "SEB_DomainSC",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 4,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_DomainV",
        "label": "SEB_DomainV",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 5,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_DomainT",
        "label": "SEB_DomainT",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 6,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_AngleP_OC",
        "label": "SEB_AngleP_OC",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 7,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_AngleP_AF",
        "label": "SEB_AngleP_AF",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_AngleS_OC",
        "label": "SEB_AngleS_OC",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_AngleS_AF",
        "label": "SEB_AngleS_AF",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_NoPreSensor",
        "label": "SEB_NoPreSensor",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_SensorUCL",
        "label": "SEB_SensorUCL",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 5,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_AlignmentErr",
        "label": "SEB_AlignmentErr",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 6,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_AngleOver",
        "label": "SEB_AngleOver",
        "kind": "boolean",
        "_byte": 1,
        "_bit_offset": 7,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_MtrStall",
        "label": "SEB_MtrStall",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_MtrDC",
        "label": "SEB_MtrDC",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_OilErr",
        "label": "SEB_OilErr",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_InitOil",
        "label": "SEB_InitOil",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 4,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_SentValue",
        "label": "SEB_SentValue",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 5,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_MtrNoLoad",
        "label": "SEB_MtrNoLoad",
        "kind": "boolean",
        "_byte": 2,
        "_bit_offset": 6,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_PreSensorOver",
        "label": "SEB_PreSensorOver",
        "kind": "boolean",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_LowVoltCharging",
        "label": "SEB_LowVoltCharging",
        "kind": "boolean",
        "_byte": 3,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x741",
    "name": "SEB_Version",
    "sender": "SEB",
    "receivers": [
      "SYS"
    ],
    "comment": "brake-by-wire unit firmware version. Log on boot for compatibility check.",
    "dlc": 8,
    "period": "1000ms",
    "injectable": false,
    "byteOrder": "intel",
    "fields": [
      {
        "key": "SEB_SW_Version",
        "label": "SEB_SW_Version",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 0.01,
        "_offset": 0,
        "min": 0,
        "max": 2.55
      },
      {
        "key": "SEB_HW_Version",
        "label": "SEB_HW_Version",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 0.1,
        "_offset": 0,
        "min": 0,
        "max": 25.5
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x7B9",
    "name": "VCU_SEB_REQ",
    "sender": "SYS",
    "receivers": [
      "SEB"
    ],
    "comment": "brake-by-wire unit brake command. 50 Hz continuous. Byte 3 mode-mux: Stroke[15:8] in Mode 0, Pressure in Mode 1.",
    "dlc": 8,
    "period": "20ms",
    "injectable": false,
    "byteOrder": "intel",
    "fields": [
      {
        "key": "align_enable",
        "label": "SEB_AlignEnable",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "control_enable",
        "label": "SEB_CtrlEnable",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "control_mode",
        "label": "SEB_CtrlMode",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 2,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "auto_brake",
        "label": "SEB_AutoBrake",
        "kind": "boolean",
        "_byte": 0,
        "_bit_offset": 3,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "stroke_req",
        "label": "SEB_StrokeReq",
        "kind": "number",
        "_byte": 2,
        "_bit_offset": 0,
        "_size": 16,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true,
        "unit": "raw",
        "min": 0,
        "max": 65535
      },
      {
        "key": "pressure_req",
        "label": "SEB_PressureReq",
        "kind": "number",
        "_byte": 3,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "multiplexed": true,
        "unit": "raw",
        "min": 0,
        "max": 100
      },
      {
        "key": "SEB_RollCntEnable",
        "label": "SEB_RollCntEnable",
        "kind": "boolean",
        "_byte": 6,
        "_bit_offset": 0,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "SEB_ChecksumEnable",
        "label": "SEB_ChecksumEnable",
        "kind": "boolean",
        "_byte": 6,
        "_bit_offset": 1,
        "_size": 1,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0
      },
      {
        "key": "rolling_counter",
        "label": "SEB_RollCnt",
        "kind": "number",
        "_byte": 6,
        "_bit_offset": 4,
        "_size": 4,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 15
      },
      {
        "key": "checksum",
        "label": "SEB_Checksum",
        "kind": "number",
        "_byte": 7,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x7FD",
    "name": "RT_HEARTBEAT",
    "sender": "RT",
    "receivers": [
      "Host",
      "SYS"
    ],
    "comment": "RT sends independently on both buses (per-bus, NOT bridged). Separate counters. This is the low-bus instance.",
    "dlc": 2,
    "period": "500ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "RT_AliveCtr",
        "label": "RT_AliveCtr",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      },
      {
        "key": "RT_HealthFlags",
        "label": "RT_HealthFlags",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  },
  {
    "bus": "low",
    "id": "0x7FE",
    "name": "SYS_HEARTBEAT",
    "sender": "SYS",
    "receivers": [
      "RT"
    ],
    "comment": "Low bus only, never leaves low bus.",
    "dlc": 2,
    "period": "100ms",
    "injectable": false,
    "byteOrder": "motorola",
    "fields": [
      {
        "key": "SYS_AliveCtr",
        "label": "SYS_AliveCtr",
        "kind": "number",
        "_byte": 0,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      },
      {
        "key": "SYS_HealthFlags",
        "label": "SYS_HealthFlags",
        "kind": "number",
        "_byte": 1,
        "_bit_offset": 0,
        "_size": 8,
        "_type": "unsigned",
        "_factor": 1.0,
        "_offset": 0.0,
        "min": 0,
        "max": 255
      }
    ]
  }
];
