import { VehiclePlant } from "./src/physics/plant.js";

function testBrake(mode: "stroke" | "pressure") {
  const plant = new VehiclePlant({
    updateHz: 1000,
    brakeDecelMmps2PerMm: 2000,
  });

  // Start at exactly 3000 mm/s (10.8 km/h max speed limit)
  plant["speedMmps"] = 3000;
  
  let cmdBrakeMm = 0;
  
  if (mode === "stroke") {
    // Stroke mode: ESTOP commands max 27mm physical stroke
    cmdBrakeMm = 27;
  } else {
    // Pressure mode: Host requests a comfortable 1000 kPa (out of 20000 kPa max)
    // 1000 kPa / 50 = 20 raw. 20/100 * 27mm = 5.4mm equivalent actuator target.
    cmdBrakeMm = 5.4;
  }

  // Tell plant to hold speed commands zero, apply brake
  plant.setCommands(0, 0, cmdBrakeMm);

  let timeToStopMs = 0;
  let distMm = 0;
  let speed = plant.getState().speedMmps;

  while (speed > 1 && timeToStopMs < 10000) {
    distMm += speed * 0.001;
    plant.tick(1);
    speed = plant.getState().speedMmps;
    timeToStopMs++;
  }

  console.log(`\n--- Brake Mode: ${mode.toUpperCase()} ---`);
  console.log(`Initial Speed: 3000 mm/s (10.8 km/h)`);
  if (mode === "stroke") {
    console.log(`Actuator Command: 27 mm physical stroke (ESTOP)`);
  } else {
    console.log(`Actuator Command: 1000 kPa hydraulic pressure`);
  }
  
  console.log(`Time to stop: ${timeToStopMs} ms`);
  console.log(`Stopping distance: ${(distMm / 10).toFixed(2)} cm (${(distMm / 1000).toFixed(2)} m)`);
}

testBrake("stroke");
testBrake("pressure");

