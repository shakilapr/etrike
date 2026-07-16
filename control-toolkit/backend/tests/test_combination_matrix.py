"""Smoke the combination matrix (quick subset) so CI stays fast.

Full dense matrix (~3k cases) is run via:
  python scripts/combination_matrix_qa.py
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

_SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from combination_matrix_qa import count_cases, run_matrix  # noqa: E402


def test_matrix_planned_counts_dense_is_thousands():
    counts = count_cases(True)
    total = sum(counts.values())
    assert total >= 2000
    assert counts["kinematics_intent"] >= 1000
    assert counts["motion_then_brake"] >= 20


@pytest.mark.timeout(120)
def test_matrix_quick_inprocess_all_pass():
    # Cap per suite so CI stays fast but still multi-suite
    report = run_matrix(dense=False, live_base=None, limit=12, suites=None)
    s = report["summary"]
    assert s["total"] >= 40
    assert s["failed"] == 0, report.get("failures", [])[:10]
