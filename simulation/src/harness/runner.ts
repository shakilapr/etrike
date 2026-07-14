/**
 * SimulationRunner — top-level orchestrator.
 *
 * Runs the clock, delivers CAN frames to buses, calls each ECU's tick(),
 * advances the physical plant, runs safety checks, and collects results.
 */

import { SimulationClock } from "../core/clock.js";
import { CanBusModel } from "../bus/can-bus.js";
import type { SimFrame, SimConfig, SimulationResult, BusId, SimNodeId } from "../core/types.js";
import type { SimulatedEcu, SimulationContext } from "../ecus/base.js";
import { VehiclePlant } from "../physics/plant.js";
import { SafetyChecker } from "../checks/safety-checker.js";
import { CanValidator } from "../checks/can-validator.js";
import { FaultInjector } from "./fault-injector.js";
import { HostEcu } from "../ecus/host.js";
import { RtEcu } from "../ecus/rt.js";
import { SysEcu } from "../ecus/sys.js";
import { MtrEcu } from "../ecus/mtr.js";
import { Sbwc } from "../ecus/epsc.js";
import { Bbw } from "../ecus/seb.js";
import { decodeAs, decodeSimFrame } from "../protocol.js";

export class SimulationRunner {
  readonly clock = new SimulationClock(0);
  readonly highBus = new CanBusModel("high");
  readonly lowBus = new CanBusModel("low");
  readonly plant = new VehiclePlant();
  readonly safetyChecker = new SafetyChecker();
  readonly canValidator = new CanValidator();
  readonly faultInjector = new FaultInjector();

  readonly ecus: SimulatedEcu[] = [];
  readonly host: HostEcu;
  readonly rt: RtEcu;
  readonly sys: SysEcu;
  readonly mtr: MtrEcu;
  readonly epsc: Sbwc;
  readonly seb: Bbw;

  private allFrames: SimFrame[] = [];
  private lastCmdSpeedMmps = 0;
  private lastCmdSteerDeg = 0;
  private lastCmdBrakeMm = 0;
  private estopLatched = false;  // ESTOP latches until simulation reset
  capturedFrames: SimFrame[] = [];    // test hook: full frame log
  private config: SimConfig = {
    tickMs: 1, speed: 0, initialMode: "manual",
    plant: { wheelbaseMm: 1500, maxSpeedMmps: 3000, maxSteeringDeg: 40, steerLagMs: 50, brakeDecelMmps2PerMm: 2000 },
    hostDriveCycle: [], faults: [],
  };

  constructor() {
    this.host = new HostEcu();
    this.rt = new RtEcu();
    this.sys = new SysEcu();
    this.mtr = new MtrEcu();
    this.epsc = new Sbwc();
    this.seb = new Bbw();

    this.ecus = [this.host, this.rt, this.sys, this.mtr, this.epsc, this.seb];
  }

  /** Configure simulation parameters. */
  configure(config: Partial<SimConfig>): void {
    this.config = {
      tickMs: 1, speed: 0, initialMode: "manual",
      plant: { wheelbaseMm: 1500, maxSpeedMmps: 3000, maxSteeringDeg: 40, steerLagMs: 50, brakeDecelMmps2PerMm: 2000 },
      hostDriveCycle: [], faults: [],
      ...config,
    };
    this.clock.speed = this.config.speed;
    this.host.setDriveCycle(this.config.hostDriveCycle);
    this.faultInjector.load(this.config.faults);
  }

  /** Initialize all ECUs and reset state. */
  init(): void {
    this.clock.reset();
    this.highBus.reset(0);
    this.lowBus.reset(0);
    this.plant.reset();
    this.safetyChecker.reset();
    this.canValidator.reset();
    this.allFrames = [];
    this.lastCmdSpeedMmps = 0;
    this.lastCmdSteerDeg = 0;
    this.lastCmdBrakeMm = 0;
    this.estopLatched = false;
    this.capturedFrames = [];
    for (const ecu of this.ecus) {
      ecu.init();
    }
  }

  /** Run the simulation for `durationMs` ticks. */
  runDuration(durationMs: number): SimulationResult {
    this.init();

    for (let t = 0; t < durationMs; t += this.config.tickMs) {
      this.tick();
    }

    return this.getResult(durationMs);
  }

