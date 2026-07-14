"""Verify canonical protocol artifacts before a PlatformIO build."""

Import("env")

import subprocess
import sys
from pathlib import Path

project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
repository = project_dir.parent
result = subprocess.run(
    [sys.executable, "-m", "protocol.tools.protocol", "generate", "--check"],
    cwd=repository,
)
if result.returncode:
    env.Exit(result.returncode)
