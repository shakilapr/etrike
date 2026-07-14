#!/usr/bin/env python3
"""Discover and verify the impact of E-Trike CAN contract changes."""

from __future__ import annotations
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
import yaml

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "shared/can/generated/codec_manifest.json"
IMPACT = ROOT / "shared/can/generated/change_impact.json"
MAPPINGS = ROOT / "shared/can/manual-mappings.yaml"


def load():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    impacts = json.loads(IMPACT.read_text(encoding="utf-8"))["messages"] if IMPACT.exists() else []
    mappings = yaml.safe_load(MAPPINGS.read_text(encoding="utf-8")).get("mappings", [])
    return manifest["messages"], impacts, mappings


def normalize(value: str) -> str:
    return re.sub(r"[^A-Z0-9]", "", value.upper())


def select(records, query):
    try:
        wanted_id = int(query, 0)
    except ValueError:
        wanted_id = None
    return [r for r in records if
            (wanted_id is not None and int(r["id"], 0) == wanted_id) or
            normalize(r.get("message", r.get("key", ""))) == normalize(query)]


def validate(messages, mappings):
    errors = []
    ids = set()
    by_name = {normalize(m["key"]): m for m in messages}
    for item in mappings:
        if item["id"] in ids: errors.append(f"duplicate mapping id: {item['id']}")
        ids.add(item["id"])
        message = by_name.get(normalize(item["message"]))
        if not message:
            errors.append(f"{item['id']}: unknown message {item['message']}")
            continue
        if item.get("reviewed_wire_hash") != message["wire_hash"]:
            errors.append(f"{item['id']}: stale reviewed_wire_hash for {item['message']}")
        for field in ("adapter",):
            if not (ROOT / item[field]).is_file(): errors.append(f"{item['id']}: missing {item[field]}")
        for field in ("consumers", "tests"):
            if not item.get(field): errors.append(f"{item['id']}: no {field}")
            for path in item.get(field, []):
                if not (ROOT / path).is_file(): errors.append(f"{item['id']}: missing {path}")
    for finding in unregistered(mappings):
        errors.append(f"unregistered wire access: {finding['file']}:{finding['line']}")
    debug_metadata = subprocess.run(
        [sys.executable, str(ROOT / "shared/can/generate_can_ts.py"), "--check"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if debug_metadata.returncode:
        detail = (debug_metadata.stdout + debug_metadata.stderr).strip()
        errors.append(f"stale debug-tool CAN metadata: {detail}")
    return errors


def unregistered(mappings):
    allowed = {str((ROOT / m["adapter"]).resolve()) for m in mappings}
    allowed |= {str((ROOT / p).resolve()) for m in mappings for p in m.get("consumers", [])}
    findings = []
    roots = ["rt-esp32/src", "sys-esp32/src", "mtr-stm32/src", "jetson/src/autoware_vehicle_bridge/src"]
    pattern = re.compile(r"(?:frame|fr)\.data\[\d+\]|\.to_frame\(|::from_frame\(|(?:\.id|can_id)\s*(?:=|==)\s*0x[0-9A-Fa-f]+")
    for base in roots:
        for path in (ROOT / base).rglob("*"):
            if path.suffix not in (".h", ".hpp", ".cpp") or str(path.resolve()) in allowed or "driver" in path.name:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
                code = line.split("//", 1)[0]
                if pattern.search(code):
                    findings.append({"file": str(path.relative_to(ROOT)).replace("\\", "/"), "line": number, "text": line.strip()})
    return findings


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["inspect", "affected", "verify", "list-manual", "list-unregistered"])
    parser.add_argument("query", nargs="?")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    messages, impacts, mappings = load()

    if args.command == "inspect":
        if not args.query: parser.error("inspect requires a message name or ID")
        result = select(impacts, args.query)
    elif args.command == "affected":
        if not args.query: parser.error("affected requires a file")
        needle = args.query.replace("\\", "/")
        result = [r for r in impacts if needle.endswith(r["source"]) or r["source"].endswith(needle)]
    elif args.command == "list-manual":
        result = mappings
    elif args.command == "list-unregistered":
        result = unregistered(mappings)
    else:
        errors = validate(messages, mappings)
        if args.query:
            selected = select(messages, args.query)
            if not selected: errors.append(f"unknown message: {args.query}")
        result = {"ok": not errors, "errors": errors,
                  "generated_verify": "python shared/can/generate_code.py --verify"}
        if errors:
            print(json.dumps(result, indent=2) if args.json else "\n".join(errors))
            return 1

    if args.json:
        print(json.dumps(result, indent=2))
    elif isinstance(result, dict):
        print("CAN contract verification passed")
    elif not result:
        print("No matches")
    else:
        for item in result:
            print(json.dumps(item, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
