export const ESTOP_REASONS: Record<number, string> = {
  0: "None", 1: "Button", 2: "Heartbeat timeout", 3: "Following error",
  4: "Obstacle detected", 5: "CAN frame (0x001)", 6: "CAN bus-off", 7: "Internal fault",
};

export const SES_FAULTS: [number, string][] = [
  [0x01,"UnderVolt"],[0x02,"OverVolt"],[0x04,"CanComErr"],[0x08,"TempErr"],
  [0x10,"DomainSC"],[0x20,"DomainV"],[0x40,"DomainT"],[0x80,"TempSensor"],
  [0x100,"AngleP_OC"],[0x200,"AngleP_AF"],[0x400,"AngleS_OC"],[0x800,"AngleS_AF"],
  [0x1000,"SensorPow"],[0x2000,"Alignment"],[0x4000,"OverAngle"],[0x8000,"MtrStall"],
  [0x10000,"MtrCurt"],[0x20000,"SensorCL"],[0x40000,"TorqT1_OC"],[0x80000,"TorqT1_AF"],
  [0x100000,"TorqT2_OC"],[0x200000,"TorqT2_AF"],[0x400000,"SentAngle"],[0x800000,"MtrIdling"],
  [0x1000000,"EPROM"],
];

export const SEB_FAULTS: [number, string][] = [
  [0x01,"UnderVolt"],[0x02,"OverVolt"],[0x04,"CanComErr"],[0x08,"TempErr"],
  [0x10,"DomainSC"],[0x20,"DomainV"],[0x40,"DomainT"],[0x80,"AngleP_OC"],
  [0x100,"AngleP_AF"],[0x200,"AngleS_OC"],[0x400,"AngleS_AF"],[0x800,"NoPreSensor"],
  [0x2000,"SensorUCL"],[0x4000,"Alignment"],[0x8000,"AngleOver"],
  [0x20000,"MtrStall"],[0x40000,"MtrDC"],[0x80000,"OilErr"],[0x100000,"InitOil"],
  [0x200000,"SentValue"],[0x400000,"MtrNoLoad"],
  [0x1000000,"PreSensorOver"],[0x2000000,"LowVoltCharging"],
];

export function decodeFaults(mask: number, table: [number, string][]): string[] {
  const active: string[] = [];
  for (const [bit, name] of table) { if (mask & bit) active.push(name); }
  return active;
}
