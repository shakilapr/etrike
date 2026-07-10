/**
 * CAN message metadata — auto-generated from shared/can/can_high.yaml + can_low.yaml.
 * DO NOT EDIT BY HAND. Regenerate: python shared/can/generate_can_ts.py
 */

import type { CanMessageDef, CanField, Bus, FieldKind } from "../can";

export const PROTOCOL_HASH = "6f9a7c9e43297b8a3be85248d40a40f2fc62f516f205833a183f28aad9dfe178";

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
export const ID_SYS_THROTTLE_STS = "0x120";
export const ID_MTR_MOTOR_FBK = "0x206";
export const ID_RT_STATE_RPT = "0x210";
export const ID_RT_PID_RPT = "0x220";
export const ID_HOST_DRIVE_CMD = "0x300";
export const ID_HOST_BRAKE_REQ = "0x301";
export const ID_HOST_LIGHT_CMD = "0x302";
export const ID_STEER_DIAG = "0x310";
export const ID_BRAKE_DIAG = "0x311";
export const ID_HOST_OBSTACLE_DIST = "0x400";
export const ID_SYS_DIAG_RPT = "0x600";
export const ID_HOST_HEARTBEAT = "0x7FC";
export const ID_RT_HEARTBEAT = "0x7FD";
export const ID_SYS_DCDC_CMD = "0x012";
export const ID_SYS_MODE_CMD = "0x110";
export const ID_VCU_SES_REQ = "0x169";
export const ID_SES_STATUS = "0x201";
export const ID_SES_ErrInfo = "0x202";
export const ID_SES_Version = "0x203";
export const ID_RT_DRIVE_CMD = "0x204";
export const ID_RT_BRAKE_CMD = "0x205";
export const ID_SES_Test = "0x6FA";
export const ID_SEB_Test = "0x6FB";
export const ID_SEB_STATUS = "0x721";
export const ID_SEB_ErrInfo = "0x731";
export const ID_SEB_Version = "0x741";
export const ID_VCU_SEB_REQ = "0x7B9";
export const ID_SYS_HEARTBEAT = "0x7FE";

export const CAN_MESSAGES: InternalCanMessageDef[] = [
  {
    "bus": "high",
    "id": "0x001",
    "name": "SAFETY_ESTOP",
    "sender": "Any",
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
    "dlc": 3,
    "period": "200ms",
    "injectable": true,
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
    "id": "0x120",
    "name": "SYS_THROTTLE_STS",
    "sender": "MTR",
    "dlc": 2,
    "period": "10ms",
    "injectable": true,
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
    "dlc": 4,
    "period": "20ms",
    "injectable": true,
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
    "dlc": 6,
    "period": "100ms",
    "injectable": true,
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
    "dlc": 6,
    "period": "100ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "100ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "100ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "1000ms",
    "injectable": true,
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
    "dlc": 2,
    "period": "500ms",
    "injectable": true,
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
    "dlc": 3,
    "period": "200ms",
    "injectable": true,
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
    "dlc": 1,
    "period": "0ms",
    "injectable": true,
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
    "dlc": 1,
    "period": "0ms",
    "injectable": true,
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
    "id": "0x120",
    "name": "SYS_THROTTLE_STS",
    "sender": "MTR",
    "dlc": 2,
    "period": "10ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "20ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "10ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "100ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "1000ms",
    "injectable": true,
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
    "dlc": 5,
    "period": "10ms",
    "injectable": true,
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
    "dlc": 4,
    "period": "20ms",
    "injectable": true,
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
    "dlc": 4,
    "period": "20ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "1000ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "10ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "10ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "10ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "100ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "1000ms",
    "injectable": true,
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
    "dlc": 8,
    "period": "20ms",
    "injectable": true,
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
    "dlc": 2,
    "period": "500ms",
    "injectable": true,
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
    "dlc": 2,
    "period": "100ms",
    "injectable": true,
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
