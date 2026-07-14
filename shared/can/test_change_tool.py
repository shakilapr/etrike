import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/can_change.py"

class ChangeToolTests(unittest.TestCase):
    def run_tool(self, *args):
        result = subprocess.run([sys.executable, str(TOOL), *args, "--json"], cwd=ROOT,
                                text=True, capture_output=True)
        self.assertEqual(0, result.returncode, result.stderr + result.stdout)
        return json.loads(result.stdout)

    def test_inspect_reports_manual_impact(self):
        result = self.run_tool("inspect", "0x721")
        self.assertEqual("CAN-MANUAL-SEB-STATUS", result[0]["manual_mapping"])
        self.assertIn("sys-esp32", result[0]["build_targets"])

    def test_verify_accepts_reviewed_hashes(self):
        self.assertTrue(self.run_tool("verify")["ok"])

    def test_multi_bus_id_returns_both_instances(self):
        result = self.run_tool("inspect", "0x7FD")
        self.assertEqual({"high", "low"}, {item["bus"] for item in result})

if __name__ == "__main__":
    unittest.main()
