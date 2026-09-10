"""Compile and run the host-compilable DiagnosticManager unit test.

The firmware manager (shared/diagnostics.h) is embedded-safe C++17; we verify it
on the host with the same C++17 compiler used for the protocol codec tests.
"""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

try:
    from protocol.tests.python.compiler_helper import find_cpp_compiler
except (ImportError, ModuleNotFoundError):
    from compiler_helper import find_cpp_compiler

ROOT = Path(__file__).resolve().parents[3]


class TestDiagnosticManager(unittest.TestCase):
    def test_diagnostics_manager_compiles_and_runs(self):
        compiler = find_cpp_compiler("c++17")
        if compiler is None:
            self.skipTest("a C++17 compiler is not available")
        source = ROOT / "shared" / "test_diagnostics.cpp"
        self.assertTrue(source.exists(), source)
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "test_diagnostics"
            if sys.platform == "win32":
                executable = executable.with_suffix(".exe")
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    f"-I{ROOT}",
                    str(source),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, compile_result.returncode, compile_result.stdout + compile_result.stderr)
            run_result = subprocess.run(
                [str(executable)], cwd=ROOT, text=True, capture_output=True, check=False
            )
            self.assertEqual(0, run_result.returncode, run_result.stdout + run_result.stderr)
            self.assertIn("PASS", run_result.stdout)


if __name__ == "__main__":
    unittest.main()
