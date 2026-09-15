import sys
import json
import requests

sys.stdout.reconfigure(encoding='utf-8')

r = requests.get('http://127.0.0.1:8001/api/v1/events')
events = r.json().get('events', [])
events.sort(key=lambda e: e.get('first_wall', 0))

print("=== FIRST 25 EVENTS ===")
for e in events[:25]:
    t = e.get('first_wall', 0)
    sev = e.get('severity', '')
    code = e.get('code', '')
    title = e.get('title', '')
    detail = e.get('detail', '')
    print(f"[{t:.2f}] [{sev.upper():8s}] {code:25s} | {title} | {detail}")

print("\n=== ALL SAFETY EVENTS ===")
for e in events:
    if 'safety' in e.get('code', ''):
        t = e.get('first_wall', 0)
        sev = e.get('severity', '')
        code = e.get('code', '')
        title = e.get('title', '')
        detail = e.get('detail', '')
        evidence = e.get('evidence', '')
        print(f"[{t:.2f}] [{sev.upper():8s}] {code:25s} | {title} | detail: {detail} | evidence: {evidence}")
