from __future__ import annotations

import json
import unittest
from pathlib import Path

from protocol.codecs.python import Frame, decode, decode_into, encode
from protocol.codecs.python import generated, pwt, seb, ses
from protocol.generated.python.etrike_protocol import METADATA

ROOT = Path(__file__).resolve().parents[2]


class SelectedCodecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.vectors = json.loads(
            (ROOT / "vectors" / "payload-v1.json").read_text(encoding="utf-8")
        )["vectors"]

    def test_shared_payload_vectors(self) -> None:
        for vector in self.vectors:
            metadata = METADATA[vector["message"]]
            instance = next(item for item in metadata["instances"] if item["bus"] == vector["bus"])
            frame = Frame(
                vector["bus"],
                instance["id"],
                vector["frame_format"],
                bytes.fromhex(vector["payload"]),
            )
            with self.subTest(vector=vector["id"]):
                status, value = decode(vector["message"], frame)
                self.assertEqual(vector["status"], status)
                if status == "unsupported_semantics":
                    self.assertEqual(frame.data, value["raw"])
                if status != "ok" or "values" not in vector:
                    continue
                for key, expected in vector["values"].items():
                    self.assertAlmostEqual(expected, value[key])
                encode_status, encoded = encode(
                    vector["message"], vector["values"], bus=vector["bus"]
                )
                self.assertEqual("ok", encode_status)
                self.assertEqual(frame, encoded)

    def test_ses_command_typed_round_trip(self) -> None:
        values = {
            "alignment_enable": True,
            "control_enable": True,
            "target_angle_raw": 30000,
            "target_speed_raw": 125,
            "rolling_counter": 5,
            "vehicle_speed_raw": 5,
        }
        status, frame = ses.encode_command(values)
        self.assertEqual("ok", status)
        self.assertEqual(bytes.fromhex("030030757d530592"), frame.data)
        self.assertEqual(("ok", values), ses.decode_command(frame))

    def test_seb_pressure_command_and_overlapping_status(self) -> None:
        status, frame = seb.encode_command(
            {
                "control_enable": True,
                "control_mode": 1,
                "stroke_request_raw": 0,
                "pressure_request_raw": 80,
                "rolling_counter": 5,
            }
        )
        self.assertEqual("ok", status)
        self.assertEqual(bytes.fromhex("06000050000053fa"), frame.data)
        status, command = seb.decode_command(frame)
        self.assertEqual("ok", status)
        self.assertEqual(80, command["pressure_request_raw"])

        status_frame = Frame("low", seb.STATUS_ID, "standard", bytes.fromhex("450034120078a347"))
        status, value = seb.decode_status(status_frame)
        self.assertEqual("ok", status)
        self.assertEqual(0x1234, value["stroke_value_raw"])
        self.assertEqual(0x12, value["pressure_value_raw"])
        self.assertEqual(-23688, value["angle_value_raw"])

    def test_raw_error_version_and_little_endian_test_paths(self) -> None:
        ses_error = Frame("low", ses.ERROR_INFO_ID, "standard", bytes.fromhex("018004010000002a"))
        self.assertEqual(ses_error.data, ses.decode_error_info(ses_error)[1]["raw"])
        ses_version = Frame("low", ses.VERSION_ID, "standard", bytes(range(8)))
        self.assertEqual(("unsupported_semantics", {"raw": bytes(range(8))}), ses.decode_version(ses_version))

        seb_version = Frame("low", seb.VERSION_ID, "standard", bytes.fromhex("c80d000000000000"))
        status, version = seb.decode_version(seb_version)
        self.assertEqual("ok", status)
        self.assertEqual((0xC8, 0x0D), (version["software_raw"], version["hardware_raw"]))
        telemetry = Frame("low", seb.TEST_ID, "standard", bytes.fromhex("00feff5000001800"))
        self.assertEqual(
            {
                "motor_current_raw": -2,
                "ecu_temperature_raw": 0x50,
                "supply_voltage_raw": 0x1800,
            },
            seb.decode_test(telemetry)[1],
        )

    def test_identity_integrity_constants_ranges_and_atomic_output(self) -> None:
        valid = Frame("low", ses.STATUS_ID, "standard", bytes.fromhex("410030751000a348"))
        cases = [
            (Frame("high", valid.id, valid.frame_format, valid.data), "wrong_message_id"),
            (Frame(valid.bus, valid.id + 1, valid.frame_format, valid.data), "wrong_message_id"),
            (Frame(valid.bus, valid.id, "extended", valid.data), "wrong_frame_format"),
            (Frame(valid.bus, valid.id, valid.frame_format, valid.data, 7), "unexpected_length"),
            (Frame(valid.bus, valid.id, valid.frame_format, valid.data[:-1]), "unexpected_length"),
            (Frame(valid.bus, valid.id, valid.frame_format, valid.data[:-1] + b"\x49"), "checksum_mismatch"),
        ]
        for frame, expected in cases:
            output = {"sentinel": 99}
            with self.subTest(expected=expected):
                self.assertEqual(expected, decode_into("ses:ses_status", frame, output))
                self.assertEqual({"sentinel": 99}, output)

        status, frame = ses.encode_command({"target_speed_raw": 600})
        self.assertEqual(("value_out_of_range", None), (status, frame))
        bad_pwt = Frame("powertrain", pwt.DCDC_COMMAND_ID, "extended", bytes.fromhex("0100ffffffffff00"))
        self.assertEqual("constant_mismatch", pwt.decode_dcdc_command(bad_pwt)[0])

    def test_generic_codec_cannot_compete_for_custom_messages(self) -> None:
        self.assertFalse(generated.is_generated("ses:ses_status"))
        with self.assertRaisesRegex(ValueError, "selects a custom codec"):
            generated.decode(
                "ses:ses_status",
                Frame("low", ses.STATUS_ID, "standard", bytes.fromhex("410030751000a348")),
            )


if __name__ == "__main__":
    unittest.main()
