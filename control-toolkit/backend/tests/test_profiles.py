"""Profile availability and controlled transitions."""


def test_list_profiles(client):
    r = client.get("/api/v1/sessions/profiles")
    assert r.status_code == 200
    body = r.json()
    by_id = {p["id"]: p for p in body["profiles"]}
    # All profiles are selectable; physical link is reported separately.
    assert by_id["pure_software"]["available"] is True
    assert by_id["bench_test"]["available"] is True
    assert by_id["full_vehicle"]["available"] is True
    assert by_id["bench_test"]["link_available"] is False
    assert by_id["full_vehicle"]["link_available"] is False
    adapter = body["physical_adapter"]
    assert adapter["kind"] == "canalystii"
    assert adapter["channels"] == {"high": "CH0", "low": "CH1"}
    assert adapter["usb_vid"] == 0x04D8
    assert adapter["usb_pid"] == 0x0053
    assert adapter["python_can_version"] == "4.6.1"
    assert adapter["available"] is False


def test_create_pure_software_session(client):
    r = client.post("/api/v1/sessions", json={"profile": "pure_software"})
    assert r.status_code == 200
    ses = r.json()["session"]
    assert ses["profile"] == "pure_software"
    assert ses["phase"] == "running"
    assert ses["destination"] == "virtual"
    assert ses["bench_tx"] == "disabled"
    assert ses["test_session_id"]
    assert ses["wire_hash"]
    assert ses["semantic_hash"]


def test_full_vehicle_allowed_without_adapter(client):
    """Real mode is enterable without CANalyst; no silent virtual fallback."""
    r = client.post("/api/v1/sessions", json={"profile": "full_vehicle"})
    assert r.status_code == 200
    ses = r.json()["session"]
    assert ses["profile"] == "full_vehicle"
    assert ses["destination"] == "physical"
    assert ses["phase"] == "running"
    assert ses["bench_tx"] == "disabled"
    assert "link_absent" in (ses.get("capabilities") or [])
    st = client.get("/api/v1/status").json()
    assert st["adapter"]["health"] == "absent"
    assert st["link"]["mode"] == "real"
    assert st["link"]["connected"] is False


def test_bench_test_allowed_without_adapter(client):
    r = client.post("/api/v1/sessions", json={"profile": "bench_test"})
    assert r.status_code == 200
    ses = r.json()["session"]
    assert ses["profile"] == "bench_test"
    assert ses["destination"] == "physical"
    assert ses["bench_tx"] == "disabled"
    # TX still blocked without open adapter
    r2 = client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    assert r2.status_code in (409, 503)


def test_profile_change_requires_confirm(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    sid = ses["session_id"]
    r = client.post(
        f"/api/v1/sessions/{sid}/profile",
        json={"profile": "pure_software", "confirm": False},
    )
    # same profile with confirm false still requires confirm flag
    assert r.status_code == 400
    assert r.json()["code"] == "profile.confirm_required"


def test_profile_change_to_physical_without_adapter(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    sid = ses["session_id"]
    r = client.post(
        f"/api/v1/sessions/{sid}/profile",
        json={
            "profile": "full_vehicle",
            "confirm": True,
            "expected_revision": ses["revision"],
        },
    )
    assert r.status_code == 200
    body = r.json()["session"]
    assert body["profile"] == "full_vehicle"
    assert body["destination"] == "physical"
    assert body["bench_tx"] == "disabled"
    st = client.get("/api/v1/status").json()
    assert st["adapter"]["health"] == "absent"
    assert st["link"]["connected"] is False
