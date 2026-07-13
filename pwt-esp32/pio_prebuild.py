Import("env")

import subprocess
import sys
from pathlib import Path

project_dir = Path(env.subst("$PROJECT_DIR"))
result = subprocess.run([sys.executable, str(project_dir / "generate_can.py"), "--verify"])
if result.returncode:
    env.Exit(result.returncode)

