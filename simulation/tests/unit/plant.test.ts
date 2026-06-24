import { describe, it, expect, beforeEach } from "vitest";
import { VehiclePlant } from "../../src/physics/plant.js";

describe("VehiclePlant", () => {
  let plant: VehiclePlant;

  beforeEach(() => {
    plant = new VehiclePlant();
  });

  it("starts at rest", () => {
    const s = plant.getState();
    expect(s.speedMmps).toBe(0);
    expect(s.steerAngleDeg).toBe(0);
    expect(s.brakeStrokeMm).toBe(0);
  });

  it("accelerates toward commanded speed", () => {
    plant.setCommands(2000, 0, 0);

    // Run 100ms
    for (let i = 0; i < 100; i++) plant.tick(1);

    const s = plant.getState();
    // After 100ms at 3000mm/s², speed should be ~300 mm/s
    expect(s.speedMmps).toBeGreaterThan(100);
    expect(s.speedMmps).toBeLessThan(2000);
  });

  it("reaches commanded speed after enough time", () => {
    plant.setCommands(1000, 0, 0);

    // Run 2000ms (2 seconds) — plenty to reach 1000 mm/s
    for (let i = 0; i < 2000; i++) plant.tick(1);

    expect(plant.speedMmps).toBeCloseTo(1000, -1); // within ~10 mm/s
  });

  it("steering responds with first-order lag", () => {
    plant.setCommands(0, 20, 0);

    // After 50ms (one time constant), should be ~63% of target
    for (let i = 0; i < 50; i++) plant.tick(1);
    expect(plant.steerAngleDeg).toBeGreaterThan(10);
    expect(plant.steerAngleDeg).toBeLessThan(20);
  });

  it("brake decelerates the vehicle", () => {
    // First get moving
    plant.setCommands(2000, 0, 0);
    for (let i = 0; i < 1000; i++) plant.tick(1);

    const speedBeforeBrake = plant.speedMmps;
    expect(speedBeforeBrake).toBeGreaterThan(500);

    // Now brake hard
    plant.setCommands(0, 0, 15); // 15mm brake stroke
    for (let i = 0; i < 200; i++) plant.tick(1);

    expect(plant.speedMmps).toBeLessThan(speedBeforeBrake);
  });

  it("speed is clamped to forward max", () => {
    plant.setCommands(5000, 0, 0); // way over max

    for (let i = 0; i < 5000; i++) plant.tick(1);

    expect(plant.speedMmps).toBeLessThanOrEqual(3000);
  });

  it("reset returns to zero state", () => {
    plant.setCommands(2000, 10, 5);
    for (let i = 0; i < 100; i++) plant.tick(1);

    plant.reset();
    const s = plant.getState();
    expect(s.speedMmps).toBe(0);
    expect(s.steerAngleDeg).toBe(0);
    expect(s.brakeStrokeMm).toBe(0);
  });
});
