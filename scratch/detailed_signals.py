import sys
import json
import requests

sys.stdout.reconfigure(encoding='utf-8')

r = requests.get('http://127.0.0.1:8001/api/v1/state')
state = r.json()

print(f"Total messages in state: {len(state.get('messages', []))}")
for m in state.get('messages', []):
    bus = m.get('bus')
    cid = hex(m.get('can_id'))
    name = m.get('name')
    key = m.get('key')
    freshness = m.get('freshness')
    val_status = m.get('validation_status')
    obs_rate = m.get('observed_rate_hz')
    exp_rate = m.get('expected_rate_hz')
    age_ms = m.get('age_ms')
    
    print(f"\nMESSAGE: [{bus.upper()}] {cid} ({name} - {key})")
    print(f"  Freshness: {freshness} | Age: {age_ms:.1f}ms | Obs Rate: {obs_rate:.2f}Hz | Exp Rate: {exp_rate}Hz | Validation: {val_status}")
    signals = m.get('signals', {})
    for sname, sinfo in signals.items():
        val = sinfo.get('engineering_value')
        raw = sinfo.get('raw_value')
        valid = sinfo.get('valid')
        enum_lbl = sinfo.get('enum_label')
        unit = sinfo.get('unit')
        extra = f" [{enum_lbl}]" if enum_lbl else ""
        extra += f" {unit}" if unit else ""
        valid_str = "VALID" if valid else "INVALID"
        print(f"    - {sname}: eng={val}{extra} (raw={raw}) [{valid_str}]")
