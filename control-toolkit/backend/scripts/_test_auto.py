import urllib.request, json, time
base = "http://127.0.0.1:8001/api/v1"

def req_api(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    headers = {"Content-Type": "application/json"} if body is not None else {}
    req = urllib.request.Request(base + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            return resp.status, json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode(errors="replace"))

# 1. Setup session in bench_test profile
st = req_api("GET", "/status")[1]
ses = st.get("session", {})
sid = ses.get("session_id")
rev = ses.get("revision", 0)
if not sid or ses.get("profile") != "bench_test":
    if sid:
        req_api("DELETE", f"/sessions/{sid}", {"expected_revision": rev})
    code, created = req_api("POST", "/sessions", {"profile": "bench_test"})
    ses = created.get("session", {})
    sid = ses.get("session_id")
    rev = ses.get("revision", 0)

# Enable bench_tx
if ses.get("bench_tx") != "enabled":
    code, tx_res = req_api("POST", f"/sessions/{sid}/bench-tx", {"enabled": True, "expected_revision": rev})
    print("Enabled bench_tx:", code, tx_res.get("session", {}).get("bench_tx"))

# 2. Start baseline synthetic peers
p1 = req_api("POST", "/injections", {"bus":"low","key":"ses:sbw_status","values":{"angle_aligned":1,"steering_angle_raw":0},"period_ms":20})
p2 = req_api("POST", "/injections", {"bus":"low","key":"seb:bbw_status","values":{"stroke":0,"status":1},"period_ms":20})
p3 = req_api("POST", "/injections", {"bus":"low","key":"mtr:mtr_motor_fbk","values":{"motor_command_speed_mmps":0,"gear_state":0,"fault_flags":0},"period_ms":20})
p4 = req_api("POST", "/injections", {"bus":"high","key":"host:host_heartbeat","values":{"alive_ctr":1,"health_flags":1},"period_ms":500})
print("Started baseline peers")
time.sleep(0.5)

# 3. Send advancing HMI Power ON frames
req_api("POST", "/injections", {"bus":"high","key":"hmi:hmi_pwr_req","values":{"req_start":1, "rolling_counter": 1}})
time.sleep(0.05)
req_api("POST", "/injections", {"bus":"high","key":"hmi:hmi_pwr_req","values":{"req_start":1, "rolling_counter": 2}})
print("Sent Power ON frames")
time.sleep(0.2)

# 4. Send advancing HMI Mode AUTO frames
req_api("POST", "/injections", {"bus":"high","key":"hmi:hmi_mode_req","values":{"req_mode":1, "rolling_counter": 1}})
time.sleep(0.05)
req_api("POST", "/injections", {"bus":"high","key":"hmi:hmi_mode_req","values":{"req_mode":1, "rolling_counter": 2}})
print("Sent Mode AUTO frames")
time.sleep(0.5)

# 5. Check resulting states
st2 = req_api("GET", "/state")[1]
msgs = {f"{m['bus']}:{m['name']}": m["signals"] for m in st2.get("messages", [])}
print("SYS_MODE_CMD mode:", msgs.get("low:SYS_MODE_CMD", {}).get("mode"))
print("RT_STATE_RPT mode:", msgs.get("high:RT_STATE_RPT", {}).get("mode"))
print("SYS_PWR_CMD power:", msgs.get("low:SYS_PWR_CMD", {}).get("power_state"))