  /** Run a single tick. */
  tick(): void {
    const nowMs = this.clock.nowMs;
    const ctx: SimulationContext = {
      nowMs,
      ticks: this.clock.ticks,
      mode: this.config.initialMode,
      estopActive: this.estopLatched,  // latched: once triggered, stays active
      brakeLeverPressed: false,
    };

    // ── Process fault injection ────────────────────────────────
    const mutation = this.faultInjector.tick(nowMs, ctx);
    if (mutation.estopActive) {
      ctx.estopActive = true;
      this.estopLatched = true;  // latch ESTOP permanently
    }
    if (mutation.estopGpio !== undefined) this.sys.setEstopButton(mutation.estopGpio);
    if (mutation.brakeLever !== undefined) this.sys.setBrakeLever(mutation.brakeLever);

    // ── Deliver CAN frames ─────────────────────────────────────
    const highRx = this.highBus.deliver(nowMs);
    const lowRx = this.lowBus.deliver(nowMs);

    // ── Run ECUs in fixed order ────────────────────────────────
    // Order matters: Host sends commands → RT processes → SYS+MTR act
    const allTx: SimFrame[] = [];

    for (const ecu of this.ecus) {
      const tx = ecu.tick(nowMs, highRx, lowRx, ctx);
      for (let f of tx) {
        // Apply fault injection
        if (this.faultInjector.shouldDrop(f.canId, f.bus)) continue;
        // Gap #12: freezeHeartbeat — stop heartbeat frames from frozen nodes
        const hbCanIds = ["0x7FC", "0x7FD", "0x7FE"];
        if (hbCanIds.includes(f.canId) && this.faultInjector.isHeartbeatFrozen(f.sender as SimNodeId)) continue;

        let data = f.data;
        data = this.faultInjector.corrupt(f.canId, f.bus, data);

        const frame: SimFrame = { ...f, data, simTimeMs: nowMs };
        const decoded = decodeSimFrame(frame);
        if (decoded?.status === "ok") frame.decoded = decoded.values;
        allTx.push(frame);

        // Validate
        this.canValidator.validate(nowMs, frame.canId, frame.bus, frame.dlc, frame.data.length, frame.sender);
      }
    }

    // ── Schedule transmitted frames on buses (deliver next tick) ─
    for (const f of allTx) {
      if (f.bus === "high") this.highBus.schedule(f, nowMs + 1);
      else this.lowBus.schedule(f, nowMs + 1);
    }

    this.allFrames.push(...allTx);
    this.capturedFrames.push(...allTx);  // test hook

    // ── Advance physical plant ─────────────────────────────────
    // Feed actual state back to ECUs
    this.mtr.setActualSpeed(this.plant.speedMmps);
    this.sys.setActualSpeed(this.plant.speedMmps);
    this.epsc.setActualAngle(this.plant.steerAngleDeg);
    this.seb.setActualStroke(this.plant.brakeStrokeMm);

    // Extract commands from this tick's transmitted CAN frames.
    // Persist the last commanded values so the plant keeps accelerating
    // between periodic CAN frames (which only fire every 10-50ms).
    let cmdSpeedMmps = this.lastCmdSpeedMmps;
    let cmdSteerDeg = this.lastCmdSteerDeg;
    let cmdBrakeMm = this.lastCmdBrakeMm;

    for (const f of allTx) {
      const drive = decodeAs(f, "rt:rt_drive_cmd");
      if (drive !== undefined) {
        cmdSpeedMmps = Number(drive.motor_speed_mmps);
      }
      const steering = decodeAs(f, "ses:vcu_ses_req");
      if (steering !== undefined) {
        cmdSteerDeg = (Number(steering.target_angle_raw) - 30000) / 10;
      }
      const brake = decodeAs(f, "seb:vcu_seb_req");
      if (brake !== undefined) {
        if (Number(brake.control_mode) === 1) {
          const pressureRaw = Number(brake.pressure_request_raw);
          cmdBrakeMm = (pressureRaw / 100) * 27;  // proportional to 27mm max stroke
        } else {
          const strokeRaw = Number(brake.stroke_request_raw);
          cmdBrakeMm = Math.max(0, strokeRaw * 0.05 - 30);
        }
      }
    }

    this.lastCmdSpeedMmps = cmdSpeedMmps;
    this.lastCmdSteerDeg = cmdSteerDeg;
    this.lastCmdBrakeMm = cmdBrakeMm;

    this.plant.setCommands(cmdSpeedMmps, cmdSteerDeg, cmdBrakeMm);
    this.plant.tick(this.config.tickMs);

    // ── Safety checks ──────────────────────────────────────────
    if (ctx.estopActive) {
      this.safetyChecker.triggerEstop(nowMs);
      this.safetyChecker.checkEstopResponse(nowMs, ctx, true, true);
    }

    this.clock.tick();
  }

  getResult(durationMs: number): SimulationResult {
    const uptimeS = Math.max(durationMs / 1000, 0.001);
    const state = this.plant.getState();

    return {
      durationMs,
      totalFrames: this.allFrames.length,
      violations: this.safetyChecker.getAllViolations(),
      validationErrors: this.canValidator.getAllErrors(),
      highBus: this.highBus.getStats(durationMs, uptimeS),
      lowBus: this.lowBus.getStats(durationMs, uptimeS),
      plantFinalSpeedMmps: state.speedMmps,
      plantFinalSteerDeg: state.steerAngleDeg,
      plantMaxSteerDeg: this.plant.maxSteerAngleDeg,
      plantFinalBrakeStrokeMm: state.brakeStrokeMm,
    };
  }
}
