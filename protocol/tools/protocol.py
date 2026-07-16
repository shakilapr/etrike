"""Validate, generate, and inspect the canonical E-Trike protocol contracts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
CONTRACTS = PACKAGE_ROOT / "contracts"
VECTORS = PACKAGE_ROOT / "vectors"
GENERATED = PACKAGE_ROOT / "generated"
KEY_RE = re.compile(r"^[a-z][a-z0-9_.-]*$")
STRATEGIES = {"generated", "profile", "custom"}
SEMANTICS = {"same_frame", "regenerated", "independent"}
FORMATS = {"standard", "extended"}


class ContractError(ValueError):
    pass


def canonical_json(value: object, *, pretty: bool = False) -> str:
    if pretty:
        return json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ContractError(f"{path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ContractError(f"{path}: document root must be an object")
    return value


def parse_id(value: object) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 16 if value.lower().startswith("0x") else 10)
    raise ContractError(f"invalid CAN ID {value!r}")


def message_files(contracts: Path) -> list[Path]:
    return sorted(
        path for path in contracts.glob("*.yaml")
        if path.name != "network.yaml"
    )


def load_model(root: Path = PACKAGE_ROOT) -> dict:
    contracts = root / "contracts"
    network = read_json(contracts / "network.yaml")
    messages = list(network.get("messages", []))
    for path in message_files(contracts):
        document = read_json(path)
        if document.get("schema_version") != 1:
            raise ContractError(f"{path}: schema_version must be 1")
        messages.extend(document.get("messages", []))
    return {"network": network, "messages": messages, "root": root}


def canonical_key(message: dict) -> str:
    return f"{message.get('owner')}:{message.get('key')}"


def _field_limits(field: dict) -> tuple[float, float]:
    bits = field["bits"]
    if field.get("signed"):
        default_min, default_max = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    else:
        default_min, default_max = 0, (1 << bits) - 1
    factor = field.get("factor", 1)
    offset = field.get("offset", 0)
    return field.get("min", default_min * factor + offset), field.get("max", default_max * factor + offset)


def validate_model(model: dict, *, check_baseline: bool = True) -> dict:
    network, messages, root = model["network"], model["messages"], model["root"]
    if network.get("schema_version") != 1:
        raise ContractError("network.yaml: schema_version must be 1")
    buses = {bus.get("key") for bus in network.get("buses", [])}
    if not buses or None in buses:
        raise ContractError("network.yaml: buses require stable keys")
    nodes = set(network.get("nodes", []))
    vector_documents = [read_json(path) for path in sorted((root / "vectors").glob("*.json"))]
    vector_ids = {document.get("vector_set_id") for document in vector_documents}
    vectors = [vector for document in vector_documents for vector in document.get("vectors", [])]
    covered = {vector.get("message") for vector in vectors if vector.get("status") in {"ok", "unsupported_semantics"}}

    by_key: dict[str, dict] = {}
    by_instance: dict[tuple[str, int], tuple[str, dict, dict]] = {}
    for message in messages:
        key = canonical_key(message)
        if not KEY_RE.fullmatch(str(message.get("owner", ""))) or not KEY_RE.fullmatch(str(message.get("protocol", ""))) or not KEY_RE.fullmatch(str(message.get("key", ""))):
            raise ContractError(f"{key}: owner, protocol, and key must be stable lowercase keys")
        if key in by_key:
            raise ContractError(f"duplicate canonical message key {key}")
        by_key[key] = message
        dlc = message.get("dlc")
        if not isinstance(dlc, int) or not 0 <= dlc <= 8:
            raise ContractError(f"{key}: classic CAN DLC must be 0..8")
        if message.get("byte_order") not in {"big", "little"}:
            raise ContractError(f"{key}: byte_order must be big or little")
        codec = message.get("codec")
        if not isinstance(codec, dict) or set(codec) - {"strategy", "implementation_id", "profile_id", "vector_set_id"}:
            raise ContractError(f"{key}: exactly one codec object is required")
        strategy = codec.get("strategy")
        if strategy not in STRATEGIES:
            raise ContractError(f"{key}: strategy must be generated, profile, or custom")
        if strategy != "generated":
            id_name = "profile_id" if strategy == "profile" else "implementation_id"
            if not codec.get(id_name) or not codec.get("vector_set_id"):
                raise ContractError(f"{key}: {strategy} strategy requires {id_name} and vector_set_id")
            if codec["vector_set_id"] not in vector_ids:
                raise ContractError(f"{key}: unknown vector set {codec['vector_set_id']}")
        elif any(name in codec for name in ("implementation_id", "profile_id", "vector_set_id")):
            raise ContractError(f"{key}: generated strategy cannot select a competing implementation")

        layout = message.get("layout")
        if not isinstance(layout, dict) or layout.get("kind") not in {"signals", "opaque"}:
            raise ContractError(f"{key}: one signals or opaque layout is required")
        if layout["kind"] == "opaque":
            if layout.get("bytes") != dlc:
                raise ContractError(f"{key}: opaque layout must cover DLC")
        else:
            occupied: dict[int, str] = {}
            field_keys: set[str] = set()
            for field in layout.get("fields", []):
                field_key = field.get("key")
                if not KEY_RE.fullmatch(str(field_key or "")) or field_key in field_keys:
                    raise ContractError(f"{key}: invalid or duplicate field key {field_key!r}")
                field_keys.add(field_key)
                byte, bit, bits = field.get("byte"), field.get("bit"), field.get("bits")
                if not all(isinstance(item, int) for item in (byte, bit, bits)) or byte < 0 or not 0 <= bit < 8 or bits < 1 or bits > 64:
                    raise ContractError(f"{key}.{field_key}: invalid field position")
                if byte * 8 + bit + bits > dlc * 8:
                    raise ContractError(f"{key}.{field_key}: field exceeds DLC")
                for position in range(byte * 8 + bit, byte * 8 + bit + bits):
                    if position in occupied:
                        raise ContractError(f"{key}: fields {occupied[position]} and {field_key} overlap")
                    occupied[position] = field_key
                minimum, maximum = _field_limits(field)
                if minimum > maximum:
                    raise ContractError(f"{key}.{field_key}: min exceeds max")
                if "constant" in field and not minimum <= field["constant"] <= maximum:
                    raise ContractError(f"{key}.{field_key}: constant is outside range")
                for enum_value in field.get("enum", {}):
                    if not minimum <= int(enum_value) <= maximum:
                        raise ContractError(f"{key}.{field_key}: enum value is outside range")

        instances = message.get("instances")
        if not isinstance(instances, list) or not instances:
            raise ContractError(f"{key}: at least one explicit bus instance is required")
        for instance in instances:
            bus = instance.get("bus")
            can_id = parse_id(instance.get("id"))
            frame_format = instance.get("frame_format")
            if bus not in buses:
                raise ContractError(f"{key}: unknown bus {bus}")
            if frame_format not in FORMATS:
                raise ContractError(f"{key}: frame_format must be standard or extended")
            maximum = 0x7FF if frame_format == "standard" else 0x1FFFFFFF
            if not 0 <= can_id <= maximum:
                raise ContractError(f"{key}: ID is invalid for {frame_format} format")
            identity = (bus, can_id)
            if identity in by_instance:
                raise ContractError(f"duplicate bus+ID {bus}:0x{can_id:X} ({by_instance[identity][0]} and {key})")
            by_instance[identity] = (key, message, instance)
            if instance.get("semantics") not in SEMANTICS:
                raise ContractError(f"{key}: instance semantics must be same_frame, regenerated, or independent")
            if instance.get("sender") not in nodes or any(receiver not in nodes for receiver in instance.get("receivers", [])):
                raise ContractError(f"{key}: sender/receiver is not a declared node")
            if not isinstance(instance.get("cycle_ms"), int) or instance["cycle_ms"] < 0:
                raise ContractError(f"{key}: cycle_ms must be a non-negative integer")
        if key not in covered:
            raise ContractError(f"{key}: no successful payload vector")

    for route in network.get("routes", []):
        if route.get("semantics") not in {"same_frame", "regenerated"}:
            raise ContractError(f"route {route.get('key')}: invalid forwarding semantics")
        message = by_key.get(route.get("message"))
        if message is None:
            raise ContractError(f"route {route.get('key')}: unknown message")
        instance_by_bus = {instance["bus"]: instance for instance in message["instances"]}
        source, destination = instance_by_bus.get(route.get("from")), instance_by_bus.get(route.get("to"))
        if source is None or destination is None:
            raise ContractError(f"route {route.get('key')}: source and destination instances are required")
        if route["semantics"] == "same_frame":
            if parse_id(source["id"]) != parse_id(destination["id"]) or source["frame_format"] != destination["frame_format"]:
                raise ContractError(f"route {route.get('key')}: same_frame identity differs")
            if destination["semantics"] != "same_frame":
                raise ContractError(f"route {route.get('key')}: destination must declare same_frame")

    ses_version = by_key.get("ses:ses_version", {})
    if ses_version.get("layout", {}).get("semantic_support") != "raw_only":
        raise ContractError("ses:ses_version must remain raw_only pending vendor evidence")

    if check_baseline:
        _validate_baseline(root, by_instance)
    return {"messages": by_key, "instances": by_instance, "vectors": vector_documents, "network": network}


def _validate_baseline(root: Path, instances: dict) -> None:
    baseline = read_json(root / "contracts" / "baseline-manifest.json")
    if baseline.get("frozen") is not True:
        raise ContractError("baseline manifest must be frozen")
    expected = {}
    for row in baseline.get("instances", []):
        bus, id_text, frame_format, key, dlc, cycle_ms, sender, receivers = row
        expected[(bus, parse_id(id_text))] = (frame_format, key, dlc, cycle_ms, sender, receivers)
    actual = {
        identity: (instance["frame_format"], key, message["dlc"], instance["cycle_ms"], instance["sender"], instance["receivers"])
        for identity, (key, message, instance) in instances.items()
    }
    if actual != expected:
        missing = sorted(set(expected) - set(actual))
        extra = sorted(set(actual) - set(expected))
        changed = sorted(identity for identity in set(actual) & set(expected) if actual[identity] != expected[identity])
        raise ContractError(f"normalized contract differs from frozen baseline; missing={missing}, extra={extra}, changed={changed}")


def normalized_contract(validated: dict) -> dict:
    messages = []
    for key, message in sorted(validated["messages"].items()):
        item = {name: value for name, value in message.items() if name != "instances"}
        item["canonical_key"] = key
        item["instances"] = sorted(message["instances"], key=lambda value: (value["bus"], parse_id(value["id"])))
        messages.append(item)
    return {"schema_version": 1, "messages": messages}


def hashes(model: dict, validated: dict) -> tuple[str, str]:
    """Return (semantic/wire hash, network hash).

    SEMANTIC_HASH / WIRE_HASH are content hashes of the canonical message wire
    facts (layout, codec strategy, name/DLC/endian). Whitespace/comments in YAML
    source files do not affect the hash. NETWORK_HASH additionally folds buses,
    routes, and per-bus instances.
    """
    normalized = normalized_contract(validated)
    wire_view = {
        "messages": [
            {name: message[name] for name in ("canonical_key", "protocol", "name", "dlc", "byte_order", "codec", "layout")}
            for message in normalized["messages"]
        ]
    }
    wire_hash = hashlib.sha256(canonical_json(wire_view).encode()).hexdigest()
    network_view = {"wire": wire_view, "buses": model["network"]["buses"], "routes": model["network"]["routes"], "instances": [message["instances"] for message in normalized["messages"]]}
    return wire_hash, hashlib.sha256(canonical_json(network_view).encode()).hexdigest()


def render_outputs(model: dict, validated: dict) -> dict[str, str]:
    wire_hash, network_hash = hashes(model, validated)
    # Semantic hash is the wire-content hash (stable across source formatting).
    semantic_hash = wire_hash
    normalized = normalized_contract(validated)
    discovery = {
        "schema_version": 1,
        "wire_hash": wire_hash,
        "semantic_hash": semantic_hash,
        "network_hash": network_hash,
        "messages": normalized["messages"],
        "routes": model["network"]["routes"],
    }
    capabilities = {
        "schema_version": 1,
        "wire_hash": wire_hash,
        "semantic_hash": semantic_hash,
        "network_hash": network_hash,
        "languages": {},
    }
    for language in ("cpp", "python", "typescript"):
        capabilities["languages"][language] = [
            {
                "message": key,
                "strategy": message["codec"]["strategy"],
                "implementation": message["codec"].get("implementation_id") or message["codec"].get("profile_id") or "generated-v1",
                "payload": (
                    "typed" if language in {"cpp", "python"} and message["codec"]["strategy"] == "generated"
                    else "raw_unsupported" if message["layout"].get("semantic_support") == "raw_only"
                    else "raw_compatibility" if language in {"python", "typescript"}
                    else "metadata_only"
                ),
                "semantic_decode": language in {"cpp", "python"} and message["codec"]["strategy"] == "generated",
            }
            for key, message in sorted(validated["messages"].items())
        ]
    errors = {
        "schema_version": 1,
        "statuses": ["ok", "wrong_message_id", "wrong_frame_format", "unexpected_length", "value_out_of_range", "invalid_enum", "constant_mismatch", "checksum_mismatch", "unsupported_semantics"],
        "unsupported": [{"message": "ses:ses_version", "status": "unsupported_semantics", "raw_access": True, "reason": "Vendor version semantics are unresolved."}],
    }
    schema = _render_schema()
    metadata = {key: _runtime_message(message) for key, message in sorted(validated["messages"].items())}
    return {
        "discovery.json": canonical_json(discovery, pretty=True),
        "capabilities.json": canonical_json(capabilities, pretty=True),
        "errors.json": canonical_json(errors, pretty=True),
        "contract-schema.json": canonical_json(schema, pretty=True),
        "python/etrike_protocol.py": _render_python(metadata, semantic_hash, network_hash),
        "typescript/etrike-protocol.ts": _render_typescript(metadata, semantic_hash, network_hash),
        "cpp/etrike_protocol.hpp": _render_cpp(validated, semantic_hash, network_hash),
    }


def _runtime_message(message: dict) -> dict:
    return {
        "name": message["name"], "dlc": message["dlc"], "byte_order": message["byte_order"],
        "codec": message["codec"], "layout": message["layout"],
        "instances": [{**instance, "id": parse_id(instance["id"])} for instance in message["instances"]],
    }


def _render_schema() -> dict:
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": "https://etrike.local/protocol/contract-schema-v1.json",
        "title": "E-Trike canonical message contract",
        "type": "object",
        "required": ["schema_version", "messages"],
        "properties": {
            "schema_version": {"const": 1},
            "messages": {"type": "array", "items": {"$ref": "#/$defs/message"}},
        },
        "$defs": {
            "message": {"type": "object", "required": ["key", "owner", "protocol", "name", "dlc", "byte_order", "codec", "layout", "instances"]},
            "codec": {"type": "object", "properties": {"strategy": {"enum": sorted(STRATEGIES)}}, "required": ["strategy"]},
            "instance": {"type": "object", "required": ["bus", "id", "frame_format", "sender", "receivers", "cycle_ms", "semantics"]},
        },
    }


def _render_python(metadata: dict, semantic_hash: str, network_hash: str) -> str:
    embedded = repr(metadata)
    return f'''# Generated by protocol.tools.protocol. Do not edit.\nfrom __future__ import annotations\n\n# SEMANTIC_HASH / WIRE_HASH: content hash of canonical wire facts (layout/codec).\n# NETWORK_HASH: also includes buses, routes, and bus instances.\nSEMANTIC_HASH = {semantic_hash!r}\nWIRE_HASH = SEMANTIC_HASH\nNETWORK_HASH = {network_hash!r}\nMETADATA = {embedded}\n\ndef _raw(field, value):\n    factor = field.get("factor", 1)\n    offset = field.get("offset", 0)\n    return int(round((value - offset) / factor))\n\ndef _limits(field):\n    bits = field["bits"]\n    if field.get("signed"):\n        defaults = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1)\n    else:\n        defaults = (0, (1 << bits) - 1)\n    factor, offset = field.get("factor", 1), field.get("offset", 0)\n    return field.get("min", defaults[0] * factor + offset), field.get("max", defaults[1] * factor + offset)\n\ndef _instance(message, bus):\n    matches = [item for item in message["instances"] if bus is None or item["bus"] == bus]\n    return matches[0] if len(matches) == 1 else None\n\ndef encode(key, values, *, bus=None, frame_format=None):\n    message = METADATA.get(key)\n    if message is None:\n        return "wrong_message_id", b""\n    instance = _instance(message, bus)\n    if instance is None:\n        return "wrong_message_id", b""\n    if frame_format is not None and frame_format != instance["frame_format"]:\n        return "wrong_frame_format", b""\n    if message["codec"]["strategy"] != "generated":\n        raw = values.get("raw")\n        if isinstance(raw, str):\n            raw = bytes.fromhex(raw)\n        if not isinstance(raw, (bytes, bytearray)) or len(raw) != message["dlc"]:\n            return "unexpected_length", b""\n        return ("unsupported_semantics" if message["layout"].get("semantic_support") == "raw_only" else "ok"), bytes(raw)\n    payload = bytearray(message["dlc"])\n    for field in message["layout"]["fields"]:\n        value = field.get("constant", values.get(field["key"]))\n        if value is None:\n            return "value_out_of_range", b""\n        minimum, maximum = _limits(field)\n        if value < minimum or value > maximum:\n            return "value_out_of_range", b""\n        raw = _raw(field, value)\n        if field.get("signed") and raw < 0:\n            raw += 1 << field["bits"]\n        start, bits = field["byte"] * 8 + field["bit"], field["bits"]\n        if field["bit"] == 0 and bits % 8 == 0:\n            width = bits // 8\n            payload[field["byte"]:field["byte"] + width] = raw.to_bytes(width, message["byte_order"], signed=False)\n        else:\n            for offset in range(bits):\n                position = start + offset\n                payload[position // 8] |= ((raw >> offset) & 1) << (position % 8)\n    return "ok", bytes(payload)\n\ndef decode(key, payload, *, bus=None, frame_format=None):\n    message = METADATA.get(key)\n    if message is None:\n        return "wrong_message_id", None\n    instance = _instance(message, bus)\n    if instance is None:\n        return "wrong_message_id", None\n    if frame_format is not None and frame_format != instance["frame_format"]:\n        return "wrong_frame_format", None\n    if len(payload) != message["dlc"]:\n        return "unexpected_length", None\n    if message["codec"]["strategy"] != "generated":\n        implementation = message["codec"].get("implementation_id", "")\n        if ("command-v1" in implementation or "status-v1" in implementation):\n            checksum = 0\n            for value in payload[:-1]: checksum ^= value\n            if (checksum ^ 0xFF) != payload[-1]:\n                return "checksum_mismatch", None\n        if message["layout"].get("semantic_support") == "raw_only":\n            return "unsupported_semantics", {{"raw": bytes(payload)}}\n        return "ok", {{"raw": bytes(payload)}}\n    values = {{}}\n    for field in message["layout"]["fields"]:\n        bits = field["bits"]\n        if field["bit"] == 0 and bits % 8 == 0:\n            width = bits // 8\n            raw = int.from_bytes(payload[field["byte"]:field["byte"] + width], message["byte_order"], signed=False)\n        else:\n            raw = 0\n            start = field["byte"] * 8 + field["bit"]\n            for offset in range(bits):\n                position = start + offset\n                raw |= ((payload[position // 8] >> (position % 8)) & 1) << offset\n        if field.get("signed") and raw & (1 << (bits - 1)):\n            raw -= 1 << bits\n        value = raw * field.get("factor", 1) + field.get("offset", 0)\n        minimum, maximum = _limits(field)\n        if value < minimum or value > maximum:\n            return "value_out_of_range", None\n        if "constant" in field and value != field["constant"]:\n            return "constant_mismatch", None\n        values[field["key"]] = value\n    return "ok", values\n\ndef decode_into(key, payload, output, *, bus=None, frame_format=None):\n    status, value = decode(key, payload, bus=bus, frame_format=frame_format)\n    if status == "ok":\n        output.clear(); output.update(value)\n    return status\n'''


def _render_typescript(metadata: dict, semantic_hash: str, network_hash: str) -> str:
    embedded = canonical_json(metadata, pretty=True)
    return f'''// Generated by protocol.tools.protocol. Do not edit.\n// SEMANTIC_HASH / WIRE_HASH: content hash of canonical wire facts.\n// NETWORK_HASH: buses + routes + instances.\nexport const SEMANTIC_HASH = {json.dumps(semantic_hash)} as const;\nexport const WIRE_HASH = SEMANTIC_HASH;\nexport const NETWORK_HASH = {json.dumps(network_hash)} as const;\nexport const METADATA = {embedded.strip()} as const;\nexport type CodecStatus = "ok" | "wrong_message_id" | "wrong_frame_format" | "unexpected_length" | "value_out_of_range" | "constant_mismatch" | "checksum_mismatch" | "unsupported_semantics";\nexport function lookup(bus: string, id: number) {{\n  return Object.entries(METADATA).flatMap(([key, message]) => message.instances.filter(instance => instance.bus === bus && instance.id === id).map(instance => ({{ key, message, instance }})))[0];\n}}\n// Custom vendor payloads are intentionally raw compatibility artifacts until Stage 5.\nexport function decodeRaw(key: keyof typeof METADATA, payload: Uint8Array): [CodecStatus, Uint8Array?] {{\n  const message = METADATA[key];\n  if (payload.length !== message.dlc) return ["unexpected_length"];\n  if (message.layout.kind === "opaque" && "semantic_support" in message.layout) return ["unsupported_semantics", payload.slice()];\n  return ["ok", payload.slice()];\n}}\n'''


def _cpp_identifier(value: str) -> str:
    return re.sub(r"_+", "_", re.sub(r"[^A-Za-z0-9_]", "_", value)).strip("_").lower()


def _cpp_pascal(value: str) -> str:
    return "".join(part[:1].upper() + part[1:].lower() for part in re.split(r"[^A-Za-z0-9]+", value) if part)


def _cpp_integer_type(field: dict) -> str:
    width = 8 if field["bits"] <= 8 else 16 if field["bits"] <= 16 else 32 if field["bits"] <= 32 else 64
    return f"std::{'int' if field.get('signed') else 'uint'}{width}_t"


def _cpp_type(field: dict) -> str:
    if "factor" in field or "offset" in field:
        return "double"
    minimum, maximum = _field_limits(field)
    if field["bits"] == 1 or (field["bits"] <= 8 and minimum == 0 and maximum == 1):
        return "bool"
    return _cpp_integer_type(field)


def _cpp_number(value: object, *, floating: bool = False) -> str:
    if floating:
        number = repr(float(value))
        return number if any(character in number for character in ".eE") else number + ".0"
    return str(int(value))


def _cpp_range_check(expression: str, field: dict, *, indent: str) -> list[str]:
    minimum, maximum = _field_limits(field)
    cpp_type = _cpp_type(field)
    if cpp_type == "bool":
        return []
    checks = []
    if cpp_type == "double":
        checks.append(f"!std::isfinite({expression})")
        checks.append(f"{expression} < {_cpp_number(minimum, floating=True)}")
        checks.append(f"{expression} > {_cpp_number(maximum, floating=True)}")
    else:
        raw_minimum = -(1 << (field["bits"] - 1)) if field.get("signed") else 0
        raw_maximum = (1 << (field["bits"] - (1 if field.get("signed") else 0))) - 1
        if minimum > raw_minimum:
            checks.append(f"{expression} < {_cpp_number(minimum)}")
        if maximum < raw_maximum:
            checks.append(f"{expression} > {_cpp_number(maximum)}")
    return [f"{indent}if ({' || '.join(checks)}) return CodecStatus::ValueOutOfRange;"] if checks else []


def _cpp_enum_check(expression: str, field: dict, *, indent: str) -> list[str]:
    enum_dict = field.get("enum", {})
    if not enum_dict:
        return []
    bits = field["bits"]
    if field.get("signed"):
        raw_minimum, raw_maximum = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    else:
        raw_minimum, raw_maximum = 0, (1 << bits) - 1
    factor = field.get("factor", 1)
    offset = field.get("offset", 0)
    min_val = field.get("min", raw_minimum * factor + offset)
    max_val = field.get("max", raw_maximum * factor + offset)
    raw_min_val = int(round((min_val - offset) / factor))
    raw_max_val = int(round((max_val - offset) / factor))
    range_val = raw_max_val - raw_min_val + 1
    if len(enum_dict) < range_val:
        return []
    values = sorted((int(value) for value in enum_dict), key=int)
    conditions = " && ".join(f"{expression} != {_cpp_number(value, floating=_cpp_type(field) == 'double')}" for value in values)
    return [f"{indent}if ({conditions}) return CodecStatus::InvalidEnum;"]


def _render_cpp_message(message: dict) -> list[str]:
    type_name = _cpp_pascal(message["name"])
    function_name = _cpp_identifier(message["key"])
    fields = message["layout"]["fields"]
    instances = sorted(message["instances"], key=lambda value: (value["bus"], parse_id(value["id"])))
    primary = instances[0]
    lines = [f"struct {type_name} {{",
             f'    static constexpr std::string_view kKey = "{message["owner"]}:{message["key"]}";',
             f"    static constexpr std::uint32_t kId = 0x{parse_id(primary['id']):X}u;",
             f"    static constexpr std::size_t kDlc = {message['dlc']}u;",
             f"    static constexpr std::uint32_t kCycleMs = {primary['cycle_ms']}u;",
             f"    static constexpr bool kExtended = {str(primary['frame_format'] == 'extended').lower()};"]
    for instance in instances:
        instance_name = _cpp_pascal(instance["bus"])
        lines += [f"    static constexpr std::uint32_t k{instance_name}Id = 0x{parse_id(instance['id']):X}u;",
                  f"    static constexpr std::uint32_t k{instance_name}CycleMs = {instance['cycle_ms']}u;",
                  f"    static constexpr bool k{instance_name}Extended = {str(instance['frame_format'] == 'extended').lower()};"]
    for field in fields:
        member = _cpp_identifier(field["key"])
        initializer = _cpp_number(field["constant"], floating=_cpp_type(field) == "double") if "constant" in field else ""
        lines.append(f"    {_cpp_type(field)} {member}{{{initializer}}};")
    for field in fields:
        member_pascal = _cpp_pascal(field["key"])
        cpp_type = _cpp_type(field)
        if "constant" in field:
            lines.append(f"    static constexpr {cpp_type} k{member_pascal} = {_cpp_number(field['constant'], floating=cpp_type == 'double')};")
        for raw_value, label in sorted(field.get("enum", {}).items(), key=lambda item: int(item[0])):
            lines.append(f"    static constexpr {cpp_type} k{member_pascal}{_cpp_pascal(label)} = {_cpp_number(raw_value, floating=cpp_type == 'double')};")
        mask = (1 << field["bits"]) - 1
        lines += [f"    struct {member_pascal}Meta {{",
                  f"        static constexpr std::size_t kByte = {field['byte']}u;",
                  f"        static constexpr std::uint8_t kBitOffset = {field['bit']}u;",
                  f"        static constexpr std::uint8_t kWidth = {field['bits']}u;",
                  f"        static constexpr std::uint64_t kMask = 0x{mask:X}ull;",
                  "    };"]

    lines += ["", "    CodecStatus pack(std::uint8_t* destination, std::size_t length) const noexcept {",
              "        if (length != kDlc) return CodecStatus::UnexpectedLength;",
              "        if (destination == nullptr && kDlc != 0u) return CodecStatus::NullData;"]
    for field in fields:
        member = _cpp_identifier(field["key"])
        if "constant" in field:
            constant = _cpp_number(field["constant"], floating=_cpp_type(field) == "double")
            lines.append(f"        if ({member} != {constant}) return CodecStatus::ConstantMismatch;")
        lines += _cpp_range_check(member, field, indent="        ")
        lines += _cpp_enum_check(member, field, indent="        ")
    lines.append("        std::array<std::uint8_t, kDlc> payload{};")
    for field in fields:
        member = _cpp_identifier(field["key"])
        if "factor" in field or "offset" in field:
            factor, offset = field.get("factor", 1), field.get("offset", 0)
            lines.append(f"        const auto raw_{member} = static_cast<std::int64_t>(std::llround(({member} - {_cpp_number(offset, floating=True)}) / {_cpp_number(factor, floating=True)}));")
            raw = f"static_cast<std::uint64_t>(raw_{member})"
        else:
            raw = f"static_cast<std::uint64_t>({member})"
        lines.append(f"        detail::insert(payload.data(), {field['byte']}u, {field['bit']}u, {field['bits']}u, {str(message['byte_order'] == 'little').lower()}, {raw});")
    lines += ["        for (std::size_t index = 0; index < kDlc; ++index) destination[index] = payload[index];",
              "        return CodecStatus::Ok;", "    }", "",
              f"    static CodecStatus unpack(const std::uint8_t* source, std::size_t length, {type_name}& out) noexcept {{",
              "        if (length != kDlc) return CodecStatus::UnexpectedLength;",
              "        if (source == nullptr && kDlc != 0u) return CodecStatus::NullData;",
              f"        {type_name} value{{}};"]
    for field in fields:
        member = _cpp_identifier(field["key"])
        raw = f"detail::extract(source, {field['byte']}u, {field['bit']}u, {field['bits']}u, {str(message['byte_order'] == 'little').lower()})"
        lines.append(f"        const std::uint64_t raw_{member} = {raw};")
        if "constant" in field:
            lines.append(f"        if (raw_{member} != {_cpp_number(field['constant'])}u) return CodecStatus::ConstantMismatch;")
        if _cpp_type(field) == "bool" and field["bits"] > 1:
            lines.append(f"        if (raw_{member} > 1u) return CodecStatus::ValueOutOfRange;")
        if "factor" in field or "offset" in field:
            raw_value = f"detail::sign_extend(raw_{member}, {field['bits']}u)" if field.get("signed") else f"raw_{member}"
            factor, offset = field.get("factor", 1), field.get("offset", 0)
            expression = f"static_cast<double>({raw_value}) * {_cpp_number(factor, floating=True)} + {_cpp_number(offset, floating=True)}"
        elif _cpp_type(field) == "bool":
            expression = f"raw_{member} != 0u"
        elif field.get("signed"):
            expression = f"static_cast<{_cpp_type(field)}>(detail::sign_extend(raw_{member}, {field['bits']}u))"
        else:
            expression = f"static_cast<{_cpp_type(field)}>(raw_{member})"
        lines.append(f"        value.{member} = {expression};")
        lines += _cpp_range_check(f"value.{member}", field, indent="        ")
        lines += _cpp_enum_check(f"value.{member}", field, indent="        ")
    lines += ["        out = value;", "        return CodecStatus::Ok;", "    }", "};", "",
              f"inline CodecStatus encode_{function_name}(const {type_name}& value, Frame& out) noexcept {{",
              f"    Frame frame = {'Frame::extended_frame' if primary['frame_format'] == 'extended' else 'Frame::standard'}({type_name}::kId, static_cast<std::uint8_t>({type_name}::kDlc));",
              f"    const CodecStatus status = value.pack(frame.data.data(), {type_name}::kDlc);",
              "    if (status != CodecStatus::Ok) return status;", "    out = frame;", "    return CodecStatus::Ok;", "}", "",
              f"inline CodecStatus decode_{function_name}(FrameView frame, {type_name}& out) noexcept {{"]
    identities = sorted({(parse_id(instance["id"]), instance["frame_format"] == "extended") for instance in instances})
    ids = sorted({can_id for can_id, _ in identities})
    id_condition = " && ".join(f"frame.id() != 0x{can_id:X}u" for can_id in ids)
    format_condition = " && ".join(f"!(frame.id() == 0x{can_id:X}u && frame.extended() == {str(extended).lower()})" for can_id, extended in identities)
    lines += [f"    if ({id_condition}) return CodecStatus::WrongMessageId;",
              f"    if ({format_condition}) return CodecStatus::WrongFrameFormat;",
              f"    if (frame.dlc() != {type_name}::kDlc) return CodecStatus::UnexpectedLength;",
              f"    return {type_name}::unpack(frame.data(), frame.dlc(), out);", "}", "",
              f"inline CodecStatus encode(const {type_name}& value, Frame& out) noexcept {{ return encode_{function_name}(value, out); }}",
              f"inline CodecStatus decode(FrameView frame, {type_name}& out) noexcept {{ return decode_{function_name}(frame, out); }}", ""]
    return lines


def _render_cpp(validated: dict, semantic_hash: str, network_hash: str) -> str:
    rows = []
    for key, message in sorted(validated["messages"].items()):
        strategy = {"generated": "Generated", "profile": "Profile", "custom": "Custom"}[message["codec"]["strategy"]]
        for instance in sorted(message["instances"], key=lambda value: (value["bus"], parse_id(value["id"]))):
            rows.append(f'    {{"{key}", "{instance["bus"]}", 0x{parse_id(instance["id"]):X}u, {message["dlc"]}u, {str(instance["frame_format"] == "extended").lower()}, CodecStrategy::{strategy}}},')
    route_rows = [
        f'    {{"{route["key"]}", "{route["message"]}", "{route["from"]}", "{route["to"]}", RouteSemantics::{"SameFrame" if route["semantics"] == "same_frame" else "Regenerated"}}},'
        for route in validated["network"]["routes"]
    ]
    lines = ["// Generated by protocol.tools.protocol. Do not edit.", "#pragma once", "#include <array>",
             "#include <cmath>", "#include <cstddef>", "#include <cstdint>", "#include <string_view>", "",
             '#include "protocol/core/codec_status.hpp"', '#include "protocol/core/frame.hpp"', "",
             "namespace etrike::protocol {",
             f'inline constexpr std::string_view kSemanticHash = "{semantic_hash}";',
             f'inline constexpr std::string_view kWireHash = kSemanticHash;',
             f'inline constexpr std::string_view kNetworkHash = "{network_hash}";',
             "enum class CodecStrategy : std::uint8_t { Generated, Profile, Custom };",
             "enum class RouteSemantics : std::uint8_t { SameFrame, Regenerated };",
             "struct MessageMetadata { std::string_view key; std::string_view bus; std::uint32_t id; std::uint8_t dlc; bool extended; CodecStrategy strategy; };",
             "struct RouteMetadata { std::string_view key; std::string_view message; std::string_view from_bus; std::string_view to_bus; RouteSemantics semantics; };",
             f"inline constexpr std::array<MessageMetadata, {len(rows)}> kMessages{{{{", *rows, "}};",
             f"inline constexpr std::array<RouteMetadata, {len(route_rows)}> kRoutes{{{{", *route_rows, "}};", "",
             "namespace generated {", "using ::etrike::protocol::CodecStatus;", "using ::etrike::protocol::Frame;", "using ::etrike::protocol::FrameView;", "",
             "namespace detail {",
             "inline std::uint64_t extract(const std::uint8_t* source, std::size_t byte, std::uint8_t bit, std::uint8_t width, bool little) noexcept {",
             "    if (little) {", "        std::uint64_t value = 0;", "        for (std::uint8_t index = 0; index < width; ++index) {",
             "            const std::size_t absolute = byte * 8u + bit + index;",
             "            value |= std::uint64_t((source[absolute / 8u] >> (absolute % 8u)) & 1u) << index;", "        }", "        return value;", "    }",
             "    if (bit + width <= 8u) {", "        const std::uint64_t mask = width == 64u ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1u;",
             "        return (source[byte] >> bit) & mask;", "    }", "    std::uint64_t value = 0;",
             "    for (std::size_t index = 0; index < width / 8u; ++index) value = (value << 8u) | source[byte + index];", "    return value;", "}", "",
             "inline void insert(std::uint8_t* destination, std::size_t byte, std::uint8_t bit, std::uint8_t width, bool little, std::uint64_t value) noexcept {",
             "    if (little) {", "        for (std::uint8_t index = 0; index < width; ++index) {",
             "            const std::size_t absolute = byte * 8u + bit + index;", "            const std::uint8_t mask = static_cast<std::uint8_t>(1u << (absolute % 8u));",
             "            destination[absolute / 8u] = static_cast<std::uint8_t>((destination[absolute / 8u] & ~mask) | (((value >> index) & 1u) != 0u ? mask : 0u));", "        }", "        return;", "    }",
             "    if (bit + width <= 8u) {", "        const std::uint64_t value_mask = (std::uint64_t{1} << width) - 1u;",
             "        const std::uint8_t mask = static_cast<std::uint8_t>(value_mask << bit);",
             "        destination[byte] = static_cast<std::uint8_t>((destination[byte] & ~mask) | ((value << bit) & mask));", "        return;", "    }",
             "    const std::size_t count = width / 8u;", "    for (std::size_t index = 0; index < count; ++index) destination[byte + count - 1u - index] = static_cast<std::uint8_t>(value >> (index * 8u));", "}", "",
             "inline std::int64_t sign_extend(std::uint64_t value, std::uint8_t width) noexcept {", "    if (width == 64u) return static_cast<std::int64_t>(value);",
             "    const std::uint64_t sign = std::uint64_t{1} << (width - 1u);", "    return static_cast<std::int64_t>((value ^ sign) - sign);", "}",
             "}  // namespace detail", ""]
    for _, message in sorted(validated["messages"].items()):
        if message["codec"]["strategy"] == "generated":
            lines += _render_cpp_message(message)
    lines += ["}  // namespace generated", "}  // namespace etrike::protocol", "",
              "namespace can {", "namespace generated = ::etrike::protocol::generated;", "}  // namespace can", ""]
    return "\n".join(lines)


def write_or_check(outputs: dict[str, str], *, check: bool) -> list[str]:
    changed = []
    for relative, content in sorted(outputs.items()):
        path = GENERATED / relative
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current != content:
            changed.append(relative)
            if not check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8", newline="\n")
    return changed


def inspect_message(model: dict, validated: dict, id_text: str, bus: str | None) -> dict:
    can_id = parse_id(id_text)
    matches = [
        (key, message, instance) for (instance_bus, instance_id), (key, message, instance) in validated["instances"].items()
        if instance_id == can_id and (bus is None or bus == instance_bus)
    ]
    if not matches:
        raise ContractError(f"no message for {bus + ':' if bus else ''}0x{can_id:X}")
    if len(matches) > 1 and bus is None:
        buses = ", ".join(sorted(instance["bus"] for _, _, instance in matches))
        raise ContractError(f"0x{can_id:X} is ambiguous on buses {buses}; --bus is required")
    key, message, instance = matches[0]
    return {"canonical_key": key, "name": message["name"], "codec": message["codec"], "layout": message["layout"], "instance": instance}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate")
    validate.add_argument("--no-baseline", action="store_true", help=argparse.SUPPRESS)
    generate = subparsers.add_parser("generate")
    generate.add_argument(
        "target",
        nargs="?",
        default="all",
        choices=["all", "python", "typescript", "cpp", "manifests"],
        help="artifact family to generate or check (default: all)",
    )
    generate.add_argument("--check", action="store_true", help="read-only verification; fail if output differs")
    inspect = subparsers.add_parser("inspect")
    inspect.add_argument("id")
    inspect.add_argument("--bus", choices=["high", "low", "powertrain"])
    return parser


_TARGET_PREFIXES = {
    "python": ("python/",),
    "typescript": ("typescript/",),
    "cpp": ("cpp/",),
    "manifests": ("discovery.json", "capabilities.json", "errors.json", "contract-schema.json"),
    "all": (),
}


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        model = load_model()
        validated = validate_model(model, check_baseline=not getattr(args, "no_baseline", False))
        if args.command == "validate":
            wire_hash, network_hash = hashes(model, validated)
            print(
                f"valid: {len(validated['messages'])} messages, "
                f"{len(validated['instances'])} instances, "
                f"SEMANTIC_HASH={wire_hash}, NETWORK_HASH={network_hash}"
            )
        elif args.command == "generate":
            semantic_hash, network_hash = hashes(model, validated)
            outputs = render_outputs(model, validated)
            prefixes = _TARGET_PREFIXES[args.target]
            if prefixes:
                outputs = {
                    relative: content
                    for relative, content in outputs.items()
                    if any(
                        relative == prefix or relative.startswith(prefix)
                        for prefix in prefixes
                    )
                }
            changed = write_or_check(outputs, check=args.check)
            if args.check and changed:
                raise ContractError("generated output differs: " + ", ".join(changed))
            print("generated output is current" if not changed else "generated: " + ", ".join(changed))
            print(f"SEMANTIC_HASH={semantic_hash}")
            print(f"NETWORK_HASH={network_hash}")
        else:
            print(canonical_json(inspect_message(model, validated, args.id, args.bus), pretty=True), end="")
    except ContractError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
