"""Scenario: SEB FAULT CLASSES AGAINST THE REAL SYS.

SYS is the consumer of our SEB restbus. Boundary faults (silent / stuck / L3)
must drive the *real* SYS into the documented faults — recoverable comms inhibit
for silence, latched brake-following fault for a stuck actuator, latched ESTOP
for L3 — and reset must be refused while the cause is still active.
"""
from __future__ import annotations

import time

import pytest

from assertions import safety
from assertions.temporal import never_window
from bench.wire import LOW, SEB_STATUS, SYS_SAFETY_STS
from faults.actuator_faults import seb_set
from scenarios.procedures import operator_start_reset, wait_sys_clear


@pytest.mark.hardware
def test_seb_silent_sys_comms_inhibit_and_reset(physical_bench):
    """Silence the SEB restbus while SYS is healthy. Real SYS must flag the
    0x721-comms condition (native tests 60 / 88) and refuse/relatch reset until
    the SEB stream is fresh again."""
    bench = physical_bench
    capture = bench.capture
    seb = bench.restbus.seb
    wait_sys_clear(bench)
    # sanity: fresh 0x721 is reaching SYS
    capture.wait_for(
        lambda s: s.bus == LOW and s.key == SEB_STATUS and s.is_valid, timeout=3.0
    )
    seb_set(seb, "silent")
    try:
        # SYS keeps hearing nothing -> system must not silently re-enable traction
        with never_window(capture, lambda: safety.motor_unsafe(capture)):
            time.sleep(3.0)
        operator_start_reset(bench)  # attempted reset while cause present
        time.sleep(1.0)
        # authoritative: no 0x011 clear may be published while SEB comms are lost
        sys_clear = capture.latest_on(LOW, SYS_SAFETY_STS)
        is_clear = sys_clear is not None and sys_clear.is_valid and sys_clear.values["estop_active"] == 0
        assert not is_clear, "SYS published clear while SEB comms still lost"
    finally:
        seb_set(seb, "healthy")
        wait_sys_clear(bench)


@pytest.mark.hardware
def test_seb_stuck_latches_brake_following_fault(physical_bench):
    """SEB stuck at 0 mm while SYS commands stroke -> SYS latches a brake
    following fault and refuses reset until the actuator follows again."""
    bench = physical_bench
    capture = bench.capture
    host = bench.restbus.host
    wait_sys_clear(bench)

    host.set_brake(8000, active=True)   # force SYS to command real stroke
    host.start()
    try:
        time.sleep(0.5)
        seb_set(bench.restbus.seb, "stuck")
        time.sleep(3.0)                 # following-error confirm interval
        operator_start_reset(bench)
        time.sleep(1.0)
        latest = capture.latest_on(LOW, SYS_SAFETY_STS)
        if latest is not None and latest.is_valid:
            assert latest.values["estop_active"] == 1, \
                "SYS cleared an active brake-following fault without a healthy cause"
    finally:
        bench.restbus.seb.fault_mode = "healthy"
        host.set_brake(0, active=False)
        host.stop()
        wait_sys_clear(bench)


@pytest.mark.selftest
def test_selftest_seb_healthy_follows_command(vb):
    """Seb restbus: healthy mode reports the commanded stroke with a rolling
    counter and decodes through the vendor codec."""
    from bench.can import VirtualLink
    from bench.capture import Capture
    from bench.config import BenchConfig
    from restbus.seb import SebNode
    from protocol.codecs.python.types import Frame
    from protocol.codecs.python import seb as seb_codec

    link = VirtualLink(vb.cfg)
    capture = Capture(link)
    capture.start()
    cmd = {"control_mode": 0, "stroke_request_raw": 900, "auto_brake": False, "rolling_counter": 1}
    seb = SebNode(link, command_provider=lambda: cmd)
    seb.start()
    try:
        sample = capture.wait_for(
            lambda s: s.bus == "low" and s.key == SEB_STATUS and s.is_valid, timeout=3.0
        )
        status, decoded = seb_codec.decode_status(
            Frame(bus="low", id=0x721, frame_format="standard", data=sample.data)
        )
        assert status == "ok"
        assert int(decoded["stroke_value_raw"]) == 900, "SEB did not follow the commanded stroke"
    finally:
        seb.stop()
        capture.stop()
        link.close()


@pytest.mark.selftest
def test_selftest_seb_stuck_holds_stroke(vb):
    from bench.can import VirtualLink
    from bench.capture import Capture
    from restbus.seb import SebNode

    link = VirtualLink(vb.cfg)
    capture = Capture(link)
    capture.start()
    state = {"stroke": 600}
    cmd = {"control_mode": 0, "stroke_request_raw": 1500, "auto_brake": False, "rolling_counter": 1}
    seb = SebNode(link, command_provider=lambda: cmd)
    seb.inject_fault("stuck")
    seb.start()
    try:
        sample = capture.wait_for(
            lambda s: s.bus == "low" and s.key == SEB_STATUS and s.is_valid, timeout=3.0
        )
        from bench.wire import decode_message
        key, values = decode_message("low", 0x721, sample.data)
        assert values is not None and values["stroke_value_raw"] == 600, \
            "stuck SEB must keep reporting its old stroke"
    finally:
        seb.stop()
        capture.stop()
        link.close()


@pytest.mark.selftest
def test_selftest_seb_l3_reports_error_status(vb):
    from bench.can import VirtualLink
    from bench.capture import Capture
    from restbus.seb import SebNode

    link = VirtualLink(vb.cfg)
    capture = Capture(link)
    capture.start()
    seb = SebNode(link, command_provider=lambda: None)
    seb.inject_fault("l3_fault")
    seb.start()
    try:
        sample = capture.wait_for(
            lambda s: s.bus == "low" and s.key == SEB_STATUS and s.is_valid, timeout=3.0
        )
        from bench.wire import decode_message
        _, values = decode_message("low", 0x721, sample.data)
        assert values is not None and values["error_status"] == 3, "SEB L3 must report error_status=3"
        capture.wait_for(
            lambda s: s.bus == "low" and s.key == "seb:seb_err_info" and s.is_valid,
            timeout=2.0,
        )
    finally:
        seb.stop()
        capture.stop()
        link.close()
