from __future__ import annotations

import copy
import unittest

from protocol.tools.protocol import (
    ContractError,
    inspect_message,
    load_model,
    validate_model,
)


class ContractSchemaTests(unittest.TestCase):
    def setUp(self):
        self.model = load_model()

    def test_current_contract_is_complete_and_matches_frozen_baseline(self):
        validated = validate_model(self.model)
        self.assertEqual(34, len(validated["messages"]))
        self.assertEqual(44, len(validated["instances"]))
        self.assertEqual("raw_only", validated["messages"]["ses:ses_version"]["layout"]["semantic_support"])
        self.assertEqual("extended", validated["messages"]["pwt:pwt_dcdc_cmd"]["instances"][0]["frame_format"])

    def test_duplicate_canonical_key_is_rejected(self):
        model = copy.deepcopy(self.model)
        model["messages"].append(copy.deepcopy(model["messages"][0]))
        with self.assertRaisesRegex(ContractError, "duplicate canonical message key"):
            validate_model(model, check_baseline=False)

    def test_duplicate_bus_and_id_is_rejected_even_if_format_differs(self):
        model = copy.deepcopy(self.model)
        duplicate = model["messages"][1]["instances"][0]
        duplicate["bus"] = model["messages"][0]["instances"][0]["bus"]
        duplicate["id"] = model["messages"][0]["instances"][0]["id"]
        duplicate["frame_format"] = "extended"
        with self.assertRaisesRegex(ContractError, r"duplicate bus\+ID"):
            validate_model(model, check_baseline=False)

    def test_custom_codec_requires_implementation_and_vectors(self):
        model = copy.deepcopy(self.model)
        message = next(item for item in model["messages"] if item["key"] == "ses_status")
        del message["codec"]["implementation_id"]
        with self.assertRaisesRegex(ContractError, "requires implementation_id"):
            validate_model(model, check_baseline=False)

    def test_profile_codec_requires_profile_id(self):
        model = copy.deepcopy(self.model)
        message = next(item for item in model["messages"] if item["key"] == "host_drive_cmd")
        message["codec"] = {"strategy": "profile", "vector_set_id": "payload-v1"}
        with self.assertRaisesRegex(ContractError, "requires profile_id"):
            validate_model(model, check_baseline=False)

    def test_generated_codec_rejects_competing_implementation(self):
        model = copy.deepcopy(self.model)
        message = next(item for item in model["messages"] if item["key"] == "host_drive_cmd")
        message["codec"]["implementation_id"] = "other-v1"
        with self.assertRaisesRegex(ContractError, "competing implementation"):
            validate_model(model, check_baseline=False)

    def test_field_outside_dlc_is_rejected(self):
        model = copy.deepcopy(self.model)
        message = next(item for item in model["messages"] if item["key"] == "host_brake_req")
        message["layout"]["fields"][0]["bits"] = 40
        with self.assertRaisesRegex(ContractError, "exceeds DLC"):
            validate_model(model, check_baseline=False)

    def test_overlapping_fields_are_rejected(self):
        model = copy.deepcopy(self.model)
        message = next(item for item in model["messages"] if item["key"] == "host_light_cmd")
        message["layout"]["fields"][1]["bit"] = 0
        with self.assertRaisesRegex(ContractError, "overlap"):
            validate_model(model, check_baseline=False)

    def test_ambiguous_id_requires_bus(self):
        validated = validate_model(self.model)
        with self.assertRaisesRegex(ContractError, "--bus is required"):
            inspect_message(self.model, validated, "0x210", None)
        result = inspect_message(self.model, validated, "0x210", "low")
        self.assertEqual("rt:rt_state_rpt", result["canonical_key"])
        self.assertEqual("low", result["instance"]["bus"])

    def test_same_frame_route_requires_identical_identity(self):
        model = copy.deepcopy(self.model)
        message = next(item for item in model["messages"] if item["key"] == "host_light_cmd")
        message["instances"][1]["id"] = "0x303"
        with self.assertRaisesRegex(ContractError, "same_frame identity differs"):
            validate_model(model, check_baseline=False)

    def test_regenerated_route_allows_new_identity_and_requires_explicit_semantics(self):
        model = copy.deepcopy(self.model)
        route = next(item for item in model["network"]["routes"] if item["key"] == "rt-h2l-lights")
        route["semantics"] = "regenerated"
        message = next(item for item in model["messages"] if item["key"] == "host_light_cmd")
        message["instances"][1]["id"] = "0x303"
        message["instances"][1]["semantics"] = "regenerated"
        validate_model(model, check_baseline=False)


if __name__ == "__main__":
    unittest.main()
