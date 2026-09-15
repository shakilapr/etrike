import sys
import json
import requests

sys.stdout.reconfigure(encoding='utf-8')

print("--- FETCHING API LOGS ---")
r = requests.get('http://127.0.0.1:8001/api/v1/logs')
logs = r.json().get('logs', [])
titles = {}
for l in logs:
    t = l.get('title')
    titles[t] = titles.get(t, 0) + 1

for t, count in titles.items():
    sample = next(l for l in logs if l.get('title') == t)
    print(f"[{sample.get('severity').upper():8s}] ({count:2d}x) {t} | message: {sample.get('message')} | detail: {sample.get('detail')}")

print("\n--- FETCHING ALL EVENTS ---")
r = requests.get('http://127.0.0.1:8001/api/v1/events')
events = r.json().get('events', [])
for e in events:
    if e.get('severity') in ('critical', 'error', 'warning'):
        print(f"[{e.get('severity').upper():8s}] {e.get('title')} | detail: {e.get('detail')} | evidence: {e.get('evidence')}")
