import urllib.request, json, time

st = json.loads(urllib.request.urlopen("http://127.0.0.1:8001/api/v1/status").read())
print("bench_tx:", st.get("session", {}).get("bench_tx"))
print("profile:", st.get("session", {}).get("profile"))
print("adapter:", st.get("adapter", {}).get("identity"))

req = urllib.request.Request(
    "http://127.0.0.1:8001/api/v1/injections",
    data=json.dumps({
        "bus": "high",
        "key": "host:host_light_cmd",
        "values": {"headlight": 1, "left_turn": 1, "right_turn": 0, "brake_light": 0}
    }).encode(),
    headers={"Content-Type": "application/json"}
)
try:
    resp = json.loads(urllib.request.urlopen(req).read())
    print("Injection resp:", resp)
except urllib.error.HTTPError as e:
    print("Injection HTTPError:", e.code, e.read().decode())

time.sleep(0.5)
st2 = json.loads(urllib.request.urlopen("http://127.0.0.1:8001/api/v1/state").read())
msgs = {f"{m['bus']}:{m['name']}": m for m in st2.get("messages", [])}
print("High 0x302 seen?", "high:HOST_LIGHT_CMD" in msgs)
print("Low 0x302 seen?", "low:HOST_LIGHT_CMD" in msgs)
print("Low 0x011 signals:", msgs.get("low:SYS_SAFETY_STS", {}).get("signals"))
