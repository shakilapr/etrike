import React, { useState, useRef, useEffect, useCallback } from 'react';
import {
  sendHostDriveCmd, sendBrakeReq, sendLightCmd,
  sendObstacleDist, sendHeartbeat, sendEstop, sendHmiModeReq
} from '../api/inject';

// ────────────────────────────────────────────
// Sub-components
// ────────────────────────────────────────────

function CmdRow({ label, unit, min, max, value, onChange, step = 1 }: {
  label: string; unit: string; min: number; max: number;
  value: number; onChange: (v: number) => void; step?: number;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <span className="cmd-label">{label}</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <input
            type="number" min={min} max={max} step={step} value={value}
            onChange={e => onChange(Number(e.target.value))}
            style={{ width: 90, height: 28, fontSize: 13, border: '1px solid var(--border-strong)', borderRadius: 'var(--radius)', padding: '0 7px', fontVariantNumeric: 'tabular-nums' }}
          />
          <span className="cmd-unit" style={{ minWidth: 50, textAlign: 'right' }}>{unit}</span>
        </div>
      </div>
      <input type="range" min={min} max={max} step={step} value={value}
        onChange={e => onChange(Number(e.target.value))} />
      <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 10.5, color: 'var(--text-tertiary)' }}>
        <span>{min}</span><span>{max}</span>
      </div>
    </div>
  );
}

function Toggle({ id, checked, onChange, danger }: {
  id: string; checked: boolean; onChange: (v: boolean) => void; danger?: boolean;
}) {
  return (
    <label className={`toggle-switch ${danger ? 'danger' : ''}`} htmlFor={id}>
      <input id={id} type="checkbox" checked={checked} onChange={e => onChange(e.target.checked)} />
      <span className="toggle-track" />
    </label>
  );
}

function ActiveBadge({ active }: { active: boolean }) {
  return (
    <div className={`cmd-status ${active ? 'active' : ''}`}>
      <span className={`status-dot ${active ? 'success' : ''}`} style={{ background: active ? undefined : 'var(--border-strong)' }} />
      <span style={{ fontSize: 11 }}>{active ? 'Transmitting' : 'Idle'}</span>
    </div>
  );
}

// ────────────────────────────────────────────
// Drive Command Tab
// ────────────────────────────────────────────

function DriveTab() {
  const [speed, setSpeed] = useState(0);
  const [yaw, setYaw] = useState(0);
  const [gear, setGear] = useState(1); // D
  const [active, setActive] = useState(false);
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const speedRef = useRef(speed);
  const yawRef = useRef(yaw);
  const gearRef = useRef(gear);
  speedRef.current = speed;
  yawRef.current = yaw;
  gearRef.current = gear;

  useEffect(() => {
    if (!active) { sendHostDriveCmd(0, 0, 1); return; }
    intervalRef.current = setInterval(() => {
      sendHostDriveCmd(speedRef.current, yawRef.current, gearRef.current);
    }, 50);
    return () => { if (intervalRef.current) clearInterval(intervalRef.current); };
  }, [active]);

  const gears = ['N', 'D', 'S', 'R'] as const;

  return (
    <>
      <div className="command-body">
        <CmdRow label="Speed" unit="mm/s" min={-500} max={3000} value={speed} onChange={setSpeed} />
        <CmdRow label="Yaw Rate" unit="mrad/s" min={-3000} max={3000} value={yaw} onChange={setYaw} />
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
          <span className="cmd-label">Gear</span>
          <div className="gear-selector" id="gear-selector">
            {gears.map((g, i) => (
              <button key={g} className={`gear-btn ${g} ${gear === i ? 'selected' : ''}`}
                onClick={() => setGear(i)} id={`gear-${g}`}>{g}</button>
            ))}
          </div>
        </div>
      </div>
      <div className="cmd-footer">
        <ActiveBadge active={active} />
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span className="cmd-tx-rate">20 Hz</span>
          <button
            className={`send-btn ${active ? 'secondary' : ''}`}
            id="drive-toggle"
            onClick={() => setActive(!active)}
          >
            {active ? '⏹ Stop TX' : '▶ Start TX'}
          </button>
        </div>
      </div>
    </>
  );
}

// ────────────────────────────────────────────
// Brake Tab
// ────────────────────────────────────────────

