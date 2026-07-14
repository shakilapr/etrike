from __future__ import annotations

import json
import unittest
from pathlib import Path

from protocol.generated.python import etrike_protocol
from protocol.tools.protocol import load_model, validate_model


ROOT = Path(__file__).resolve().parents[2]


class PayloadVectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = json.loads((ROOT / "vectors" / "payload-v1.json").read_text(encoding="utf-8"))
        cls.vectors = cls.document["vectors"]
        cls.model = validate_model(load_model())

    def test_every_message_has_an_independent_success_vector(self):
        covered = {item["message"] for item in self.vectors if item["status"] in {"ok", "unsupported_semantics"}}
        self.assertEqual(set(self.model["messages"]), covered)

    def test_all_payload_vectors(self):
        for vector in self.vectors:
            with self.subTest(vector=vector["id"]):
                payload = bytes.fromhex(vector["payload"])
                status, decoded = etrike_protocol.decode(
                    vector["message"], payload, bus=vector["bus"], frame_format=vector["frame_format"]
                )
                self.assertEqual(vector["status"], status)
                if status != "ok" or "values" not in vector:
                    continue
                for key, expected in vector["values"].items():
                    self.assertAlmostEqual(expected, decoded[key])
                message = self.model["messages"][vector["message"]]
                if message["codec"]["strategy"] == "generated":
                    encode_status, encoded = etrike_protocol.encode(
                        vector["message"], vector["values"], bus=vector["bus"], frame_format=vector["frame_format"]
                    )
                    self.assertEqual("ok", encode_status)
                    self.assertEqual(payload, encoded)

    def test_decode_failure_leaves_output_unchanged(self):
        output = {"sentinel": 99}
        status = etrike_protocol.decode_into(
            "host:host_drive_cmd", b"\x00", output, bus="high", frame_format="standard"
        )
        self.assertEqual("unexpected_length", status)
        self.assertEqual({"sentinel": 99}, output)

    def test_pwt_extended_identity_and_constants(self):
        status, payload = etrike_protocol.encode(
            "pwt:pwt_dcdc_cmd", {"control": 1, "reset_control": 0},
            bus="powertrain", frame_format="extended",
        )
        self.assertEqual("ok", status)
        self.assertEqual(bytes.fromhex("01ffffffffffff00"), payload)
        status, _ = etrike_protocol.decode(
            "pwt:pwt_dcdc_cmd", payload, bus="powertrain", frame_format="standard"
        )
        self.assertEqual("wrong_frame_format", status)

    def test_ses_version_is_raw_and_never_interpreted(self):
        raw = bytes.fromhex("0102030405060708")
        status, value = etrike_protocol.decode(
            "ses:ses_version", raw, bus="low", frame_format="standard"
        )
        self.assertEqual("unsupported_semantics", status)
        self.assertEqual(raw, value["raw"])
        self.assertEqual({"raw"}, set(value))


class SequenceVectorTests(unittest.TestCase):
    def test_sequence_document_covers_required_state_boundaries(self):
        document = json.loads((ROOT / "vectors" / "sequences-v1.json").read_text(encoding="utf-8"))
        text = json.dumps(document)
        for expected in ("wrap", "duplicate", "gap", "reorder", "recovery", "frozen", "session_epoch"):
            self.assertIn(expected, text)
        independent = next(item for item in document["sequences"] if item["id"] == "same-id-independent-buses")
        self.assertEqual({"high", "low"}, {item["bus"] for item in independent["instances"]})


if __name__ == "__main__":
    unittest.main()
