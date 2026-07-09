import { SimulationRunner } from "./src/harness/runner.js";

async function main() {
  const runner = new SimulationRunner();
  runner.configure({
    tickMs: 1,
    speed: 0,
    initialMode: "auto",
    plant: {
      wheelbaseMm: 1500,
      maxSpeedMmps: 9000, // allow > 30km/h (8333 mm/s)
      maxSteeringDeg: 40,
      steerLagMs: 50,
      brakeDecelMmps2PerMm: 2000 // default
    },
    hostDriveCycle: [
      // 30 km/h = 8333 mm/s
      // 20 deg/min = 20 * (PI / 180) rad / 60 sec = 0.0058 rad/s = 5.8 mrad/s
      { durationMs: 2000, speedMmps: 8333, yawRateMradS: 6, gear: 1 }
    ],
    faults: [
      { atMs: 2000, type: "triggerEstop" } // Sudden brake command (ESTOP)
    ]
  });

  // Run until 2000ms (steady state)
  const result1 = runner.runDuration(2000);
  
  const cmdFrames = runner.capturedFrames.filter(f => f.canId === "0x300");
  const driveFrames = runner.capturedFrames.filter(f => f.canId === "0x204");
  console.log("0x300 count:", cmdFrames.length, "last:", cmdFrames.length > 0 ? cmdFrames[cmdFrames.length - 1].data : []);
  console.log("0x204 count:", driveFrames.length, "last:", driveFrames.length > 0 ? driveFrames[driveFrames.length - 1].data : []);

  console.log("Speed at t=2000ms (just before brake):", result1.plantFinalSpeedMmps, "mm/s");

  // Run the next ms chunks to see how long it takes to stop
  let timeToStopMs = 0;
  let finalSpeed = result1.plantFinalSpeedMmps;
  
  while (finalSpeed > 1 && timeToStopMs < 10000) {
    runner.tick();
    finalSpeed = runner.getResult(0).plantFinalSpeedMmps;
    timeToStopMs++;
  }

  console.log("Speed at stop:", finalSpeed, "mm/s");
  console.log("Time to stop:", timeToStopMs, "ms");
}

main();
