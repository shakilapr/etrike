import urllib.request
import json
import time
import sys

URL = "http://127.0.0.1:8001/api/v1/synthetic-peers/feed"

def make_payload(t):
    # slight dynamic variation over time
    speed_noise = 2500 + int(100 * ((t % 10) - 5))
    return {
        "messages": [
            {
                "bus": "high",
                "can_id": 0x300,
                "key": "host:host_drive_cmd",
                "name": "HOST_DRIVE_CMD",
                "expected_rate_hz": 20.0,
                "signals": {
                    "speed_mmps": 2778,
                    "yaw_rate_mrad_s": 120,
                    "gear": "D"
                }
            },
            {
                "bus": "low",
                "can_id": 0x204,
                "key": "rt:rt_drive_cmd",
                "name": "RT_DRIVE_CMD",
                "expected_rate_hz": 20.0,
                "signals": {
                    "motor_speed_mmps": speed_noise,
                    "gear": "D"
                }
            },
            {
                "bus": "low",
                "can_id": 0x206,
                "key": "mtr:mtr_motor_fbk",
                "name": "MTR_MOTOR_FBK",
                "expected_rate_hz": 20.0,
                "signals": {
                    "motor_command_speed_mmps": speed_noise,
                    "actual_speed_rpm": int(speed_noise * 0.8),
                    "gear_state": "D",
                    "drive_mode": "AUTO"
                }
            },
            {
                "bus": "low",
                "can_id": 0x122,
                "key": "rt:rt_wheel_speed_sts",
                "name": "RT_WHEEL_SPEED_STS",
                "expected_rate_hz": 10.0,
                "signals": {
                    "measured_speed_mmps": speed_noise
                }
            },
            {
                "bus": "low",
                "can_id": 0x110,
                "key": "sys:sys_mode_cmd",
                "name": "SYS_MODE_CMD",
                "expected_rate_hz": 10.0,
                "signals": {
                    "mode": "AUTO"
                }
            },
            {
                "bus": "low",
                "can_id": 0x011,
                "key": "sys:sys_safety_sts",
                "name": "SYS_SAFETY_STS",
                "expected_rate_hz": 10.0,
                "signals": {
                    "estop_active": 0,
                    "estop_source": 0
                }
            },
            {
                "bus": "high",
                "can_id": 0x303,
                "key": "host:host_steer_cmd",
                "name": "HOST_STEER_CMD",
                "expected_rate_hz": 20.0,
                "signals": {
                    "target_angle_deg": 14.0
                }
            },
            {
                "bus": "low",
                "can_id": 0x201,
                "key": "ses:ses_status",
                "name": "SES_STATUS",
                "expected_rate_hz": 20.0,
                "signals": {
                    "actual_angle_deg": 12.4,
                    "torque_nm": 4.8,
                    "mode": "Active"
                }
            },
            {
                "bus": "high",
                "can_id": 0x310,
                "key": "rt:steer_diag",
                "name": "STEER_DIAG",
                "expected_rate_hz": 10.0,
                "signals": {
                    "rt_target_angle_deg": 12.0,
                    "slew_rate": 85
                }
            },
            {
                "bus": "high",
                "can_id": 0x301,
                "key": "host:host_brake_req",
                "name": "HOST_BRAKE_REQ",
                "expected_rate_hz": 20.0,
                "signals": {
                    "brake_pressure_kpa": 2000
                }
            },
            {
                "bus": "low",
                "can_id": 0x205,
                "key": "rt:rt_brake_cmd",
                "name": "RT_BRAKE_CMD",
                "expected_rate_hz": 20.0,
                "signals": {
                    "brake_pressure_kpa": 1900
                }
            },
            {
                "bus": "low",
                "can_id": 0x721,
                "key": "seb:seb_status",
                "name": "SEB_STATUS",
                "expected_rate_hz": 20.0,
                "signals": {
                    "pressure_kpa": 1850,
                    "stroke_mm": 18.5
                }
            },
            {
                "bus": "low",
                "can_id": 0x7FE,
                "key": "sys:sys_heartbeat",
                "name": "SYS_HEARTBEAT",
                "expected_rate_hz": 10.0,
                "signals": {
                    "mode_auto": 1
                }
            },
            {
                "bus": "low",
                "can_id": 0x7FD,
                "key": "rt:rt_heartbeat",
                "name": "RT_HEARTBEAT",
                "expected_rate_hz": 10.0,
                "signals": {
                    "alive": 1
                }
            },
            {
                "bus": "high",
                "can_id": 0x7FC,
                "key": "host:host_heartbeat",
                "name": "HOST_HEARTBEAT",
                "expected_rate_hz": 10.0,
                "signals": {
                    "alive": 1
                }
            }
        ]
    }

def send_once(t=0):
    payload = make_payload(t)
    req = urllib.request.Request(
        URL,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=2.0) as resp:
        return json.loads(resp.read().decode("utf-8"))

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--once":
        res = send_once()
        print("Sent once:", res)
        sys.exit(0)
    print("Starting Dummy CAN Daemon (broadcasts every 100ms)...")
    count = 0
    while True:
        try:
            send_once(count)
            count += 1
            if count % 50 == 0:
                print(f"Pushed {count} dummy CAN cycles...")
            time.sleep(0.1)
        except Exception as e:
            print("Feed error:", e)
            time.sleep(1.0)
