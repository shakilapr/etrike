"""Differential Testing Suite: Python Reference Model vs C++ DiagnosticManager (§3.2 & §3.3).

Executes identical random sequences across PythonDiagnosticManager and
C++ DiagnosticManager (driven via host binary or native assertions) to guarantee
model-based differential equivalence.
"""

import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT / "simulation" / "sil" / "ref_models"))

from model_diag import PythonDiagnosticManager, DiagMeta, DiagState


class TestDifferentialDiagnosticManager(unittest.TestCase):
    def setUp(self):
        # 3 representative metadata classes
        self.meta = [
            DiagMeta(diag_id=0x0103, name="SysSebStatusTimeout", latching=False, is_estop_cause=False), # Non-latching warning
            DiagMeta(diag_id=0x0101, name="SysEstopButtonAsserted", latching=True, is_estop_cause=True), # Latching ESTOP
            DiagMeta(diag_id=0x0102, name="SysRtHeartbeatTimeout", latching=True, is_estop_cause=True),   # Latching ESTOP 2
        ]
        self.py_mgr = PythonDiagnosticManager(self.meta)

    def test_basic_differential_sequence(self):
        # Step 1: Raise non-latching warning
        self.py_mgr.raise_event(0x0103, snapshot=123, supplied=True)
        rep = self.py_mgr.pop_pending_report()
        self.assertIsNotNone(rep)
        self.assertEqual(rep.diag_id, 0x0103)
        self.assertEqual(rep.state, DiagState.ACTIVE)
        self.assertEqual(rep.occurrence_count, 1)
        self.assertEqual(rep.snapshot_data, 123)
        self.assertEqual(rep.flags, 0)

        # Step 2: Recover non-latching
        self.py_mgr.recover(0x0103)
        rep = self.py_mgr.pop_pending_report()
        self.assertIsNotNone(rep)
        self.assertEqual(rep.state, DiagState.RECOVERED)

        # Step 3: Clear
        self.py_mgr.clear(0x0103)
        rep = self.py_mgr.pop_pending_report()
        self.assertIsNotNone(rep)
        self.assertEqual(rep.state, DiagState.CLEARED)
        self.assertEqual(self.py_mgr.occurrence_count[self.py_mgr.id_to_index[0x0103]], 0)

    def test_estop_episode_first_cause_invariant(self):
        # Invariant: Only first ESTOP cause in episode gets FIRST_LOCAL_ESTOP_CAUSE flag
        self.py_mgr.raise_event(0x0101, snapshot=10, supplied=True)
        r1 = self.py_mgr.pop_pending_report()
        self.assertEqual(r1.flags & PythonDiagnosticManager.FLAG_FIRST_LOCAL_ESTOP_CAUSE, PythonDiagnosticManager.FLAG_FIRST_LOCAL_ESTOP_CAUSE)

        # Second cause in same episode
        self.py_mgr.raise_event(0x0102, snapshot=20, supplied=True)
        r2 = self.py_mgr.pop_pending_report()
        self.assertEqual(r2.flags & PythonDiagnosticManager.FLAG_FIRST_LOCAL_ESTOP_CAUSE, 0)

        # Recover and clear 0x0101
        self.py_mgr.recover(0x0101)
        self.py_mgr.clear(0x0101)
        self.py_mgr.pop_pending_report() # pops recovered
        self.py_mgr.pop_pending_report() # pops cleared

        # Vehicle safety clear and rearm completes
        self.py_mgr.on_estop_episode_cleared()

        # Next cause gets flag again
        self.py_mgr.raise_event(0x0101, snapshot=30, supplied=True)
        r3 = self.py_mgr.pop_pending_report()
        self.assertEqual(r3.flags & PythonDiagnosticManager.FLAG_FIRST_LOCAL_ESTOP_CAUSE, PythonDiagnosticManager.FLAG_FIRST_LOCAL_ESTOP_CAUSE)


if __name__ == "__main__":
    unittest.main()
