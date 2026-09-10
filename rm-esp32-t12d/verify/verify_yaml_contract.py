#!/usr/bin/env python3
"""
Phase 1 — Programmatic protocol compatibility check for rm-esp32-t12d.

For every CAN frame that rm-esp32-t12d emits in each operating mode
(BARE / SYS / RT), this script:

  1. Cross-checks the emission against the ORIGINAL protocol YAML contracts
     (protocol/contracts/*.yaml) — verifying frame id, DLC, byte order,
     codec strategy, bus, the emulated sender role, and the expected receiver.
  2. Round-trips a representative rm command through the canonical generated
     codec (protocol/generated/python/etrike_protocol.py) or the custom
     ses/seb codecs (protocol/codecs/python/) to prove the wire encoding is
     self-consistent and decodable by the consumer.

Run from repo root:  py rm-esp32-t12d/verify/verify_yaml_contract.py
Exit code non-zero on any contract or round-trip failure.
"""
from __future__ import annotations

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO)

from protocol.generated.python.etrike_protocol import encode as gen_encode, decode as gen_decode
from protocol.codecs.python import ses as ses_codec, seb as seb_codec

CONTRACT_DIR = os.path.join(REPO, "protocol", "contracts")

# ---------------------------------------------------------------------------
# Load original YAML (JSON) contracts
# ---------------------------------------------------------------------------
CONTRACTS = {}
for fn in ("ses", "seb", "rt", "sys", "hmi", "host"):
    with open(os.path.join(CONTRACT_DIR, f"{fn}.yaml"), encoding="utf-8") as fh:
        CONTRACTS[fn] = json.load(fh)


def find_instance(key: str, bus: str):
    owner, _, name = key.partition(":")
    msg = next((m for m in CONTRACTS[owner]["messages"] if m["key"] == name), None)
    if msg is None:
        return None, None
    inst = next((i for i in msg["instances"] if i["bus"] == bus), None)
    return msg, inst


# ---------------------------------------------------------------------------
# rm emission table: (mode, frame_name, yaml_key, bus, emulated_sender,
#                     target_receiver, expected_id, sample_values, codec_kind)
# codec_kind: "gen" (generated codec) or "custom" (ses/seb python codec)
# ---------------------------------------------------------------------------
EMISSIONS = [
    # ---- BARE (Low-CAN -> actuators) ----
    ("BARE", "VCU_SES_REQ",   "ses:vcu_ses_req", "low", "RT",  "SES", 0x169,
     {"alignment_enable": True, "control_enable": True, "target_angle_raw": 30000,
      "target_speed_raw": 328, "rolling_counter": 7, "vehicle_speed_raw": 0}, "custom_ses"),
    ("BARE", "VCU_SEB_REQ",   "seb:vcu_seb_req", "low", "SYS", "SEB", 0x7B9,
     {"alignment_enable": True, "control_enable": True, "control_mode": 0,
      "stroke_request_raw": 900, "pressure_request_raw": 0, "rolling_counter": 3}, "custom_seb"),
    ("BARE", "RT_DRIVE_CMD",  "rt:rt_drive_cmd",  "low", "RT",  "MTR", 0x204,
     {"motor_speed_mmps": 2500, "gear": 1}, "gen"),
    ("BARE", "SYS_MODE_CMD",  "sys:sys_mode_cmd", "low", "SYS", "MTR", 0x110,
     {"mode": 1, "rolling_counter": 42}, "gen"),
    ("BARE", "SYS_PWR_CMD",   "sys:sys_pwr_cmd",  "low", "SYS", "MTR", 0x113,
     {"power_state": 1, "rolling_counter": 12}, "gen"),

    # ---- SYS (Low-CAN -> sys-esp32) ----
    ("SYS", "VCU_SES_REQ",    "ses:vcu_ses_req",  "low", "RT",  "SES", 0x169, None, "custom_ses"),
    ("SYS", "VCU_SEB_REQ",    "seb:vcu_seb_req",  "low", "SYS", "SEB", 0x7B9, None, "custom_seb"),
    ("SYS", "RT_DRIVE_CMD",   "rt:rt_drive_cmd",   "low", "RT",  "SYS", 0x204,
     {"motor_speed_mmps": 2500, "gear": 1}, "gen"),
    ("SYS", "HMI_MODE_REQ",   "hmi:hmi_mode_req",  "low", "Host", "SYS", 0x111,
     {"req_mode": 1, "rolling_counter": 5}, "gen"),
    ("SYS", "HMI_PWR_REQ",    "hmi:hmi_pwr_req",   "low", "Host", "SYS", 0x112,
     {"req_start": 1, "rolling_counter": 9}, "gen"),
    ("SYS", "RT_HEARTBEAT",   "rt:rt_heartbeat",   "low", "RT",  "SYS", 0x7FD,
     {"alive_ctr": 4, "health_flags": 0}, "gen"),

    # ---- RT (High-CAN -> rt-esp32) ----
    ("RT", "HOST_STEER_CMD",  "host:host_steer_cmd", "high", "Host", "RT", 0x303,
     {"steer_angle_0_1deg": 200, "angle_valid": 1, "rolling_counter": 11}, "gen"),
    ("RT", "HOST_BRAKE_REQ",  "host:host_brake_req", "high", "Host", "RT", 0x301,
     {"brake_pressure_kpa": 8000}, "gen"),
    ("RT", "HOST_DRIVE_CMD",  "host:host_drive_cmd", "high", "Host", "RT", 0x300,
     {"speed_mmps": 2500, "yaw_rate_mrad_s": 0, "gear": 1}, "gen"),
    ("RT", "HMI_MODE_REQ",    "hmi:hmi_mode_req",   "high", "Host", "RT", 0x111,
     {"req_mode": 1, "rolling_counter": 5}, "gen"),
    ("RT", "HMI_PWR_REQ",     "hmi:hmi_pwr_req",    "high", "Host", "RT", 0x112,
     {"req_start": 1, "rolling_counter": 9}, "gen"),
    ("RT", "HOST_HEARTBEAT",  "host:host_heartbeat", "high", "Host", "RT", 0x7FC,
     {"alive_ctr": 4, "health_flags": 0}, "gen"),
]

