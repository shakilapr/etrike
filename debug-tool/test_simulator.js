const { spawn } = require('child_process');

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function startRecording() {
  const res = await fetch('http://localhost:3000/api/recordings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ label: 'Automated Sim Recording' })
  });
  return await res.json();
}

async function stopRecording(id) {
  const res = await fetch(`http://localhost:3000/api/recordings/${id}/stop`, { method: 'PUT' });
  return await res.json();
}

async function getFrames(id) {
  const res = await fetch(`http://localhost:3000/api/recordings/${id}/frames?limit=5000`);
  return await res.json();
}

async function runTest() {


  console.log("Starting simulator...");
  const sim = spawn('npx.cmd', ['tsx', 'src/index.ts'], { cwd: 'c:\\projects\\etrike\\debug-tool\\simulator', shell: true });
  sim.stdout.on('data', d => console.log(`[SIM] ${d}`));
  sim.stderr.on('data', d => console.log(`[SIM ERR] ${d}`));

  await sleep(2000); // Wait for simulator to connect and start spamming
  
  console.log("Starting recording...");
  const recRes = await startRecording();
  const recId = recRes.recording.id;
  console.log(`Started recording ID: ${recId}`);

  console.log("Waiting 5 seconds to collect data under load...");
  await sleep(5000);
  
  console.log("Stopping recording...");
  await stopRecording(recId);
  
  console.log("Stopping simulator...");
  sim.kill();
  
  console.log("Fetching recorded frames...");
  const framesRes = await getFrames(recId);
  const frames = framesRes.frames || [];
  console.log(`Recorded ${frames.length} frames.`);
  
  // Analyze frames
  const byId = {};
  for (const f of frames) {
    if (!byId[f.id]) byId[f.id] = [];
    byId[f.id].push(f);
  }
  
  for (const [id, f_arr] of Object.entries(byId)) {
    let maxDelta = 0;
    let minDelta = Infinity;
    let sumDelta = 0;
    
    // Sort by ts_real
    f_arr.sort((a, b) => a.ts_real - b.ts_real);
    
    let dropCount = 0;
    for (let i = 1; i < f_arr.length; i++) {
      const delta = (f_arr[i].ts_real - f_arr[i-1].ts_real) * 1000;
      if (delta > maxDelta) maxDelta = delta;
      if (delta < minDelta) minDelta = delta;
      sumDelta += delta;
      
      // Typical intervals for sim are 10ms (100Hz) or 50ms (20Hz)
      // 100ms is a large gap for something that should be at least 20Hz.
      if (delta > 100) dropCount++; 
    }
    const avgDelta = f_arr.length > 1 ? (sumDelta / (f_arr.length - 1)) : 0;
    console.log(`\nCAN ID ${id} (${f_arr.length} frames):`);
    console.log(`  Avg Delta: ${avgDelta.toFixed(2)} ms`);
    console.log(`  Min Delta: ${minDelta} ms`);
    console.log(`  Max Delta: ${maxDelta} ms`);
    console.log(`  Gaps > 50ms: ${dropCount}`);
  }
}

runTest().catch(console.error);
