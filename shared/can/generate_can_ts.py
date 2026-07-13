#!/usr/bin/env python3
"""Generate can-metadata.ts from can_high.yaml + can_low.yaml.
This replaces the runtime YAML parsing in dynamic-decoder.ts.
"""

import sys
import os
import json
from can_signals_schema import load_can_database_dir, semantic_protocol_hash

try:
    import yaml
except ImportError:
    print("Install pyyaml: pip install pyyaml")
    sys.exit(1)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
OUTPUT = os.path.join(REPO_ROOT, "debug-tool", "shared", "src", "generated", "can-metadata.ts")
YAML_FILES = [
    os.path.join(SCRIPT_DIR, "can_high.yaml"),
    os.path.join(SCRIPT_DIR, "can_low.yaml"),
]

HEADER = """/**
 * CAN message metadata — auto-generated from shared/can/can_high.yaml + can_low.yaml.
 * DO NOT EDIT BY HAND. Regenerate: python shared/can/generate_can_ts.py
 */

import type { CanMessageDef, CanField, Bus, FieldKind } from "../can";

export const PROTOCOL_HASH = "{hash}";

export interface InternalCanField extends CanField {
  _byte: number;
  _bit_offset: number;
  _size: number;
  _type: "signed" | "unsigned";
  _factor: number;
  _offset: number;
  multiplexed?: boolean;
}

export interface InternalCanMessageDef extends CanMessageDef {
  byteOrder: "motorola" | "intel";
  fields: InternalCanField[];
}
"""

def fmt_id(v):
    if isinstance(v, str):
        return v if v.startswith("0x") else f"0x{int(v,16):03X}"
    return f"0x{int(v):03X}"

def parse_signal(sig):
    raw_options = sig.get("values", sig.get("options"))
    has_enum = (sig.get("unit") == "enum") or (raw_options and len(raw_options) > 0)
    
    size = sig.get("size", 1)
    min_val = sig.get("min")
    max_val = sig.get("max")
    factor = sig.get("factor")
    offset = sig.get("offset")
    
    is_boolean = (not has_enum) and (
        size == 1 or (min_val == 0 and max_val == 1 and not factor and not offset)
    )
    
    kind = "enum" if has_enum else ("boolean" if is_boolean else "number")
    
    options = None
    if raw_options:
        options = []
        for k, v in raw_options.items():
            options.append({"value": int(k), "label": str(v)})
    
    # Internal fields needed by encoder/decoder
    _byte = sig.get("byte", 0)
    _bit_offset = sig.get("bit_offset", 0)
    _type = sig.get("type", "unsigned")
    _factor = sig.get("factor", 1.0)
    _offset = sig.get("offset", 0.0)
    
    res = {
        "key": sig.get("key", sig.get("name")),
        "label": sig.get("name"),
        "kind": kind,
        "_byte": _byte,
        "_bit_offset": _bit_offset,
        "_size": size,
        "_type": _type,
        "_factor": _factor,
        "_offset": _offset,
    }
    
    if "multiplexed" in sig:
        res["multiplexed"] = sig["multiplexed"]
    
    if "unit" in sig:
        res["unit"] = str(sig["unit"])
    if min_val is not None:
        res["min"] = min_val
    if max_val is not None:
        res["max"] = max_val
    if options is not None:
        res["options"] = options
        
    return res

def msg_ts(msg, bus, byte_order):
    id_str = fmt_id(msg["id"])
    fields = [parse_signal(s) for s in msg.get("signals", [])]
    
    res = {
        "bus": bus,
        "id": id_str,
        "name": msg.get("name"),
        "sender": msg.get("sender", "Unknown"),
        "receivers": msg.get("receivers", []),
        "comment": msg.get("comment", ""),
        "dlc": msg.get("dlc", 8),
        "period": str(msg.get("cycle_ms", 0)) + "ms",
        "injectable": msg.get("sender") in ["Host", "Any"],
        "byteOrder": byte_order,
        "fields": fields,
    }
    return res

def parse_yaml(fp):
    with open(fp, 'r') as f:
        doc = yaml.safe_load(f)
    out = []
    for proto in doc.get("protocols", {}).values():
        bus = proto.get("bus", "low")
        byte_order = proto.get("byte_order", "motorola")
        for msg in proto.get("messages", []):
            out.append((msg, bus, byte_order))
    return out

def main():
    messages = []
    seen = set()
    
    for yf in YAML_FILES:
        for msg, bus, byte_order in parse_yaml(yf):
            key = f"{bus}:{fmt_id(msg['id'])}"
            if key in seen:
                print(f"Duplicate CAN message definition: {key}", file=sys.stderr)
                sys.exit(1)
            seen.add(key)
            messages.append(msg_ts(msg, bus, byte_order))
            
    # Sort for determinism
    messages.sort(key=lambda x: f"{x['bus']}:{x['id']}")

    protocol_hash = semantic_protocol_hash(load_can_database_dir(SCRIPT_DIR))

    out = HEADER.replace("{hash}", protocol_hash) + "\n"
    
    # Generate ID and SIG constants
    emitted_ids = set()
    emitted_sigs = set()
    for m in messages:
        var_name = f"ID_{m['name']}"
        if var_name not in emitted_ids:
            out += f"export const {var_name} = \"{m['id']}\";\n"
            emitted_ids.add(var_name)
        
        for f in m['fields']:
            sig_name = f"SIG_{m['name']}_{f['key'].upper()}"
            if sig_name not in emitted_sigs:
                out += f"export const {sig_name} = \"{f['key']}\";\n"
                emitted_sigs.add(sig_name)
        
    out += "\nexport const CAN_MESSAGES: InternalCanMessageDef[] = "
    out += json.dumps(messages, indent=2) + ";\n"

    if "--check" in sys.argv:
        try:
            with open(OUTPUT, 'r') as f:
                existing = f.read()
            if existing != out:
                print("can-metadata.ts is out of date. Run: python shared/can/generate_can_ts.py")
                sys.exit(1)
            print("can-metadata.ts is up to date.")
            return
        except FileNotFoundError:
            print("can-metadata.ts is missing. Run: python shared/can/generate_can_ts.py")
            sys.exit(1)

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, 'w') as f:
        f.write(out)
    print(f"Generated {OUTPUT} ({len(messages)} messages)")

if __name__ == "__main__":
    main()
