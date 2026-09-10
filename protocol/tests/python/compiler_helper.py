"""Helper to discover and select the best C++ compiler on the system."""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from functools import lru_cache
from pathlib import Path


def _get_compiler_version(compiler_path: str) -> tuple[int, ...]:
    """Extract numeric version tuple from compiler --version output."""
    try:
        proc = subprocess.run(
            [compiler_path, "--version"],
            capture_output=True,
            text=True,
            timeout=3,
            check=False,
        )
        first_line = proc.stdout.splitlines()[0] if proc.stdout else ""
        match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", first_line)
        if match:
            return tuple(int(x) for x in match.groups() if x is not None)
    except Exception:
        pass
    return (0,)


@lru_cache(maxsize=None)
def _probe_standard_support(compiler_path: str, standard: str) -> bool:
    """Verify if the compiler can compile code using the requested standard."""
    test_src = {
        "c++17": "#include <string_view>\nint main() { std::string_view sv; return (int)sv.size(); }\n",
        "c++11": "int main() { auto x = 0; return x; }\n",
    }.get(standard, "int main() { return 0; }\n")

    try:
        proc = subprocess.run(
            [compiler_path, f"-std={standard}", "-fsyntax-only", "-x", "c++", "-"],
            input=test_src,
            text=True,
            capture_output=True,
            check=False,
            timeout=5,
        )
        return proc.returncode == 0
    except Exception:
        return False


@lru_cache(maxsize=None)
def find_cpp_compiler(standard: str = "c++17") -> str | None:
    """Find the best available C++ compiler supporting the specified standard.

    Scans all candidate compilers (clang++, g++) across PATH and CXX environment
    variable, sorts them by highest version first (so modern toolchains like
    Clang 22 or GCC 16 are preferred over legacy versions like GCC 6.3),
    and validates standard compatibility.
    """
    seen: set[str] = set()
    candidates: list[str] = []

    # 1. Inspect CXX environment variable if set
    cxx_env = os.environ.get("CXX")
    if cxx_env:
        resolved = shutil.which(cxx_env) or cxx_env
        p = Path(resolved)
        if p.is_file():
            candidates.append(str(p.resolve()))
            seen.add(str(p.resolve()).lower())

    # 2. Gather all clang++ and g++ binaries found across PATH
    exts = [".exe", ""] if sys.platform == "win32" else [""]
    for name in ["clang++", "g++"]:
        for dir_str in os.environ.get("PATH", "").split(os.pathsep):
            if not dir_str.strip():
                continue
            for ext in exts:
                full_path = Path(dir_str) / f"{name}{ext}"
                if full_path.is_file():
                    try:
                        resolved_str = str(full_path.resolve())
                    except Exception:
                        resolved_str = str(full_path)
                    if resolved_str.lower() not in seen:
                        seen.add(resolved_str.lower())
                        candidates.append(resolved_str)

    # 3. Sort candidates by version descending so the best/latest compiler is selected first
    # If versions are equal, prioritize clang++ over g++ for superior C++17 conformance
    candidates.sort(
        key=lambda p: (_get_compiler_version(p), "clang" in Path(p).name.lower()),
        reverse=True,
    )

    # 4. Probe candidates for standard support
    for candidate in candidates:
        if _probe_standard_support(candidate, standard):
            return candidate

    return None
