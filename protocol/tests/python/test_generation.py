from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

from protocol.tools.protocol import GENERATED, load_model, render_outputs, validate_model, write_or_check


class GenerationTests(unittest.TestCase):
    def test_render_is_deterministic(self):
        model = load_model()
        validated = validate_model(model)
        self.assertEqual(render_outputs(model, validated), render_outputs(model, validated))

    def test_generated_files_are_current_without_writing(self):
        model = load_model()
        validated = validate_model(model)
        before = {path: path.stat().st_mtime_ns for path in GENERATED.rglob("*") if path.is_file()}
        self.assertEqual([], write_or_check(render_outputs(model, validated), check=True))
        after = {path: path.stat().st_mtime_ns for path in GENERATED.rglob("*") if path.is_file()}
        self.assertEqual(before, after)

    def test_cli_check_and_ambiguous_inspect(self):
        root = Path(__file__).resolve().parents[3]
        check = subprocess.run(
            [sys.executable, "-m", "protocol.tools.protocol", "generate", "--check"],
            cwd=root, text=True, capture_output=True, check=False,
        )
        self.assertEqual(0, check.returncode, check.stderr)
        inspect = subprocess.run(
            [sys.executable, "-m", "protocol.tools.protocol", "inspect", "0x001"],
            cwd=root, text=True, capture_output=True, check=False,
        )
        self.assertEqual(2, inspect.returncode)
        self.assertIn("--bus is required", inspect.stderr)

    def test_discovery_capability_error_and_schema_manifests(self):
        manifest = json.loads((GENERATED / "discovery.json").read_text(encoding="utf-8"))
        capabilities = json.loads((GENERATED / "capabilities.json").read_text(encoding="utf-8"))
        errors = json.loads((GENERATED / "errors.json").read_text(encoding="utf-8"))
        schema = json.loads((GENERATED / "contract-schema.json").read_text(encoding="utf-8"))
        self.assertEqual(32, len(manifest["messages"]))
        self.assertEqual({"cpp", "python", "typescript"}, set(capabilities["languages"]))
        cpp = {item["message"]: item for item in capabilities["languages"]["cpp"]}
        self.assertEqual("typed", cpp["host:host_drive_cmd"]["payload"])
        self.assertTrue(cpp["host:host_drive_cmd"]["semantic_decode"])
        self.assertEqual("metadata_only", cpp["ses:ses_status"]["payload"])
        self.assertFalse(cpp["ses:ses_status"]["semantic_decode"])
        self.assertEqual("ses:ses_version", errors["unsupported"][0]["message"])
        self.assertEqual(1, schema["properties"]["schema_version"]["const"])

    def test_cpp_only_generates_payload_codecs_for_generated_strategy(self):
        header = (GENERATED / "cpp" / "etrike_protocol.hpp").read_text(encoding="utf-8")
        self.assertIn("struct HostDriveCmd", header)
        self.assertIn("encode_host_drive_cmd", header)
        self.assertIn("namespace generated = ::etrike::protocol::generated", header)
        self.assertNotIn("struct SesStatus", header)
        self.assertNotIn("decode_ses_status", header)


if __name__ == "__main__":
    unittest.main()
