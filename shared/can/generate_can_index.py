#!/usr/bin/env python3
"""Generate can-index.ts from can_high.yaml + can_low.yaml."""

import sys, os, json

try:
    import yaml
except ImportError:
    print("Install pyyaml: pip install pyyaml")
    sys.exit(1)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
OUTPUT = os.path.join(REPO_ROOT, "debug-tool", "ui", "src", "lib", "can-index.ts")
YAML_FILES = [
    os.path.join(SCRIPT_DIR, "can_high.yaml"),
    os.path.join(SCRIPT_DIR, "can_low.yaml"),
]

HEADER = """/**
 * CAN message index — auto-generated from shared/can/can_high.yaml + can_low.yaml.
 * DO NOT EDIT BY HAND. Regenerate: python shared/can/generate_can_index.py
 */

export type Bus = "high" | "low";

export interface CanSignalDef {
  name: string; byte: number; bit_offset: number; size: number;
  type: "signed" | "unsigned"; factor: number; offset: number;
  unit: string; min: number; max: number;
  values: Record<number, string> | null; comment: string;
}

export interface CanMessageIndex {
  bus: Bus; id: string; name: string; dlc: number;
  sender: string; receivers: string[]; cycle_ms: number;
  comment: string; signals: CanSignalDef[];
}
"""

def fmt_id(v):
    if isinstance(v, str):
        return v if v.startswith("0x") else f"0x{int(v,16):03X}"
    return f"0x{int(v):03X}"

def signal_ts(s):
    vals = json.dumps(s.get("values")) if s.get("values") else "null"
    return (f"    {{{json.dumps(s['name'])}, byte:{s['byte']}, bit_offset:{s['bit_offset']}, "
            f"size:{s['size']}, type:{json.dumps(str(s.get('type','unsigned')))}, "
            f"factor:{s.get('factor',1)}, offset:{s.get('offset',0)}, "
            f"unit:{json.dumps(str(s.get('unit','')))}, min:{s.get('min',0)}, max:{s.get('max',0)}, "
            f"values:{vals}, comment:{json.dumps(s.get('comment',''))}}}")

def msg_ts(msg, bus):
    sigs = ",\n".join(signal_ts(s) for s in msg.get("signals", []))
    sig_block = f"\n{sigs}\n  " if sigs else ""
    return (f"  {{bus:{json.dumps(bus)}, id:{json.dumps(fmt_id(msg['id']))}, "
            f"name:{json.dumps(msg['name'])}, dlc:{msg['dlc']}, "
            f"sender:{json.dumps(str(msg.get('sender','Unknown')))}, "
            f"receivers:{json.dumps(msg.get('receivers',[]))}, cycle_ms:{msg.get('cycle_ms',0)}, "
            f"comment:{json.dumps(msg.get('comment',''))}, "
            f"signals:[{sig_block}]}}")

def parse_yaml(fp):
    with open(fp, 'r') as f:
        doc = yaml.safe_load(f)
    out = []
    for proto in doc.get("protocols", {}).values():
        bus = proto.get("bus", "low")
        for msg in proto.get("messages", []):
            out.append((msg, bus))
    return out

def main():
    messages = []
    seen = set()
    for yf in YAML_FILES:
        for msg, bus in parse_yaml(yf):
            key = f"{bus}:{fmt_id(msg['id'])}"
            if key in seen: continue
            seen.add(key)
            messages.append(msg_ts(msg, bus))
    messages.sort()

    out = HEADER + "\nexport const CAN_INDEX: CanMessageIndex[] = [\n"
    out += ",\n".join(messages) + "\n];\n"

    if "--check" in sys.argv:
        with open(OUTPUT, 'r') as f:
            existing = f.read()
        if existing != out:
            print("can-index.ts is out of date. Run: python shared/can/generate_can_index.py")
            sys.exit(1)
        print("can-index.ts is up to date.")
        return

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, 'w') as f:
        f.write(out)
    print(f"Generated {OUTPUT} ({len(messages)} messages)")

if __name__ == "__main__":
    main()
