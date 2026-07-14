"""Independent contract tests for CAN schema and generated codec metadata."""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from can_signals_schema import (  # noqa: E402
    MessageDef, SignalDef, load_can_database_dir,
    network_contract_hash, wire_protocol_hash,
)
from generate_cpp_codecs import generate_codec_manifest  # noqa: E402


class ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db = load_can_database_dir(ROOT)

    def test_wire_and_network_hashes_are_distinct_and_stable_length(self):
        self.assertEqual(64, len(wire_protocol_hash(self.db)))
        self.assertEqual(64, len(network_contract_hash(self.db)))
        self.assertNotEqual(wire_protocol_hash(self.db), network_contract_hash(self.db))

    def test_manifest_keeps_both_bus_instances(self):
        manifest = json.loads(generate_codec_manifest(self.db))
        rt = [m for m in manifest["messages"] if m["key"] == "rt_heartbeat"]
        self.assertEqual({"high", "low"}, {m["bus"] for m in rt})
        self.assertEqual(2, len(rt))

    def test_counter_semantics_are_machine_readable(self):
        manifest = json.loads(generate_codec_manifest(self.db))
        heartbeats = [m for m in manifest["messages"] if "heartbeat" in m["key"]]
        for message in heartbeats:
            counters = [s for s in message["signals"] if s["counter_kind"]]
            self.assertEqual(1, len(counters), message["instance"])
            self.assertEqual("wrapping", counters[0]["counter_kind"])
            self.assertEqual("ecu_restart", counters[0]["reset_scope"])

    def test_signal_outside_dlc_is_rejected(self):
        with self.assertRaises(ValueError):
            MessageDef(id=1, name="BAD", dlc=1, sender="RT", signals=[
                SignalDef(name="TooWide", byte=0, bit_offset=0, size=16)
            ])


if __name__ == "__main__":
    unittest.main()
