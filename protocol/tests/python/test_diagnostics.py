from __future__ import annotations

import copy
import unittest

from protocol.tools.protocol import (
    ContractError,
    diagnostics_hash,
    load_diagnostics,
    validate_diagnostics,
)


def _index(doc, key):
    for i, entry in enumerate(doc["diagnostics"]):
        if entry["key"] == key:
            return i
    raise KeyError(key)


class DiagnosticsRegistryTests(unittest.TestCase):
    def setUp(self):
        self.doc = load_diagnostics()

    def test_registry_counts(self):
        validate_diagnostics(self.doc)
        total = len(self.doc["diagnostics"])
        impl = [e for e in self.doc["diagnostics"] if e["monitoring"] == "IMPLEMENTED"]
        not_impl = [e for e in self.doc["diagnostics"] if e["monitoring"] == "NOT_IMPLEMENTED"]
        self.assertEqual(total, 149)
        self.assertEqual(len(impl), 43)
        self.assertEqual(len(not_impl), 106)

    def test_hash_deterministic_and_length(self):
        self.assertEqual(diagnostics_hash(self.doc), diagnostics_hash(copy.deepcopy(self.doc)))
        self.assertEqual(len(diagnostics_hash(self.doc)), 64)

    def test_hash_ignores_ordering_description_disposition(self):
        base = diagnostics_hash(self.doc)
        shuffled = copy.deepcopy(self.doc)
        shuffled["diagnostics"] = list(reversed(shuffled["diagnostics"]))
        shuffled["diagnostics"][0]["description"] = "edited; must not affect compatibility hash"
        self.assertEqual(base, diagnostics_hash(shuffled))

    def test_hash_changes_when_snapshot_semantics_change(self):
        base = diagnostics_hash(self.doc)
        mutated = copy.deepcopy(self.doc)
        for entry in mutated["diagnostics"]:
            if entry.get("key") == "SYS_CAN_INVALID_DLC":
                entry["snapshot"]["fields"][1]["bits"] = "13:4"
                break
        self.assertNotEqual(base, diagnostics_hash(mutated))

    def test_implemented_enum_unique_and_estop_latching(self):
        validated = validate_diagnostics(self.doc)
        names = [item[2] for item in validated["implemented"]]
        self.assertEqual(len(names), len(set(names)))
        for _id, _key, _enum, latching, is_estop in validated["implemented"]:
            if is_estop:
                self.assertTrue(latching)

    def test_negative_cases(self):
        # duplicate id
        bad = copy.deepcopy(self.doc)
        bad["diagnostics"][1]["id"] = bad["diagnostics"][0]["id"]
        with self.assertRaisesRegex(ContractError, "duplicate diagnostic id"):
            validate_diagnostics(bad)

        # duplicate key
        bad = copy.deepcopy(self.doc)
        bad["diagnostics"][1]["key"] = bad["diagnostics"][0]["key"]
        with self.assertRaisesRegex(ContractError, "duplicate diagnostic key"):
            validate_diagnostics(bad)

        # wrong reporter namespace
        bad = copy.deepcopy(self.doc)
        bad["diagnostics"][_index(bad, "SYS_ESTOP_BUTTON_ASSERTED")]["id"] = 0x0201
        with self.assertRaisesRegex(ContractError, "id high byte"):
            validate_diagnostics(bad)

        # reaction on NOT_IMPLEMENTED
        bad = copy.deepcopy(self.doc)
        bad["diagnostics"][_index(bad, "SYS_CAN_ESTOP_FLOOD")]["reaction"] = "WARN"
        with self.assertRaisesRegex(ContractError, "must not carry a reaction"):
            validate_diagnostics(bad)

        # missing reaction on IMPLEMENTED
        bad = copy.deepcopy(self.doc)
        del bad["diagnostics"][_index(bad, "SYS_ESTOP_BUTTON_ASSERTED")]["reaction"]
        with self.assertRaisesRegex(ContractError, "requires a valid reaction"):
            validate_diagnostics(bad)

        # UNOBSERVABLE + IMPLEMENTED
        bad = copy.deepcopy(self.doc)
        bad["diagnostics"][_index(bad, "SYS_ESTOP_BUTTON_ASSERTED")]["observability"] = "UNOBSERVABLE"
        with self.assertRaisesRegex(ContractError, "UNOBSERVABLE requires"):
            validate_diagnostics(bad)

        # NOT_IMPLEMENTED with disposition ACTIVE
        bad = copy.deepcopy(self.doc)
        bad["diagnostics"][_index(bad, "SYS_CAN_ESTOP_FLOOD")]["disposition"] = "ACTIVE"
        with self.assertRaisesRegex(ContractError, "cannot be disposition ACTIVE"):
            validate_diagnostics(bad)

        # OMITTED + IMPLEMENTED
        bad = copy.deepcopy(self.doc)
        bad["diagnostics"][_index(bad, "SYS_ESTOP_BUTTON_ASSERTED")]["disposition"] = "OMITTED"
        with self.assertRaisesRegex(ContractError, "OMITTED disposition requires"):
            validate_diagnostics(bad)

        # bad bit range
        bad = copy.deepcopy(self.doc)
        bad["diagnostics"][_index(bad, "SYS_ESTOP_BUTTON_ASSERTED")]["snapshot"] = {
            "encoding": "BITFIELD16",
            "fields": [{"name": "x", "bits": "20:4"}],
        }
        with self.assertRaisesRegex(ContractError, "bits out of range"):
            validate_diagnostics(bad)

        # overlapping bitfields
        bad = copy.deepcopy(self.doc)
        bad["diagnostics"][_index(bad, "SYS_ESTOP_BUTTON_ASSERTED")]["snapshot"] = {
            "encoding": "BITFIELD16",
            "fields": [{"name": "a", "bits": "15:8"}, {"name": "b", "bits": "15:8"}],
        }
        with self.assertRaisesRegex(ContractError, "overlaps"):
            validate_diagnostics(bad)

        # invalid snapshot scale
        bad = copy.deepcopy(self.doc)
        bad["diagnostics"][_index(bad, "SYS_ESTOP_BUTTON_ASSERTED")]["snapshot"] = {
            "encoding": "U16", "quantity": "x", "unit": "ms", "scale": "bad", "saturate": True,
        }
        with self.assertRaisesRegex(ContractError, "numeric scale"):
            validate_diagnostics(bad)


if __name__ == "__main__":
    unittest.main()
