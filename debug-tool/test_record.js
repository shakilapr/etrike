const fs = require('fs');

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function startRecording() {
  const res = await fetch('http://localhost:3000/api/recordings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ label: 'Automated Test Recording' })
  });
  return await res.json();
}

async function stopRecording(id) {
  const res = await fetch(`http://localhost:3000/api/recordings/${id}/stop`, { method: 'PUT' });
  return await res.json();
}

async function getFrames(id) {
  const res = await fetch(`http://localhost:3000/api/recordings/${id}/frames`);
  return await res.json();
}

async function sendPeriodic(action, id, bus, data, dlc, interval_ms) {
  const payload = { action, bus, id, data, dlc, interval_ms };
  const res = await fetch('http://localhost:3000/api/cmd/periodic', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  return await res.json();
}

async function runTest() {
  console.log("Starting test...");
  
  const recRes = await startRecording();
  const recId = recRes.recording.id;
  console.log(`Started recording ID: ${recId}`);

  console.log("Starting continuous commands...");
  // Host Heartbeat 0x7FC on high bus (every 50ms)
  await sendPeriodic('start', '0x7FC', 'high', [1, 0], 2, 50);
  
  // Host Drive 0x300 on high bus (every 10ms to spam the bus)
  await sendPeriodic('start', '0x300', 'high', [0, 0, 0, 0, 0, 0, 0, 0], 8, 10);
  
  // MTR feedback 0x206 on low bus (every 10ms)
  await sendPeriodic('start', '0x206', 'low', [0, 0, 0, 0], 4, 10);
  
  console.log("Waiting 10 seconds to collect data...");
  await sleep(10000);
  
  console.log("Stopping commands...");
  await sendPeriodic('stop', '0x7FC', 'high');
  await sendPeriodic('stop', '0x300', 'high');
  await sendPeriodic('stop', '0x206', 'low');
  
  console.log("Stopping recording...");
  await stopRecording(recId);
  
  console.log("Fetching recorded frames...");
  const framesRes = await getFrames(recId);
  const frames = framesRes.frames || [];
  console.log(`Recorded ${frames.length} frames.`);
  
  // Analyze frames
  const byId = {};
  for (const f of frames) {
    if (!byId[f.can_id]) byId[f.can_id] = [];
    byId[f.can_id].push(f);
  }
  
  for (const [id, f_arr] of Object.entries(byId)) {
    console.log(`\nAnalysis for CAN ID ${id} (${f_arr.length} frames):`);
    let maxDelta = 0;
    let minDelta = Infinity;
    let sumDelta = 0;
    for (let i = 1; i < f_arr.length; i++) {
        // frames are ordered by some timestamp. wait, let's sort them first
    }
    
    // sorting by ts_real
    f_arr.sort((a, b) => a.ts_real - b.ts_real);
    
    let dropCount = 0;
    for (let i = 1; i < f_arr.length; i++) {
      const delta = f_arr[i].ts_real - f_arr[i-1].ts_real;
      if (delta > maxDelta) maxDelta = delta;
      if (delta < minDelta) minDelta = delta;
      sumDelta += delta;
      
      // if delta is significantly larger than expected interval (e.g. > 3x expected)
      if (id === '0x300' && delta > 30) dropCount++;
      if (id === '0x206' && delta > 30) dropCount++;
      if (id === '0x7FC' && delta > 150) dropCount++;
    }
    const avgDelta = f_arr.length > 1 ? (sumDelta / (f_arr.length - 1)) : 0;
    console.log(`  Avg Delta: ${avgDelta.toFixed(2)} ms`);
    console.log(`  Min Delta: ${minDelta} ms`);
    console.log(`  Max Delta: ${maxDelta} ms`);
    console.log(`  Suspected drops / gaps: ${dropCount}`);
  }
}

runTest().catch(console.error);
