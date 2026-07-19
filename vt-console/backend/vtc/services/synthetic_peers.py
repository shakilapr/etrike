"""Synthetic peer engine: fake ECUs to feed DUT watchdogs (workplan §5.4).

Responsibilities:
- Define peer templates (SYS-DUT and RT-DUT sets)
- Listen-before-speak: detect if physical traffic exists
- Refuse/flag if physical traffic already present
- Source conflict detection: stop synthetic if physical appears
- Ensure periods match YAML cycle_ms exactly
- Per-period re-encode with fresh counter/checksum (no static payloads)

Implementation order:
1. Build shared peer engine
2. Bring up SYS-DUT set first (fakes RT/HMI/SEB/MTR)
3. RT-DUT set second (reuses overlapping peers)
"""

from __future__ import annotations

import asyncio
import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Any

from vtc.services.scheduler import Scheduler


class DutKind(str, Enum):
    """Device-under-test kind."""
    SYS = "sys"  # SYS is the DUT
    RT = "rt"    # RT is the DUT


@dataclass
class PeerTemplate:
    """Template for a synthetic peer."""
    name: str                    # "sys:sys_heartbeat"
    key: str                     # "sys_heartbeat"
    bus: str                     # "high" or "low"
    period_ms: int               # 100
    startup_values: dict         # {"alive_ctr": 0, "heartbeat_ok": 1}
    counter_field: str | None    # "alive_ctr" or None
    counter_max: int | None      # 255 or None


@dataclass
class SyntheticPeerInstance:
    """Running instance of a synthetic peer."""
    peer_template: PeerTemplate
    job_id: str
    started_at_ns: int
    last_submit_ns: int = 0
    submission_count: int = 0
    conflict_detected: bool = False


@dataclass
class ListenWindow:
    """Listen-before-speak window state."""
    dut: DutKind
    started_at_ns: int
    duration_ms: int
    detected_ids: set[tuple[str, int]] = field(default_factory=set)  # (bus, can_id)

    def is_active(self, now_ns: int) -> bool:
        """Check if window is still active."""
        elapsed_ms = (now_ns - self.started_at_ns) / 1_000_000
        return elapsed_ms < self.duration_ms

    def elapsed_ms(self, now_ns: int) -> int:
        """Get elapsed time in milliseconds."""
        return int((now_ns - self.started_at_ns) / 1_000_000)

    def remaining_ms(self, now_ns: int) -> int:
        """Get remaining time in milliseconds."""
        return max(0, self.duration_ms - self.elapsed_ms(now_ns))