function BrakeTab() {
  const [pressure, setPressure] = useState(0);
  const [sending, setSending] = useState(false);

  const handleSend = async () => {
    setSending(true);
    await sendBrakeReq(pressure);
    setSending(false);
  };

  return (
    <>
      <div className="command-body">
        <CmdRow label="Brake Pressure" unit="kPa" min={0} max={20000} value={pressure} onChange={setPressure} step={100} />
        <div style={{ display: 'flex', gap: 8 }}>
          <button className="send-btn secondary" id="brake-zero" onClick={() => setPressure(0)}>Release (0)</button>
          <button className="send-btn secondary" id="brake-max" onClick={() => setPressure(20000)}>Max (20000)</button>
        </div>
      </div>
      <div className="cmd-footer">
        <span style={{ fontSize: 11, color: 'var(--text-tertiary)' }}>On-demand — not periodic</span>
        <button className="send-btn" id="brake-send" onClick={handleSend} disabled={sending}>
          {sending ? '...' : '▶ Send'}
        </button>
      </div>
    </>
  );
}

// ────────────────────────────────────────────
// Lights Tab
// ────────────────────────────────────────────

function LightsTab() {
  const [leftTurn, setLeftTurn] = useState(false);
  const [rightTurn, setRightTurn] = useState(false);
  const [brakeLight, setBrakeLight] = useState(false);
  const [headlight, setHeadlight] = useState(false);

  const send = useCallback(async (lt: boolean, rt: boolean, bl: boolean, hl: boolean) => {
    await sendLightCmd(lt, rt, bl, hl);
  }, []);

  return (
    <>
      <div className="command-body">
        <div style={{ border: '1px solid var(--border)', borderRadius: 'var(--radius)', overflow: 'hidden' }}>
          {[
            { id: 'light-left', label: 'Left Turn Signal', desc: 'bit 0 of byte 0', val: leftTurn, setter: setLeftTurn, cls: '' },
            { id: 'light-right', label: 'Right Turn Signal', desc: 'bit 1 of byte 0', val: rightTurn, setter: setRightTurn, cls: '' },
            { id: 'light-brake', label: 'Brake Light', desc: 'bit 2 of byte 0', val: brakeLight, setter: setBrakeLight, cls: 'danger' },
            { id: 'light-head', label: 'Headlight', desc: 'bit 3 of byte 0', val: headlight, setter: setHeadlight, cls: '' },
          ].map(({ id, label, desc, val, setter, cls }) => (
            <div key={id} className="toggle-row" style={{ padding: '10px 14px' }}>
              <div className="toggle-label">
                {label}
                <small>{desc}</small>
              </div>
              <Toggle id={id} checked={val} onChange={v => { setter(v); send(
                id === 'light-left' ? v : leftTurn,
                id === 'light-right' ? v : rightTurn,
                id === 'light-brake' ? v : brakeLight,
                id === 'light-head' ? v : headlight,
              ); }} danger={cls === 'danger'} />
            </div>
          ))}
        </div>

        <div className="light-indicators">
          <div className={`light-ind ${leftTurn ? 'active' : ''}`} id="ind-left">◄ Left</div>
          <div className={`light-ind ${rightTurn ? 'active' : ''}`} id="ind-right">Right ►</div>
          <div className={`light-ind ${brakeLight ? 'active red' : ''}`} id="ind-brake">⬤ Brake</div>
          <div className={`light-ind ${headlight ? 'active blue' : ''}`} id="ind-head">◎ Head</div>
        </div>
      </div>
      <div className="cmd-footer">
        <span style={{ fontSize: 11, color: 'var(--text-tertiary)' }}>Sent on toggle change</span>
        <button className="send-btn secondary" id="lights-off" onClick={() => {
          setLeftTurn(false); setRightTurn(false); setBrakeLight(false); setHeadlight(false);
          sendLightCmd(false, false, false, false);
        }}>All Off</button>
      </div>
    </>
  );
}

// ────────────────────────────────────────────
// Obstacle Tab
// ────────────────────────────────────────────

