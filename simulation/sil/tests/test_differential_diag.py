"""Differential Testing Suite: Python Reference Model vs C++ DiagnosticManager (§3.2 & §3.3).

Executes identical randomized operational sequences across PythonDiagnosticManager
and C++ DiagnosticManager (driven via diff_diag_driver.exe) to guarantee bit-for-bit
model-based differential equivalence.
"""

import subprocess
import sys
from pathlib import Path
import random
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT))
sys.path.append(str(ROOT / "simulation" / "sil" / "ref_models"))

from model_diag import PythonDiagnosticManager, DiagMeta, DiagState

DRIVER_EXE = ROOT / "native-test" / "test" / "diff_diag_driver.exe"


class CppDiagBridge:
    def __init__(self, exe_path: Path):
        self.proc = subprocess.Popen(
            [str(exe_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )

    def send_cmd(self, cmd: str) -> str:
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()
        return self.proc.stdout.readline().strip()

    def raise_event(self, diag_id: int, snapshot: int = None):
        if snapshot is not None:
            res = self.send_cmd(f"R {diag_id:x} {snapshot}")
        else:
            res = self.send_cmd(f"R {diag_id:x}")
        assert res == "OK"

    def recover(self, diag_id: int):
        res = self.send_cmd(f"V {diag_id:x}")
        assert res == "OK"

    def clear(self, diag_id: int):
        res = self.send_cmd(f"C {diag_id:x}")
        assert res == "OK"

    def clear_all(self):
        res = self.send_cmd("A")
        assert res == "OK"

    def on_estop_episode_cleared(self):
        res = self.send_cmd("E")
        assert res == "OK"

    def pop_pending_report(self):
        line = self.send_cmd("P")
        if line == "NONE":
            return None
        parts = line.split()
        assert parts[0] == "REP"
        return {
            "diag_id": int(parts[1], 16),
            "state": int(parts[2]),
            "occurrence_count": int(parts[3]),
            "report_counter": int(parts[4]),
            "flags": int(parts[5]),
            "snapshot_data": int(parts[6]),
        }

    def state_of(self, diag_id: int) -> int:
        line = self.send_cmd(f"S {diag_id:x}")
        return int(line.split()[1])

    def occurrence_of(self, diag_id: int) -> int:
        line = self.send_cmd(f"O {diag_id:x}")
        return int(line.split()[1])

    def close(self):
        try:
            self.send_cmd("Q")
            self.proc.wait(timeout=1.0)
        except Exception:
            self.proc.kill()


class TestDifferentialDiagnosticManager(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not DRIVER_EXE.exists():
            raise RuntimeError(f"Driver executable {DRIVER_EXE} does not exist!")

    def setUp(self):
        # Build metadata table matching the exact 42 implemented Diag IDs in diagnostics.hpp
        # 0x0101..0x010E, 0x0201..0x0213, 0x0301..0x0311
        # Extract metadata from protocol.generated.python.diagnostics if available or manual list
        from protocol.generated.python.diagnostics import REGISTRY
        meta_list = []
        for key, d in REGISTRY.items():
            if d.get("monitoring") == "IMPLEMENTED":
                is_estop = (d.get("reaction") == "ESTOP")
                meta_list.append(DiagMeta(
                    diag_id=d["id"],
                    name=d["key"],
                    latching=d["latching"],
                    is_estop_cause=is_estop,
                ))
        # Ensure order matches diag_index dense index:
        # C++ order:
        cpp_ids = [
            0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0106, 0x0107, 0x0108, 0x0109, 0x010A,
            0x010B, 0x010C, 0x010D, 0x010E, 0x0201, 0x0202, 0x0203, 0x0204, 0x0205, 0x0206,
            0x0208, 0x0209, 0x020A, 0x020B, 0x020C, 0x020D, 0x020F, 0x0210, 0x0211, 0x0212,
            0x0213, 0x0301, 0x0305, 0x0306, 0x0307, 0x0308, 0x0309, 0x030A, 0x030D, 0x030E,
            0x030F, 0x0311
        ]
        meta_by_id = {m.diag_id: m for m in meta_list}
        ordered_meta = [meta_by_id[id_val] for id_val in cpp_ids]

        self.py_mgr = PythonDiagnosticManager(ordered_meta)
        self.cpp_bridge = CppDiagBridge(DRIVER_EXE)
        self.test_ids = cpp_ids

    def tearDown(self):
        self.cpp_bridge.close()

    def test_randomized_differential_fuzzing(self):
        """Execute 1000 randomized operations and assert exact equivalence at every step."""
        random.seed(0x1337BEEF)
        ops = ["raise", "raise_snap", "recover", "clear", "clear_all", "estop_clear", "drain"]

        for step in range(1000):
            op = random.choice(ops)
            diag_id = random.choice(self.test_ids)

            if op == "raise":
                self.py_mgr.raise_event(diag_id)
                self.cpp_bridge.raise_event(diag_id)
            elif op == "raise_snap":
                snap = random.randint(0, 65535)
                self.py_mgr.raise_event(diag_id, snapshot=snap, supplied=True)
                self.cpp_bridge.raise_event(diag_id, snapshot=snap)
            elif op == "recover":
                self.py_mgr.recover(diag_id)
                self.cpp_bridge.recover(diag_id)
            elif op == "clear":
                self.py_mgr.clear(diag_id)
                self.cpp_bridge.clear(diag_id)
            elif op == "clear_all":
                self.py_mgr.clear_all()
                self.cpp_bridge.clear_all()
            elif op == "estop_clear":
                self.py_mgr.on_estop_episode_cleared()
                self.cpp_bridge.on_estop_episode_cleared()
            elif op == "drain":
                # Pop up to 5 reports
                for _ in range(5):
                    py_rep = self.py_mgr.pop_pending_report()
                    cpp_rep = self.cpp_bridge.pop_pending_report()
                    if py_rep is None:
                        self.assertIsNone(cpp_rep, f"Step {step}: Py had no report, but C++ had {cpp_rep}")
                        break
                    else:
                        self.assertIsNotNone(cpp_rep, f"Step {step}: Py had report {py_rep}, but C++ had None")
                        self.assertEqual(py_rep.diag_id, cpp_rep["diag_id"])
                        self.assertEqual(int(py_rep.state), cpp_rep["state"])
                        self.assertEqual(py_rep.occurrence_count, cpp_rep["occurrence_count"])
                        self.assertEqual(py_rep.report_counter, cpp_rep["report_counter"])
                        self.assertEqual(py_rep.flags, cpp_rep["flags"])
                        self.assertEqual(py_rep.snapshot_data, cpp_rep["snapshot_data"])

            # Verify spot-check state and occurrence count
            sample_id = random.choice(self.test_ids)
            py_state = int(self.py_mgr.state[self.py_mgr.id_to_index[sample_id]])
            cpp_state = self.cpp_bridge.state_of(sample_id)
            self.assertEqual(py_state, cpp_state, f"Step {step}: State mismatch on 0x{sample_id:04x}")

            py_occ = self.py_mgr.occurrence_count[self.py_mgr.id_to_index[sample_id]]
            cpp_occ = self.cpp_bridge.occurrence_of(sample_id)
            self.assertEqual(py_occ, cpp_occ, f"Step {step}: Occurrence mismatch on 0x{sample_id:04x}")

        # Final drain comparison
        while True:
            py_rep = self.py_mgr.pop_pending_report()
            cpp_rep = self.cpp_bridge.pop_pending_report()
            if py_rep is None:
                self.assertIsNone(cpp_rep)
                break
            self.assertIsNotNone(cpp_rep)
            self.assertEqual(py_rep.diag_id, cpp_rep["diag_id"])
            self.assertEqual(int(py_rep.state), cpp_rep["state"])
            self.assertEqual(py_rep.occurrence_count, cpp_rep["occurrence_count"])
            self.assertEqual(py_rep.report_counter, cpp_rep["report_counter"])
            self.assertEqual(py_rep.flags, cpp_rep["flags"])
            self.assertEqual(py_rep.snapshot_data, cpp_rep["snapshot_data"])


if __name__ == "__main__":
    unittest.main()
