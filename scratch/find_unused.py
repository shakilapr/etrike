import os
import re

constants = [
    "kWheelbaseMM",
    "kObstacleStopMM",
    "kObstacleClearMM",
    "kMaxSpeedFwdMmps",
    "kMaxSpeedRevMmps",
    "kLowSpeedThreshMmps",
    "kHostCmdStaleTimeoutMs",
    "kHeartbeatTimeoutMsHost",
    "kStartupGracePeriodMs",
    "kEstopBroadcastMinIntervalUs",
    "should_send_estop_now",
    "kBrakeStrokeScale",
    "kBrakeStrokeOffset",
    "kSebMaxPressureRaw",
    "kMaxBrakeKpa",
    "kObstacleMaxKpa",
    "kAssistStopKpa",
    "kMtrFaultEstopActive",
    "kMtrFaultCmdTimeout",
    "kMtrFaultAdcFault",
    "kMtrFaultGearConflict",
    "kMtrFaultStartupReady"
]

files_to_check = []
for root, dirs, files in os.walk("c:/projects/etrike"):
    # Skip standard excludes
    if ".git" in root or "node_modules" in root or "build" in root or ".pio" in root or "artifacts" in root:
        continue
    for file in files:
        if file.endswith((".c", ".cpp", ".h", ".hpp")):
            if file == "shared_config.h":
                continue
            files_to_check.append(os.path.join(root, file))

results = {c: [] for c in constants}

for filepath in files_to_check:
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            content = f.read()
            for c in constants:
                if re.search(r'\b' + c + r'\b', content):
                    results[c].append(filepath)
    except Exception as e:
        pass

print("Unused constants:")
for c, files in results.items():
    if not files:
        print(f"- {c}")

print("\nUsed constants:")
for c, files in results.items():
    if files:
        print(f"- {c} used in {len(files)} files")
