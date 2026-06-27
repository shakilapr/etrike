"""PlatformIO pre-build script — regenerates can_data.h from YAML before each build."""
Import("env")
import subprocess, sys
from pathlib import Path

script = Path(__file__).resolve().parent / "generate_code.py"
print(f"[can] Regenerating CAN data from YAML...")
result = subprocess.run([sys.executable, str(script)], capture_output=True, text=True)
if result.returncode != 0:
    print(f"[can] ERROR: {result.stderr}")
    env.Exit(1)
print(f"[can] {result.stdout.strip()}")