function ObstacleTab() {
  const [dist, setDist] = useState(4294967295);
  const [active, setActive] = useState(false);
  const distRef = useRef(dist);
  distRef.current = dist;

  useEffect(() => {
    if (!active) return;
    const id = setInterval(() => sendObstacleDist(distRef.current), 100);
    return () => clearInterval(id);
  }, [active]);

  return (
    <>
      <div className="command-body">
        <CmdRow label="Distance" unit="mm" min={0} max={4294967295} value={dist} onChange={setDist} step={100} />
        <div style={{ display: 'flex', gap: 8 }}>
          <button className="send-btn secondary" id="obs-clear" onClick={() => { setDist(4294967295); sendObstacleDist(4294967295); }}>
            ✓ Clear (∞)
          </button>
          <button className="send-btn secondary" id="obs-1m" onClick={() => { setDist(1000); }}>1 m</button>
          <button className="send-btn secondary" id="obs-5m" onClick={() => { setDist(5000); }}>5 m</button>
        </div>
      </div>
      <div className="cmd-footer">
        <ActiveBadge active={active} />
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span className="cmd-tx-rate">10 Hz</span>
          <button className={`send-btn ${active ? 'secondary' : ''}`} id="obs-toggle" onClick={() => setActive(!active)}>
            {active ? '⏹ Stop TX' : '▶ Start TX'}
          </button>
        </div>
      </div>
    </>
  );
}

// ────────────────────────────────────────────
// Heartbeat Tab
// ────────────────────────────────────────────

function HeartbeatTab() {
  const [active, setActive] = useState(false);
  const [ctr, setCtr] = useState(0);
  const [flags, setFlags] = useState(0);
  const ctrRef = useRef(0);
  const flagsRef = useRef(flags);
  flagsRef.current = flags;

  useEffect(() => {
    if (!active) return;
    const id = setInterval(() => {
      ctrRef.current = (ctrRef.current + 1) & 0xff;
      setCtr(ctrRef.current);
      sendHeartbeat(ctrRef.current, flagsRef.current);
    }, 500);
    return () => clearInterval(id);
  }, [active]);

  return (
    <>
      <div className="command-body">
        <div className="hb-display">
          <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <span className="hb-label">ALIVE COUNTER</span>
            <span className="hb-counter" id="hb-counter">0x{ctr.toString(16).toUpperCase().padStart(2, '0')}</span>
          </div>
          <div style={{ flex: 1 }} />
          <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <span className="hb-label">DECIMAL</span>
            <span style={{ fontFamily: 'inherit', fontSize: 18, fontWeight: 600 }}>{ctr}</span>
          </div>
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
          <span className="cmd-label">Health Flags (byte)</span>
          <input type="number" min={0} max={255} value={flags}
            onChange={e => setFlags(Number(e.target.value))}
            style={{ height: 32, border: '1px solid var(--border-strong)', borderRadius: 'var(--radius)', padding: '0 8px', fontSize: 13 }}
          />
        </div>
      </div>
      <div className="cmd-footer">
        <ActiveBadge active={active} />
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span className="cmd-tx-rate">2 Hz (500ms)</span>
          <button className={`send-btn ${active ? 'secondary' : ''}`} id="hb-toggle" onClick={() => { setActive(!active); if (!active) ctrRef.current = 0; setCtr(0); }}>
            {active ? '⏹ Stop HB' : '▶ Start HB'}
          </button>
        </div>
      </div>
    </>
  );
}

// ────────────────────────────────────────────
// HMI Tab
// ────────────────────────────────────────────

function HmiTab() {
  const [mode, setMode] = useState<0 | 1>(0);
  const [rolling, setRolling] = useState(0);
  const [active, setActive] = useState(false);
  const modeRef = useRef(mode);
  const rollingRef = useRef(rolling);
  modeRef.current = mode;

  useEffect(() => {
    if (!active) return;
    const id = setInterval(() => {
      rollingRef.current = (rollingRef.current + 1) & 0xff;
      setRolling(rollingRef.current);
      sendHmiModeReq(modeRef.current, rollingRef.current);
    }, 1000);
    return () => clearInterval(id);
  }, [active]);

  return (
    <>
      <div className="command-body">
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
          <span className="cmd-label">Requested Mode</span>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
            {([0, 1] as const).map(m => (
              <button key={m} id={`hmi-mode-${m}`}
                className={`gear-btn ${mode === m ? 'selected' : ''}`}
                onClick={() => setMode(m)}
              >
                {m === 0 ? 'MANUAL' : 'AUTO'}
              </button>
            ))}
          </div>
        </div>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', fontSize: 12, color: 'var(--text-secondary)' }}>
          <span>Rolling Counter</span>
          <span className="mono" id="hmi-rolling">{rolling}</span>
        </div>
        <div style={{ padding: 10, background: 'var(--warning-soft)', borderRadius: 'var(--radius)', border: '1px solid #dea82c', fontSize: 11.5, color: 'var(--warning)' }}>
          ⚠ HMI spoofing — use for bench testing only
        </div>
      </div>
      <div className="cmd-footer">
        <ActiveBadge active={active} />
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span className="cmd-tx-rate">1 Hz</span>
          <button className={`send-btn ${active ? 'secondary' : ''}`} id="hmi-toggle" onClick={() => setActive(!active)}>
            {active ? '⏹ Stop' : '▶ Start'}
          </button>
        </div>
      </div>
    </>
  );
}

