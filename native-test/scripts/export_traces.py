import subprocess
import json
import os
import sys

def build_sim_engine():
    print("Building sim_engine_native...")
    if not os.path.exists("build"):
        os.makedirs("build")
    
    subprocess.run(["cmake", ".."], cwd="build", check=True)
    subprocess.run(["cmake", "--build", ".", "--target", "sim_engine_native"], cwd="build", check=True)

def run_trace(scenario_name, tick_ms, duration_ms, drive_cmds):
    print(f"Running C++ scenario: {scenario_name}")
    exe_path = os.path.join("build", "Debug", "sim_engine_native.exe")
    if not os.path.exists(exe_path):
        exe_path = os.path.join("build", "sim_engine_native.exe")
    
    process = subprocess.Popen([exe_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    
    out_frames = []
    
    def read_responses():
        while True:
            # We don't want to block forever if no output, but readline blocks
            # Actually, main_native outputs state/frame immediately after tick
            # But we might need to send all input, then close stdin, and read all output.
            pass

    # Better approach: generate all input JSON lines, send them all, close stdin, read all stdout
    input_lines = []
    for t in range(0, duration_ms, tick_ms):
        # find active drive command
        cmd = None
        for c in drive_cmds:
            if c["start_ms"] <= t < c["end_ms"]:
                cmd = c
                break
        
        speed = cmd["speed"] if cmd else 0
        yaw = cmd["yaw"] if cmd else 0
        
        input_lines.append(json.dumps({"type":"tick", "dt_ms": tick_ms, "speed_mmps": speed, "yaw_mrad_s": yaw}, separators=(',', ':')))

    input_data = "\n".join(input_lines) + "\n"
    stdout_data, _ = process.communicate(input=input_data)
    
    print(f"DEBUG STDOUT: {stdout_data[:200]}")
    
    # Let's do a state-tracked parsing
    current_time_ms = 0
    traces = []
    for line in stdout_data.split('\n'):
        if not line: continue
        try:
            msg = json.loads(line)
            if msg.get("type") == "state" and "uptime_ms" in msg:
                current_time_ms = msg["uptime_ms"]
            elif msg.get("type") == "frame":
                # Convert data array to hex space string
                data_hex = " ".join([f"{b:02X}" for b in msg["data"]])
                traces.append({
                    "time_ms": current_time_ms,
                    "bus": msg["bus"],
                    "id": msg["id"],
                    "dlc": msg["dlc"],
                    "data": data_hex,
                    # We don't have python decoded values here, the replay tester will do it
                })
        except Exception:
            pass

    os.makedirs("traces", exist_ok=True)
    out_path = f"traces/{scenario_name}.jsonl"
    with open(out_path, "w") as f:
        for t in traces:
            f.write(json.dumps(t) + "\n")
            
    print(f"Exported {len(traces)} frames to {out_path}")

if __name__ == "__main__":
    build_sim_engine()
    
    # Drive forward scenario (2 seconds)
    # 0-500ms: 0 speed
    # 500-2000ms: 2000 speed
    drive_cmds = [
        {"start_ms": 0, "end_ms": 500, "speed": 0, "yaw": 0},
        {"start_ms": 500, "end_ms": 2000, "speed": 2000, "yaw": 0}
    ]
    run_trace("native_drive_forward", 10, 2000, drive_cmds)