# rm cadence (ms) actually emitted, for informational contract comparison
RM_CADENCE_MS = {
    0x169: 10, 0x7B9: 10, 0x204: 10,
    0x110: 100, 0x113: 100,
    0x111: 100, 0x112: 100,
    0x7FD: 500, 0x7FC: 500,
    0x300: 10, 0x301: 0, 0x303: 10,
}

results = []
fails = 0


def record(mode, name, ok, detail):
    global fails
    results.append((mode, name, ok, detail))
    if not ok:
        fails += 1
    print(f"  [{'PASS' if ok else 'FAIL'}] {mode:4} {name:16} {detail}")


for (mode, name, key, bus, sender, receiver, eid, sample, kind) in EMISSIONS:
    msg, inst = find_instance(key, bus)
    if inst is None:
        record(mode, name, False, f"NO CONTRACT INSTANCE bus={bus} key={key}")
        continue

    problems = []
    exp_dlc = 8 if kind.startswith("custom") else msg["dlc"]
    cid = int(inst["id"], 16) if isinstance(inst["id"], str) else inst["id"]
    if cid != eid:
        problems.append(f"id {cid:#04x} != {eid:#04x}")
    if msg["dlc"] != exp_dlc:
        problems.append(f"dlc {msg['dlc']} != expected {exp_dlc}")
    if msg["byte_order"] != ("little" if kind.startswith("custom") else "big"):
        problems.append(f"byte_order {msg['byte_order']}")
    strat = msg["codec"]["strategy"]
    if kind.startswith("custom") and strat != "custom":
        problems.append(f"codec strategy {strat}")
    if inst["sender"] != sender:
        problems.append(f"sender {inst['sender']} != emulated {sender}")
    if receiver not in inst["receivers"]:
        problems.append(f"receiver {inst['receivers']} missing {receiver}")
    yaml_cycle = inst.get("cycle_ms")

    if problems:
        record(mode, name, False, "; ".join(problems))
        continue

    # Round-trip through canonical codec
    if kind == "gen":
        st, payload = gen_encode(key, sample, bus=bus)
        if st != "ok":
            record(mode, name, False, f"encode status {st}")
            continue
        st2, vals = gen_decode(key, payload, bus=bus)
        if st2 != "ok":
            record(mode, name, False, f"decode status {st2}")
            continue
        mism = [k for k in sample if vals.get(k) != sample[k]]
        if mism:
            record(mode, name, False, f"round-trip mismatch {mism}")
            continue
    elif kind == "custom_ses":
        if sample is None:
            record(mode, name, True, f"contract OK (opaque custom codec, bus={bus}, sender={sender})")
            continue
        st, fr = ses_codec.encode_command(sample)
        if st != "ok":
            record(mode, name, False, f"ses encode {st}")
            continue
        st2, vals = ses_codec.decode_command(fr)
        if st2 != "ok":
            record(mode, name, False, f"ses decode {st2}")
            continue
        mism = [k for k in sample if vals.get(k) != sample[k]]
        if mism:
            record(mode, name, False, f"ses round-trip {mism}")
            continue
    elif kind == "custom_seb":
        if sample is None:
            record(mode, name, True, f"contract OK (opaque custom codec, bus={bus}, sender={sender})")
            continue
        st, fr = seb_codec.encode_command(sample)
        if st != "ok":
            record(mode, name, False, f"seb encode {st}")
            continue
        st2, vals = seb_codec.decode_command(fr)
        if st2 != "ok":
            record(mode, name, False, f"seb decode {st2}")
            continue
        mism = [k for k in sample if vals.get(k) != sample[k]]
        if mism:
            record(mode, name, False, f"seb round-trip {mism}")
            continue

    cad_note = ""
    if yaml_cycle and RM_CADENCE_MS.get(eid) is not None and RM_CADENCE_MS[eid] != yaml_cycle:
        cad_note = f"  [CADENCE rm={RM_CADENCE_MS[eid]}ms vs yaml={yaml_cycle}ms]"
    record(mode, name, True, f"contract+bytes OK (id={eid:#04x},dlc={msg['dlc']},bus={bus}){cad_note}")

print("\n" + "=" * 70)
print(f"Phase 1 results: {len(results)-fails} pass, {fails} fail of {len(results)} checks")
if fails:
    sys.exit(1)
print(">>> ALL PHASE 1 CONTRACT CHECKS PASSED <<<")