// ────────────────────────────────────────────
// E-STOP Tab
// ────────────────────────────────────────────

function EstopTab() {
  const [confirming, setConfirming] = useState(false);
  const [sent, setSent] = useState(false);

  const handleEstop = async () => {
    if (!confirming) { setConfirming(true); return; }
    await sendEstop();
    setSent(true);
    setConfirming(false);
    setTimeout(() => setSent(false), 3000);
  };

  return (
    <div className="command-body">
      <div className="estop-area">
        {sent && (
          <div style={{ padding: '8px 16px', background: 'var(--danger-soft)', border: '1px solid var(--danger)', borderRadius: 'var(--radius)', color: 'var(--danger)', fontWeight: 600, fontSize: 13 }}>
            ⚡ E-STOP SENT on HIGH + LOW bus
          </div>
        )}
        <button className="estop-btn" id="estop-btn" onClick={handleEstop}>
          {confirming ? 'CONFIRM?' : '🛑 E-STOP'}
        </button>
        {confirming && (
          <div style={{ display: 'flex', gap: 8 }}>
            <button className="send-btn danger" id="estop-confirm" onClick={handleEstop}>Yes, send ESTOP</button>
            <button className="send-btn secondary" id="estop-cancel" onClick={() => setConfirming(false)}>Cancel</button>
          </div>
        )}
        <p className="estop-label">
          Sends a zero-length frame (0x001) on both<br />
          <strong>high</strong> and <strong>low</strong> buses — triggers immediate ESTOP<br />
          in RT, SYS, and MTR ECUs.
        </p>
      </div>
    </div>
  );
}

// ────────────────────────────────────────────
// Main CommandPanel
// ────────────────────────────────────────────

const TABS = [
  { id: 'drive', label: '⚡ Drive', danger: false },
  { id: 'brake', label: '🔴 Brake', danger: false },
  { id: 'lights', label: '💡 Lights', danger: false },
  { id: 'obstacle', label: '📡 Obstacle', danger: false },
  { id: 'heartbeat', label: '💓 Heartbeat', danger: false },
  { id: 'hmi', label: '🎛 HMI', danger: false },
  { id: 'estop', label: '🛑 E-STOP', danger: true },
] as const;

type TabId = typeof TABS[number]['id'];

export function CommandPanel() {
  const [activeTab, setActiveTab] = useState<TabId>('drive');

  return (
    <div className="command-panel workspace-panel" id="command-panel">
      <div className="panel-header">
        <div>
          <p className="panel-title">CAN Command Panel</p>
          <p className="panel-subtitle">Inject commands directly onto CAN buses</p>
        </div>
      </div>

      <div className="command-tabs" role="tablist">
        {TABS.map(t => (
          <button
            key={t.id}
            role="tab"
            id={`tab-${t.id}`}
            className={`command-tab ${t.danger ? 'danger' : ''} ${activeTab === t.id ? 'active' : ''}`}
            onClick={() => setActiveTab(t.id)}
          >
            {t.label}
          </button>
        ))}
      </div>

      {activeTab === 'drive' && <DriveTab />}
      {activeTab === 'brake' && <BrakeTab />}
      {activeTab === 'lights' && <LightsTab />}
      {activeTab === 'obstacle' && <ObstacleTab />}
      {activeTab === 'heartbeat' && <HeartbeatTab />}
      {activeTab === 'hmi' && <HmiTab />}
      {activeTab === 'estop' && <EstopTab />}
    </div>
  );
}
