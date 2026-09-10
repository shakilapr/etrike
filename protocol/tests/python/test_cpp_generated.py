from __future__ import annotations

import json
import shutil
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


class GeneratedCppTests(unittest.TestCase):
    def compile_and_run(self, source: str, standard: str):
        compiler = find_cpp_compiler(standard)
        if compiler is None:
            self.skipTest(f"a {standard}-capable C++ compiler is not available")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / Path(source).stem
            if sys.platform == "win32":
                executable = executable.with_suffix(".exe")
            compile_result = subprocess.run(
                [
                    compiler,
                    f"-std={standard}",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    f"-I{ROOT}",
                    str(ROOT / "protocol" / "tests" / "cpp" / source),
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
            return run_result.stdout

    def test_transport_compatibility_compiles_as_cpp11(self):
        self.compile_and_run("test_compat_cpp11.cpp", "c++11")

    def test_compatibility_boundary_compiles_as_cpp17(self):
        output = self.compile_and_run("test_compat.cpp", "c++17")
        self.assertIn("PASS", output)

    def test_generated_codecs_compile_and_run_language_neutral_vectors(self):
        compiler = find_cpp_compiler("c++17")
        if compiler is None:
            self.skipTest("a C++17-capable compiler is not available")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "generated-vectors"
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
                    str(ROOT / "protocol" / "tests" / "cpp" / "test_generated_vectors.cpp"),
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
            actual = {
                line.split()[1]: line.split()[2]
                for line in run_result.stdout.splitlines()
                if line.startswith("VECTOR ")
            }
            document = json.loads(
                (ROOT / "protocol" / "vectors" / "payload-v1.json").read_text(encoding="utf-8")
            )
            generated_messages = {
                item["message"]
                for item in json.loads(
                    (ROOT / "protocol" / "generated" / "capabilities.json").read_text(encoding="utf-8")
                )["languages"]["cpp"]
                if item["strategy"] == "generated"
            }
            # C++ harness emits one success vector per generated message. Each
            # emitted payload must be one of the canonical ok vectors for that
            # message (payload-v1.json), not necessarily the first-listed.
            valid = {}
            for vector in document["vectors"]:
                if vector["message"] not in generated_messages or vector["status"] != "ok":
                    continue
                valid.setdefault(vector["message"], set()).add((vector["payload"] or "-").lower())
            self.assertEqual(set(actual.keys()), set(valid.keys()))
            for message, payload in actual.items():
                self.assertIn(payload.lower(), valid[message], f"{message} is not a canonical ok vector")


if __name__ == "__main__":
    unittest.main()
