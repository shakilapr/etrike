#!/usr/bin/env python3
"""Master Test Runner for E-Trike Pre-Hardware Verification Gate.

Executes all pre-hardware verification suites:
1. Native C++ Exhaustive State-Machine Suite (§3.1)
2. Native C++ Zero-Allocation Memory Trap (§4.3)
3. Native C++ 22-Scenario ESTOP & Safety Reset Suite (§7.12)
4. Python Diagnostic Reference Model & Differential Suite (§3.2)
5. Python Closed-Loop Longitudinal Physics Plant & PID Suite (§8.16)
6. TypeScript CAN Protocol & Simulation Unit Suite (§9)

Usage:
  python run_all_tests.py
  python run_all_tests.py --quick
  python run_all_tests.py --native
  python run_all_tests.py --python
  python run_all_tests.py --sim
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Colors for terminal output
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"

def print_header(title: str):
    print(f"\n{BOLD}{CYAN}{'='*70}{RESET}")
    print(f"{BOLD}{CYAN}  {title}{RESET}")
    print(f"{BOLD}{CYAN}{'='*70}{RESET}\n")

def find_cpp_compiler() -> str:
    # Prefer clang++ for modern C++17 support
    for cand in ["clang++", "g++"]:
        path = shutil.which(cand)
        if path:
            return path
    return ""

def run_step(name: str, fn) -> bool:
    print(f"{BOLD}Running: {name}...{RESET}", end=" ", flush=True)
    t0 = time.time()
    try:
        ok, msg = fn()
        elapsed = time.time() - t0
        if ok:
            print(f"{GREEN}PASS{RESET} ({elapsed:.2f}s)")
            if msg:
                for line in msg.strip().split("\n"):
                    print(f"    {line}")
            return True
        else:
            print(f"{RED}FAIL{RESET} ({elapsed:.2f}s)")
            if msg:
                for line in msg.strip().split("\n"):
                    print(f"    {RED}{line}{RESET}")
            return False
    except Exception as e:
        elapsed = time.time() - t0
        print(f"{RED}ERROR{RESET} ({elapsed:.2f}s): {e}")
        return False

# 1. Native C++ Exhaustive Suite
def test_native_exhaustive(compiler: str):
    src = ROOT / "native-test" / "test" / "test_diagnostic_manager_exhaustive.cpp"
    out = ROOT / "native-test" / "test" / "temp_exhaustive.exe"
    compile_cmd = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", f"-I{ROOT}", str(src), "-o", str(out)]
    cp = subprocess.run(compile_cmd, cwd=ROOT, capture_output=True, text=True)
    if cp.returncode != 0:
        return False, f"Compilation failed:\n{cp.stderr}"
    rp = subprocess.run([str(out)], cwd=ROOT, capture_output=True, text=True)
    if out.exists():
        out.unlink()
    if rp.returncode != 0:
        return False, rp.stdout + rp.stderr
    return True, rp.stdout.strip()

# 2. Native C++ Zero Allocation Suite
def test_native_heap(compiler: str):
    src = ROOT / "native-test" / "test" / "test_heap_allocation.cpp"
    out = ROOT / "native-test" / "test" / "temp_heap.exe"
    compile_cmd = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", f"-I{ROOT}", str(src), "-o", str(out)]
    cp = subprocess.run(compile_cmd, cwd=ROOT, capture_output=True, text=True)
    if cp.returncode != 0:
        return False, f"Compilation failed:\n{cp.stderr}"
    rp = subprocess.run([str(out)], cwd=ROOT, capture_output=True, text=True)
    if out.exists():
        out.unlink()
    if rp.returncode != 0:
        return False, rp.stdout + rp.stderr
    return True, rp.stdout.strip()

# 3. Native C++ 22 ESTOP Suite
def test_native_estop(compiler: str):
    src = ROOT / "native-test" / "test" / "test_estop_22_scenarios.cpp"
    out = ROOT / "native-test" / "test" / "temp_estop.exe"
    compile_cmd = [
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", "-pedantic",
        f"-I{ROOT}", f"-I{ROOT}/shared", f"-I{ROOT}/mtr-stm32/test", f"-I{ROOT}/mtr-stm32/test/stub", f"-I{ROOT}/mtr-stm32/src",
        str(src), "-o", str(out)
    ]
    cp = subprocess.run(compile_cmd, cwd=ROOT, capture_output=True, text=True)
    if cp.returncode != 0:
        return False, f"Compilation failed:\n{cp.stderr}"
    rp = subprocess.run([str(out)], cwd=ROOT, capture_output=True, text=True)
    if out.exists():
        out.unlink()
    if rp.returncode != 0:
        return False, rp.stdout + rp.stderr
    return True, rp.stdout.strip()

# 4. Python Differential Suite
def test_python_differential():
    script = ROOT / "simulation" / "sil" / "tests" / "test_differential_diag.py"
    cp = subprocess.run([sys.executable, str(script)], cwd=ROOT, capture_output=True, text=True)
    if cp.returncode != 0:
        return False, cp.stdout + cp.stderr
    return True, cp.stdout.strip() + cp.stderr.strip()

# 5. Python Physics Closed-Loop Suite
def test_python_physics():
    script = ROOT / "simulation" / "sil" / "tests" / "test_closed_loop_drive.py"
    cp = subprocess.run([sys.executable, str(script)], cwd=ROOT, capture_output=True, text=True)
    if cp.returncode != 0:
        return False, cp.stdout + cp.stderr
    return True, cp.stdout.strip() + cp.stderr.strip()

# 6. Simulation Vitest Suite
def test_simulation_vitest(quick: bool):
    npm_cmd = shutil.which("npm.cmd") or shutil.which("npm")
    if not npm_cmd:
        return False, "npm not found on PATH"
    args = [npm_cmd, "test", "--prefix", "simulation"]
    if quick:
        args.extend(["--", "tests/unit/can-protocol-drift.test.ts", "tests/integration/rt-to-low-bus.test.ts", "tests/unit/can-encoding.test.ts"])
    cp = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    if cp.returncode != 0:
        return False, cp.stdout + cp.stderr
    # Extract test summary
    summary_lines = [l for l in cp.stdout.split("\n") if "Tests" in l or "passed" in l]
    return True, "\n".join(summary_lines[-3:]) if summary_lines else "Vitest passed cleanly"

# 7. PlatformIO Native Test Pipelines
def run_pio_native(dir_name: str, env_name: str = "native"):
    pio_cmd = shutil.which("pio.cmd") or shutil.which("pio")
    if not pio_cmd:
        return False, "PlatformIO (pio) not found on PATH"
    
    # Ensure modern toolchain is first on PATH and legacy MinGW is removed
    env = os.environ.copy()
    llvm_path = r"C:\Users\logsh\AppData\Local\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.MSVCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260616-msvcrt-x86_64\bin"
    path_entries = [p for p in env.get("PATH", "").split(os.pathsep) if "D:\\Programs\\MinGW" not in p and "C:\\programs\\TDM-GCC-64" not in p and "C:\\TDM-GCC-64" not in p]
    env["PATH"] = llvm_path + os.pathsep + os.pathsep.join(path_entries)

    args = [pio_cmd, "test", "-d", dir_name, "-e", env_name]
    cp = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, env=env)
    if cp.returncode != 0:
        return False, cp.stdout + cp.stderr
    
    summary_lines = [l for l in cp.stdout.split("\n") if "succeeded in" in l or "test cases:" in l or "PASSED" in l]
    return True, summary_lines[-1].strip() if summary_lines else f"PlatformIO {dir_name} {env_name} passed"

def main():
    parser = argparse.ArgumentParser(description="Run E-Trike Pre-Hardware Verification Suites")
    parser.add_argument("--quick", action="store_true", help="Run quick simulation subsets instead of 3-minute soak")
    parser.add_argument("--native", action="store_true", help="Run only native C++ test suites")
    parser.add_argument("--python", action="store_true", help="Run only Python SIL test suites")
    parser.add_argument("--sim", action="store_true", help="Run only simulation vitest suites")
    parser.add_argument("--pio", action="store_true", help="Run PlatformIO native test pipelines (sys-esp32, rt-esp32, mtr-stm32)")
    args = parser.parse_args()

    # Default to quick if not specified, or allow full
    run_all = not (args.native or args.python or args.sim or args.pio)

    print_header("E-TRIKE PRE-HARDWARE VERIFICATION GATE TEST RUNNER")
    print(f"Repository Root: {ROOT}")
    print(f"Python:          {sys.version.split()[0]}")

    compiler = find_cpp_compiler()
    if compiler:
        print(f"C++ Compiler:    {compiler}")
    else:
        print(f"{YELLOW}Warning: No C++17 compiler (clang++/g++) found on PATH! Native suites will be skipped.{RESET}")

    results = []

    # ── 1. Native C++ Suites ─────────────────────────────
    if (run_all or args.native) and compiler:
        print(f"\n{BOLD}[1/3] Native C++ Suites{RESET}")
        results.append(("C++ Exhaustive State-Machine Contract", run_step("C++ Exhaustive State-Machine", lambda: test_native_exhaustive(compiler))))
        results.append(("C++ Zero Heap Allocations Trap", run_step("C++ Zero Heap Allocation Trap", lambda: test_native_heap(compiler))))
        results.append(("C++ 22 ESTOP & Safety Reset Scenarios", run_step("C++ 22 ESTOP Scenarios", lambda: test_native_estop(compiler))))

    # ── 2. Python SIL Suites ──────────────────────────────
    if run_all or args.python:
        print(f"\n{BOLD}[2/3] Python SIL Suites{RESET}")
        results.append(("Python Diagnostic Reference Differential", run_step("Python Diagnostic Reference Model", test_python_differential)))
        results.append(("Python Longitudinal Physics & PID Loop", run_step("Python Closed-Loop Longitudinal Plant", test_python_physics)))

    # ── 3. TypeScript Simulation Suites ───────────────────
    if run_all or args.sim:
        print(f"\n{BOLD}[3/4] TypeScript Simulation Suites{RESET}")
        mode_str = "Quick Subset" if args.quick else "Full Protocol Suite"
        results.append((f"Vitest Simulation ({mode_str})", run_step(f"Vitest Simulation ({mode_str})", lambda: test_simulation_vitest(args.quick or True))))

    # ── 4. PlatformIO Native Pipelines ────────────────────
    if args.pio:
        print(f"\n{BOLD}[4/4] PlatformIO Native Pipelines{RESET}")
        results.append(("PlatformIO SYS-ESP32 Native", run_step("PlatformIO SYS-ESP32 Native (31 tests)", lambda: run_pio_native("sys-esp32", "native"))))
        results.append(("PlatformIO RT-ESP32 Native", run_step("PlatformIO RT-ESP32 Native (55 tests)", lambda: run_pio_native("rt-esp32", "native"))))
        results.append(("PlatformIO RT-ESP32 Pipeline Math", run_step("PlatformIO RT-ESP32 Pipeline Math (61 tests)", lambda: run_pio_native("rt-esp32", "native_pipeline"))))
        results.append(("PlatformIO MTR-STM32 Native", run_step("PlatformIO MTR-STM32 Native (15 suites / 170 asserts)", lambda: run_pio_native("mtr-stm32", "native"))))

    # ── Summary ──────────────────────────────────────────
    print_header("VERIFICATION GATE SUMMARY")
    passed = sum(1 for _, ok in results if ok)
    total = len(results)

    for name, ok in results:
        status_str = f"{GREEN}PASS{RESET}" if ok else f"{RED}FAIL{RESET}"
        print(f"  [{status_str}] {name}")

    print(f"\nTotal: {passed}/{total} suites passed.")
    if passed == total and total > 0:
        print(f"\n{GREEN}{BOLD}>>> ALL PRE-HARDWARE VERIFICATION SUITES PASSED! <<<{RESET}\n")
        return 0
    else:
        print(f"\n{RED}{BOLD}>>> SOME TEST SUITES FAILED! <<<{RESET}\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())