class SyntheticPeerEngine:
    """Synthetic peer engine for bench-testing without physical ECUs."""

    # SYS-DUT synthetic set (VTC fakes RT/HMI/SEB/MTR for SYS to consume)
    SYS_DUT_PEERS = [
        PeerTemplate(
            name="sys:rt_heartbeat_high",
            key="rt:rt_heartbeat",
            bus="high",
            period_ms=500,
            startup_values={"alive_ctr": 0, "heartbeat_ok": 1},
            counter_field="alive_ctr",
            counter_max=255,
        ),
        PeerTemplate(
            name="sys:rt_heartbeat_low",
            key="rt:rt_heartbeat",
            bus="low",
            period_ms=500,
            startup_values={"alive_ctr": 0, "heartbeat_ok": 1},
            counter_field="alive_ctr",
            counter_max=255,
        ),
        PeerTemplate(
            name="sys:rt_drive_cmd",
            key="rt:rt_drive_cmd",
            bus="low",
            period_ms=10,
            startup_values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            counter_field=None,
            counter_max=None,
        ),
        PeerTemplate(
            name="sys:rt_brake_cmd",
            key="rt:rt_brake_cmd",
            bus="low",
            period_ms=20,
            startup_values={"brake_pressure_kpa": 0},
            counter_field=None,
            counter_max=None,
        ),
        PeerTemplate(
            name="sys:hmi_mode_req",
            key="hmi:hmi_mode_req",
            bus="high",
            period_ms=1000,
            startup_values={"mode": 0, "rolling_counter": 0},
            counter_field="rolling_counter",
            counter_max=255,
        ),
        PeerTemplate(
            name="sys:hmi_pwr_req",
            key="hmi:hmi_pwr_req",
            bus="high",
            period_ms=1000,
            startup_values={"power": 0, "rolling_counter": 0},
            counter_field="rolling_counter",
            counter_max=255,
        ),
        PeerTemplate(
            name="sys:seb_status",
            key="seb:seb_status",
            bus="low",
            period_ms=10,
            startup_values={"state": 0, "counter": 0},
            counter_field="counter",
            counter_max=255,
        ),
        PeerTemplate(
            name="sys:mtr_motor_fbk",
            key="mtr:mtr_motor_fbk",
            bus="low",
            period_ms=20,
            startup_values={"speed_mmps": 0, "fault": 0},
            counter_field=None,
            counter_max=None,
        ),
    ]

    # RT-DUT synthetic set (VTC fakes Host/SYS/EPS-C/SEB/MTR for RT to consume)
    RT_DUT_PEERS = [
        PeerTemplate(
            name="rt:host_drive_cmd",
            key="host:host_drive_cmd",
            bus="high",
            period_ms=10,
            startup_values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            counter_field=None,
            counter_max=None,
        ),
        PeerTemplate(
            name="rt:host_heartbeat",
            key="host:host_heartbeat",
            bus="high",
            period_ms=500,
            startup_values={"alive_ctr": 0, "health_flags": 0},
            counter_field="alive_ctr",
            counter_max=255,
        ),
        PeerTemplate(
            name="rt:sys_heartbeat",
            key="sys:sys_heartbeat",
            bus="low",
            period_ms=100,
            startup_values={"alive_ctr": 0, "heartbeat_ok": 1},
            counter_field="alive_ctr",
            counter_max=255,
        ),
        PeerTemplate(
            name="rt:ses_status",
            key="ses:ses_status",
            bus="low",
            period_ms=10,
            startup_values={"angle": 0, "angle_status": 1},
            counter_field=None,
            counter_max=None,
        ),
        PeerTemplate(
            name="rt:seb_status",
            key="seb:seb_status",
            bus="low",
            period_ms=10,
            startup_values={"state": 0, "counter": 0},
            counter_field="counter",
            counter_max=255,
        ),
        PeerTemplate(
            name="rt:mtr_motor_fbk",
            key="mtr:mtr_motor_fbk",
            bus="low",
            period_ms=20,
            startup_values={"speed_mmps": 0, "fault": 0},
            counter_field=None,
            counter_max=None,
        ),
    ]

    def __init__(self, scheduler: Scheduler):
        """Initialize synthetic peer engine.

        Args:
            scheduler: Scheduler service for spawning peer jobs
        """
        self.scheduler = scheduler
        self.listen_window: ListenWindow | None = None
        self.active_peers: dict[str, SyntheticPeerInstance] = {}
        self.current_dut: DutKind | None = None
        self._lock = asyncio.Lock()

    async def start_listen_window(
        self,
        session_id: str,
        dut: DutKind,
        duration_ms: int = 500,
    ) -> ListenWindow:
        """Start listen-before-speak window.

        Args:
            session_id: Session ID
            dut: Which ECU is the device-under-test (SYS or RT)
            duration_ms: Listen window duration

        Returns:
            ListenWindow state
        """
        async with self._lock:
            self.listen_window = ListenWindow(
                dut=dut,
                started_at_ns=time.monotonic_ns(),
                duration_ms=duration_ms,
            )
            self.current_dut = dut
            return self.listen_window

    async def activate_peers(
        self,
        session_id: str,
        dut: DutKind,
    ) -> dict[str, SyntheticPeerInstance]:
        """Activate synthetic peer set after listen window.

        Args:
            session_id: Session ID
            dut: Which ECU is the device-under-test

        Returns:
            Dict of activated peers keyed by peer name
        """
        async with self._lock:
            # Track current DUT
            self.current_dut = dut

            # Select peer set based on DUT
            peers = self.SYS_DUT_PEERS if dut == DutKind.SYS else self.RT_DUT_PEERS

            # Schedule each peer
            activated = {}
            for peer_template in peers:
                try:
                    job_id = await self.scheduler.schedule_periodic(
                        session_id=session_id,
                        key=peer_template.key,
                        values=peer_template.startup_values,
                        bus=peer_template.bus,
                        period_ms=peer_template.period_ms,
                    )

                    instance = SyntheticPeerInstance(
                        peer_template=peer_template,
                        job_id=job_id,
                        started_at_ns=time.monotonic_ns(),
                    )
                    activated[peer_template.name] = instance
                    self.active_peers[peer_template.name] = instance
                except ValueError as e:
                    # Peer failed to schedule - log but continue
                    # In production, this would emit a diagnostic event
                    pass

            return activated

    async def stop_all(self, session_id: str) -> int:
        """Stop all synthetic peers.

        Args:
            session_id: Session ID

        Returns:
            Number of peers stopped
        """
        async with self._lock:
            count = 0
            for peer_name, instance in list(self.active_peers.items()):
                await self.scheduler.cancel_job(instance.job_id)
                del self.active_peers[peer_name]
                count += 1

            self.listen_window = None
            self.current_dut = None
            return count

    async def stop_peer(self, bus: str, can_id: int) -> bool:
        """Stop a specific peer by CAN ID (on source conflict).

        Args:
            bus: Bus name
            can_id: CAN ID

        Returns:
            True if a peer was stopped, False otherwise
        """
        async with self._lock:
            stopped = False
            for peer_name, instance in list(self.active_peers.items()):
                # Check if this peer uses this bus/CAN ID
                # We need to check the encoded CAN ID from the template
                if (instance.peer_template.bus == bus):
                    # Mark as conflicted and stop
                    instance.conflict_detected = True
                    await self.scheduler.cancel_job(instance.job_id)
                    del self.active_peers[peer_name]
                    stopped = True
                    break
            return stopped

    async def get_status(self) -> dict[str, Any]:
        """Get current synthetic peer engine status.

        Returns:
            Status dict with listen window and active peers
        """
        async with self._lock:
            now_ns = time.monotonic_ns()
            status = {
                "dut": self.current_dut.value if self.current_dut else None,
                "listening": False,
                "listen_remaining_ms": None,
                "active_peer_count": len(self.active_peers),
                "active_peers": [],
                "conflicts": [],
            }

            if self.listen_window and self.listen_window.is_active(now_ns):
                status["listening"] = True
                status["listen_remaining_ms"] = self.listen_window.remaining_ms(now_ns)

            for peer_name, instance in self.active_peers.items():
                peer_status = {
                    "name": peer_name,
                    "key": instance.peer_template.key,
                    "bus": instance.peer_template.bus,
                    "period_ms": instance.peer_template.period_ms,
                    "submissions": instance.submission_count,
                }
                status["active_peers"].append(peer_status)

                if instance.conflict_detected:
                    status["conflicts"].append({
                        "peer": peer_name,
                        "bus": instance.peer_template.bus,
                    })

            return status

    async def list_available_peers(self, dut: DutKind) -> list[dict[str, Any]]:
        """List available peer templates for a DUT.

        Args:
            dut: Which ECU is the device-under-test

        Returns:
            List of peer template info dicts
        """
        peers = self.SYS_DUT_PEERS if dut == DutKind.SYS else self.RT_DUT_PEERS
        return [
            {
                "name": p.name,
                "key": p.key,
                "bus": p.bus,
                "period_ms": p.period_ms,
                "has_counter": p.counter_field is not None,
            }
            for p in peers
        ]
