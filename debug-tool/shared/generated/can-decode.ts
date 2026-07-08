// AUTO-GENERATED - DO NOT EDIT
import { readI16BE, readU16BE, readI16LE, readU16LE, readI32BE, readU32BE, readU32LE, readI24BE } from '../src/read-helpers';

function normalizeBytes(data: number[]): number[] {
  const bytes = data.map((value) => Number(value) & 0xff);
  while (bytes.length < 8) bytes.push(0);
  return bytes;
}

export function decodeFrame(bus: "high"|"low", id: string, data: number[]): Record<string, unknown> {
  const bytes = normalizeBytes(data);
  const key = `${bus}:${id}`;
  switch (key) {
    case "high:0x011": {
      return {
        "SYS_EstopActive": bytes[0] ?? 0,
        "SYS_HeartbeatOk": bytes[1] ?? 0,
        "SYS_LightState": ((bytes[2] ?? 0) >> 0) & 0x0f,
      };
    }
    case "high:0x120": {
      return {
        "SYS_ThrottleSpeed": readI16BE(bytes, 0),
      };
    }
    case "high:0x206": {
      return {
        "MTR_ActualSpeed": readI16BE(bytes, 0),
        "MTR_GearState": bytes[2] ?? 0,
        "MTR_FaultFlags": bytes[3] ?? 0,
      };
    }
    case "high:0x210": {
      return {
        "RT_Mode": bytes[0] ?? 0,
        "RT_SafetyState": ((bytes[1] ?? 0) >> 0) & 0x03,
        "RT_EstopReason": ((bytes[1] ?? 0) >> 4) & 0x0f,
        "RT_Reversing": bytes[2] ?? 0,
        "RT_RxOverflow": bytes[3] ?? 0,
        "RT_TaskHealth": bytes[4] ?? 0,
        "RT_SteerState": bytes[5] ?? 0,
      };
    }
    case "high:0x220": {
      return {
        "RT_PidSetpoint": readI16BE(bytes, 0),
        "RT_PidMeasured": readI16BE(bytes, 2),
        "RT_PidOutput": readI16BE(bytes, 4),
      };
    }
    case "high:0x300": {
      return {
        "HOST_DriveSpeed": readI32BE(bytes, 0),
        "HOST_YawRate": readI24BE(bytes, 4),
        "HOST_Gear": bytes[7] ?? 0,
      };
    }
    case "high:0x301": {
      return {
        "HOST_BrakePressure": readI32BE(bytes, 0),
      };
    }
    case "high:0x302": {
      return {
        "HOST_LeftTurn": Boolean((bytes[0] ?? 0) & (1 << 0)),
        "HOST_RightTurn": Boolean((bytes[0] ?? 0) & (1 << 1)),
        "HOST_BrakeLight": Boolean((bytes[0] ?? 0) & (1 << 2)),
        "HOST_Headlight": Boolean((bytes[0] ?? 0) & (1 << 3)),
      };
    }
    case "high:0x310": {
      return {
        "SteerDiag_Angle0_1deg": ((readU16BE(bytes, 0) * 0.1) + -3000),
        "SteerDiag_Fault": bytes[2] ?? 0,
        "SteerDiag_MotorCurrent": (readU16BE(bytes, 3) * 0.01),
        "SteerDiag_ECUTemp": (readU16BE(bytes, 5) * 0.1),
        "SteerDiag_Reserved": bytes[7] ?? 0,
      };
    }
    case "high:0x311": {
      return {
        "BrakeDiag_PressureRaw": (readU16BE(bytes, 0) * 0.05),
        "BrakeDiag_Fault": bytes[2] ?? 0,
        "BrakeDiag_MotorCurrent": (readU16BE(bytes, 3) * 0.01),
        "BrakeDiag_ECUTemp": (readU16BE(bytes, 5) * 0.1),
        "BrakeDiag_Reserved": bytes[7] ?? 0,
      };
    }
    case "high:0x400": {
      return {
        "HOST_ObstacleDistance": readU32BE(bytes, 0),
      };
    }
    case "high:0x600": {
      return {
        "SYS_DiagMode": bytes[0] ?? 0,
        "SYS_DiagBrakeEngaged": Boolean((bytes[1] ?? 0) & (1 << 0)),
        "SYS_DiagBrakeFault": Boolean((bytes[1] ?? 0) & (1 << 1)),
        "SYS_DiagHeartbeatOk": bytes[2] ?? 0,
        "SYS_DiagEstopActive": bytes[3] ?? 0,
        "SYS_DiagFreeHeapKb": readU16BE(bytes, 4),
        "SYS_DiagTec": bytes[6] ?? 0,
        "SYS_DiagRec": bytes[7] ?? 0,
      };
    }
    case "high:0x7FC": {
      return {
        "Host_AliveCtr": bytes[0] ?? 0,
      };
    }
    case "high:0x7FD": {
      return {
        "RT_AliveCtr": bytes[0] ?? 0,
        "RT_HealthFlags": bytes[1] ?? 0,
      };
    }
    case "low:0x011": {
      return {
        "SYS_EstopActive": bytes[0] ?? 0,
        "SYS_HeartbeatOk": bytes[1] ?? 0,
        "SYS_LightState": ((bytes[2] ?? 0) >> 0) & 0x0f,
      };
    }
    case "low:0x012": {
      return {
        "SYS_DcdcEnable": bytes[0] ?? 0,
      };
    }
    case "low:0x110": {
      return {
        "SYS_Mode": bytes[0] ?? 0,
      };
    }
    case "low:0x120": {
      return {
        "SYS_ThrottleSpeed": readI16LE(bytes, 0),
      };
    }
    case "low:0x169": {
      return {
        "SES_AlignEnable": Boolean((bytes[0] ?? 0) & (1 << 0)),
        "SES_CtrlEnable": Boolean((bytes[0] ?? 0) & (1 << 1)),
        "SES_TgtStrAngle": ((readI16LE(bytes, 2) * 0.1) + -3000),
        "SES_TgtStrAngleSpd": readU16LE(bytes, 4),
        "SES_RollCntEnable": Boolean((bytes[5] ?? 0) & (1 << 0)),
        "SES_ChecksumEnable": Boolean((bytes[5] ?? 0) & (1 << 1)),
        "SES_RollCnt": ((bytes[5] ?? 0) >> 4) & 0x0f,
        "SES_VehSpd": bytes[6] ?? 0,
        "SES_Checksum": bytes[7] ?? 0,
      };
    }
    case "low:0x201": {
      return {
        "SES_AngleStatus": Boolean((bytes[0] ?? 0) & (1 << 0)),
        "SES_CtrlModeStatus": ((bytes[0] ?? 0) >> 1) & 0x03,
        "SES_ErrorStatus": ((bytes[0] ?? 0) >> 6) & 0x03,
        "SES_StrAngle": ((readU16LE(bytes, 2) * 0.1) + -3000),
        "SES_TgtStrAngleSpd_FB": (readI16LE(bytes, 4) * 0.5),
        "SES_SteeringTorq": ((bytes[5] ?? 0 * 0.1) + -12.1),
        "SES_RollCntEnStatus": Boolean((bytes[6] ?? 0) & (1 << 0)),
        "SES_ChecksumEnStatus": Boolean((bytes[6] ?? 0) & (1 << 1)),
        "SES_RollCntStatus": ((bytes[6] ?? 0) >> 4) & 0x0f,
        "SES_ChecksumStatus": bytes[7] ?? 0,
      };
    }
    case "low:0x202": {
      return {
        "SES_ECUUnderVolt": Boolean((bytes[0] ?? 0) & (1 << 0)),
        "SES_ECUOverVolt": Boolean((bytes[0] ?? 0) & (1 << 1)),
        "SES_CanComErr": Boolean((bytes[0] ?? 0) & (1 << 2)),
        "SES_ECUTempErr": Boolean((bytes[0] ?? 0) & (1 << 3)),
        "SES_DomainSC": Boolean((bytes[0] ?? 0) & (1 << 4)),
        "SES_DomainV": Boolean((bytes[0] ?? 0) & (1 << 5)),
        "SES_DomainT": Boolean((bytes[0] ?? 0) & (1 << 6)),
        "SES_TempSensor": Boolean((bytes[0] ?? 0) & (1 << 7)),
        "SES_AngleP_OC": Boolean((bytes[1] ?? 0) & (1 << 0)),
        "SES_AngleP_AF": Boolean((bytes[1] ?? 0) & (1 << 1)),
        "SES_AngleS_OC": Boolean((bytes[1] ?? 0) & (1 << 2)),
        "SES_AngleS_AF": Boolean((bytes[1] ?? 0) & (1 << 3)),
        "SES_SensorPow": Boolean((bytes[1] ?? 0) & (1 << 4)),
        "SES_Alignment": Boolean((bytes[1] ?? 0) & (1 << 5)),
        "SES_OverAngle": Boolean((bytes[1] ?? 0) & (1 << 6)),
        "SES_StrMtrStall": Boolean((bytes[1] ?? 0) & (1 << 7)),
        "SES_MtrCurtFault": Boolean((bytes[2] ?? 0) & (1 << 0)),
        "SES_SensorCL": Boolean((bytes[2] ?? 0) & (1 << 1)),
        "SES_TorqT1_OC": Boolean((bytes[2] ?? 0) & (1 << 2)),
        "SES_TorqT1_AF": Boolean((bytes[2] ?? 0) & (1 << 3)),
        "SES_TorqT2_OC": Boolean((bytes[2] ?? 0) & (1 << 4)),
        "SES_TorqT2_AF": Boolean((bytes[2] ?? 0) & (1 << 5)),
        "SES_SentAngle": Boolean((bytes[2] ?? 0) & (1 << 6)),
        "SES_StrMtrIdling": Boolean((bytes[2] ?? 0) & (1 << 7)),
        "SES_EPROM": Boolean((bytes[3] ?? 0) & (1 << 0)),
        "SES_VehSpdSnapshot": bytes[7] ?? 0,
      };
    }
    case "low:0x203": {
      return {
        "SES_SW_Version": (bytes[0] ?? 0 * 0.01),
        "SES_HW_Version": (bytes[1] ?? 0 * 0.1),
      };
    }
    case "low:0x204": {
      return {
        "RT_MotorSpeed": readI32LE(bytes, 0) /* FIXME */,
        "RT_Gear": bytes[4] ?? 0,
      };
    }
    case "low:0x205": {
      return {
        "RT_BrakePressure": readI32LE(bytes, 0) /* FIXME */,
      };
    }
    case "low:0x206": {
      return {
        "MTR_ActualSpeed": readI16LE(bytes, 0),
        "MTR_GearState": bytes[2] ?? 0,
        "MTR_FaultFlags": bytes[3] ?? 0,
      };
    }
    case "low:0x302": {
      return {
        "HOST_LeftTurn": Boolean((bytes[0] ?? 0) & (1 << 0)),
        "HOST_RightTurn": Boolean((bytes[0] ?? 0) & (1 << 1)),
        "HOST_BrakeLight": Boolean((bytes[0] ?? 0) & (1 << 2)),
        "HOST_Headlight": Boolean((bytes[0] ?? 0) & (1 << 3)),
      };
    }
    case "low:0x600": {
      return {
        "SYS_DiagMode": bytes[0] ?? 0,
        "SYS_DiagBrakeEngaged": Boolean((bytes[1] ?? 0) & (1 << 0)),
        "SYS_DiagBrakeFault": Boolean((bytes[1] ?? 0) & (1 << 1)),
        "SYS_DiagHeartbeatOk": bytes[2] ?? 0,
        "SYS_DiagEstopActive": bytes[3] ?? 0,
        "SYS_DiagFreeHeapKb": readU16LE(bytes, 4),
        "SYS_DiagTec": bytes[6] ?? 0,
        "SYS_DiagRec": bytes[7] ?? 0,
      };
    }
    case "low:0x6FA": {
      return {
        "SES_MtrCurt": (readI16LE(bytes, 1) * 0.0078125),
        "SES_ECUTemp": (readU16LE(bytes, 3) * 0.5),
        "SES_PowVolt": (readU16LE(bytes, 5) * 0.00390625),
      };
    }
    case "low:0x6FB": {
      return {
        "SEB_MtrCurr": (readI16LE(bytes, 1) * 0.0078125),
        "SEB_ECUTemp": ((readU16LE(bytes, 3) * 0.5) + -40),
        "SEB_PowVolt": (readU16LE(bytes, 5) * 0.00390625),
      };
    }
    case "low:0x721": {
      return {
        "SEB_AlignStatus": Boolean((bytes[0] ?? 0) & (1 << 0)),
        "SEB_CtrlEnStatus": Boolean((bytes[0] ?? 0) & (1 << 1)),
        "SEB_CtrlModeStatus": ((bytes[0] ?? 0) >> 2) & 0x03,
        "SEB_AutoBrakeStatus": Boolean((bytes[0] ?? 0) & (1 << 4)),
        "SEB_ErrorStatus": ((bytes[0] ?? 0) >> 6) & 0x03,
        "SEB_StrokeValue": ((readU16LE(bytes, 2) * 0.05) + -30),
        "SEB_PressureValue": (bytes[3] ?? 0 * 0.05),
        "SEB_AngleValue": (readI16LE(bytes, 5) * 0.5),
        "SEB_RollCntEnStatus": Boolean((bytes[6] ?? 0) & (1 << 0)),
        "SEB_ChecksumEnStatus": Boolean((bytes[6] ?? 0) & (1 << 1)),
        "SEB_RollCntStatus": ((bytes[6] ?? 0) >> 4) & 0x0f,
        "SEB_ChecksumStatus": bytes[7] ?? 0,
      };
    }
    case "low:0x731": {
      return {
        "SEB_ECUUnderVolt": Boolean((bytes[0] ?? 0) & (1 << 0)),
        "SEB_ECUOverVolt": Boolean((bytes[0] ?? 0) & (1 << 1)),
        "SEB_CanComErr": Boolean((bytes[0] ?? 0) & (1 << 2)),
        "SEB_ECUTempErr": Boolean((bytes[0] ?? 0) & (1 << 3)),
        "SEB_DomainSC": Boolean((bytes[0] ?? 0) & (1 << 4)),
        "SEB_DomainV": Boolean((bytes[0] ?? 0) & (1 << 5)),
        "SEB_DomainT": Boolean((bytes[0] ?? 0) & (1 << 6)),
        "SEB_AngleP_OC": Boolean((bytes[0] ?? 0) & (1 << 7)),
        "SEB_AngleP_AF": Boolean((bytes[1] ?? 0) & (1 << 0)),
        "SEB_AngleS_OC": Boolean((bytes[1] ?? 0) & (1 << 1)),
        "SEB_AngleS_AF": Boolean((bytes[1] ?? 0) & (1 << 2)),
        "SEB_NoPreSensor": Boolean((bytes[1] ?? 0) & (1 << 3)),
        "SEB_SensorUCL": Boolean((bytes[1] ?? 0) & (1 << 5)),
        "SEB_AlignmentErr": Boolean((bytes[1] ?? 0) & (1 << 6)),
        "SEB_AngleOver": Boolean((bytes[1] ?? 0) & (1 << 7)),
        "SEB_MtrStall": Boolean((bytes[2] ?? 0) & (1 << 1)),
        "SEB_MtrDC": Boolean((bytes[2] ?? 0) & (1 << 2)),
        "SEB_OilErr": Boolean((bytes[2] ?? 0) & (1 << 3)),
        "SEB_InitOil": Boolean((bytes[2] ?? 0) & (1 << 4)),
        "SEB_SentValue": Boolean((bytes[2] ?? 0) & (1 << 5)),
        "SEB_MtrNoLoad": Boolean((bytes[2] ?? 0) & (1 << 6)),
        "SEB_PreSensorOver": Boolean((bytes[3] ?? 0) & (1 << 0)),
        "SEB_LowVoltCharging": Boolean((bytes[3] ?? 0) & (1 << 1)),
      };
    }
    case "low:0x741": {
      return {
        "SEB_SW_Version": (bytes[0] ?? 0 * 0.01),
        "SEB_HW_Version": (bytes[1] ?? 0 * 0.1),
      };
    }
    case "low:0x7B9": {
      return {
        "SEB_AlignEnable": Boolean((bytes[0] ?? 0) & (1 << 0)),
        "SEB_CtrlEnable": Boolean((bytes[0] ?? 0) & (1 << 1)),
        "SEB_CtrlMode": Boolean((bytes[0] ?? 0) & (1 << 2)),
        "SEB_AutoBrake": Boolean((bytes[0] ?? 0) & (1 << 3)),
        "SEB_StrokeReq": ((readU16LE(bytes, 2) * 0.05) + -30),
        "SEB_PressureReq": (bytes[3] ?? 0 * 0.05),
        "SEB_RollCntEnable": Boolean((bytes[6] ?? 0) & (1 << 0)),
        "SEB_ChecksumEnable": Boolean((bytes[6] ?? 0) & (1 << 1)),
        "SEB_RollCnt": ((bytes[6] ?? 0) >> 4) & 0x0f,
        "SEB_Checksum": bytes[7] ?? 0,
      };
    }
    case "low:0x7FD": {
      return {
        "RT_AliveCtr": bytes[0] ?? 0,
        "RT_HealthFlags": bytes[1] ?? 0,
      };
    }
    case "low:0x7FE": {
      return {
        "SYS_AliveCtr": bytes[0] ?? 0,
        "SYS_HealthFlags": bytes[1] ?? 0,
      };
    }
    default: return {};
  }
}
