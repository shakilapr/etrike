"""PlatformIO pre-build script — verifies committed CAN artifacts before build."""
Import("env")
import subprocess, sys
from pathlib import Path

try:
    base_dir = Path(__file__).resolve().parent
except NameError:
    project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
    base_dir = project_dir.parent / "shared" / "can"

script = base_dir / "generate_code.py"
print(f"[can] Verifying generated CAN data against YAML...")
result = subprocess.run([sys.executable, str(script), "--verify"], capture_output=True, text=True)
if result.returncode != 0:
    print(f"[can] ERROR: {result.stderr}")
    env.Exit(1)
print(f"[can] {result.stdout.strip()}")
