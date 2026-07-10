import { ID_RT_DRIVE_CMD, ID_HOST_DRIVE_CMD, ID_RT_BRAKE_CMD, ID_HOST_BRAKE_REQ, ID_SEB_STATUS, ID_VCU_SEB_REQ } from "@etrike/debug-shared";
import { describe, expect, it } from "vitest";
import { correlatePipeline } from "./can";
import type { CanFrame } from "../types/can";

function frame(ts: number, bus: "high" | "low", id: string, decoded: Record<string, unknown> = {}): CanFrame {
  return {
    ts,
    bus,
    id,
    name: id,
    dlc: 8,
    data: [],
    decoded
  };
}

describe("correlatePipeline", () => {
  it("matches the first valid drive chain frame after the trigger", () => {
    const chains = correlatePipeline([
      frame(10.030, "low", ID_RT_DRIVE_CMD, { motor_speed_mmps: 2000 }),
      frame(10.020, "low", ID_RT_DRIVE_CMD, { motor_speed_mmps: 2000 }),
      frame(10.000, "high", ID_HOST_DRIVE_CMD, { speed_mmps: 2000 })
    ]);

    expect(chains).toHaveLength(1);
    expect(chains[0].steps[0].ts).toBe(10.020);
  });

  it("rejects candidate frames outside the correlation window", () => {
    const chains = correlatePipeline([
      frame(10.250, "low", ID_RT_DRIVE_CMD, { motor_speed_mmps: 2000 }),
      frame(10.000, "high", ID_HOST_DRIVE_CMD, { speed_mmps: 2000 })
    ]);

    expect(chains).toHaveLength(0);
  });

  it("applies brake pressure tolerance before building a brake chain", () => {
    const chains = correlatePipeline([
      frame(20.020, "low", ID_RT_BRAKE_CMD, { brake_pressure_kpa: 7000 }),
      frame(20.000, "high", ID_HOST_BRAKE_REQ, { brake_pressure_kpa: 5000 })
    ]);

    expect(chains).toHaveLength(0);
  });

  it("sorts newest-first DB frames before binary searching", () => {
    const chains = correlatePipeline([
      frame(30.050, "low", ID_SEB_STATUS),
      frame(30.040, "low", ID_VCU_SEB_REQ),
      frame(30.020, "low", ID_RT_BRAKE_CMD, { brake_pressure_kpa: 5000 }),
      frame(30.000, "high", ID_HOST_BRAKE_REQ, { brake_pressure_kpa: 5000 })
    ].reverse());

    expect(chains).toHaveLength(1);
    expect(chains[0].steps.map((step) => step.id)).toEqual([ID_RT_BRAKE_CMD, ID_VCU_SEB_REQ, ID_SEB_STATUS]);
  });
});
