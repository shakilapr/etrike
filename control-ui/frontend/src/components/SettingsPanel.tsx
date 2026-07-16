import React, { useState } from 'react';

export function SettingsPanel() {
  const [profile, setProfile] = useState('full-vehicle');
  const [adapter, setAdapter] = useState('auto');
  const [pollDelay, setPollDelay] = useState(1);
  const [theme, setTheme] = useState('dark');
  const [previewMode, setPreviewMode] = useState('overlay');

  return (
    <div className="settings-panel" style={{ padding: '24px', overflowY: 'auto', height: '100%', background: 'var(--surface)' }}>
      <header style={{ marginBottom: '32px' }}>
        <h2 style={{ fontSize: '24px', fontWeight: 600, margin: 0 }}>Settings</h2>
        <p style={{ color: 'var(--text-secondary)', margin: '4px 0 0 0', fontSize: '14px' }}>
          Configure operational parameters and UI preferences.
        </p>
      </header>

      <div style={{ display: 'flex', flexDirection: 'column', gap: '32px', maxWidth: '600px' }}>
        <section>
          <h3 style={{ fontSize: '14px', fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-tertiary)', borderBottom: '1px solid var(--border)', paddingBottom: '8px', marginBottom: '16px' }}>
            Operating Profile Selection
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
            <label style={{ display: 'flex', alignItems: 'center', gap: '8px', cursor: 'pointer' }}>
              <input type="radio" name="profile" value="full-vehicle" checked={profile === 'full-vehicle'} onChange={() => setProfile('full-vehicle')} />
              <span>Full Vehicle</span>
            </label>
            <label style={{ display: 'flex', alignItems: 'center', gap: '8px', cursor: 'pointer' }}>
              <input type="radio" name="profile" value="bench-test" checked={profile === 'bench-test'} onChange={() => setProfile('bench-test')} />
              <span>Bench Test</span>
            </label>
            <label style={{ display: 'flex', alignItems: 'center', gap: '8px', cursor: 'pointer' }}>
              <input type="radio" name="profile" value="pure-software" checked={profile === 'pure-software'} onChange={() => setProfile('pure-software')} />
              <span>Pure Software</span>
            </label>
          </div>
        </section>

        <section>
          <h3 style={{ fontSize: '14px', fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-tertiary)', borderBottom: '1px solid var(--border)', paddingBottom: '8px', marginBottom: '16px' }}>
            Hardware & Adapter Config
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
            <label style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
              <span style={{ fontSize: '14px', fontWeight: 500 }}>USB Device</span>
              <select value={adapter} onChange={e => setAdapter(e.target.value)} style={{ padding: '8px 12px', background: 'var(--surface-subtle)', border: '1px solid var(--border)', borderRadius: 'var(--radius)', fontSize: '14px', maxWidth: '300px' }}>
                <option value="auto">Auto-detect</option>
                <option value="canalyst-ii">CANalyst-II</option>
                <option value="peak-can">PEAK-System PCAN-USB</option>
              </select>
            </label>
            <div>
              <button style={{ padding: '8px 16px', background: 'var(--surface-subtle)', color: 'var(--text)', border: '1px solid var(--border-strong)', borderRadius: 'var(--radius)', fontSize: '14px', fontWeight: 500 }}>
                Run Adapter Characterization
              </button>
            </div>
          </div>
        </section>

        <section>
          <h3 style={{ fontSize: '14px', fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-tertiary)', borderBottom: '1px solid var(--border)', paddingBottom: '8px', marginBottom: '16px' }}>
            Workload Limits
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
            <label style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
              <span style={{ fontSize: '14px', fontWeight: 500 }}>Backend Poll Delay (ms)</span>
              <input type="number" value={pollDelay} onChange={e => setPollDelay(Number(e.target.value))} min={1} max={100} style={{ padding: '8px 12px', background: 'var(--surface-subtle)', border: '1px solid var(--border)', borderRadius: 'var(--radius)', fontSize: '14px', maxWidth: '120px' }} />
            </label>
          </div>
        </section>

        <section>
          <h3 style={{ fontSize: '14px', fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-tertiary)', borderBottom: '1px solid var(--border)', paddingBottom: '8px', marginBottom: '16px' }}>
            Appearance & Presentation
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
            <label style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
              <span style={{ fontSize: '14px', fontWeight: 500 }}>Theme</span>
              <select value={theme} onChange={e => setTheme(e.target.value)} style={{ padding: '8px 12px', background: 'var(--surface-subtle)', border: '1px solid var(--border)', borderRadius: 'var(--radius)', fontSize: '14px', maxWidth: '300px' }}>
                <option value="dark">Dark</option>
                <option value="light">Light</option>
              </select>
            </label>
            <label style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
              <span style={{ fontSize: '14px', fontWeight: 500 }}>Vehicle Visual Preview Mode</span>
              <select value={previewMode} onChange={e => setPreviewMode(e.target.value)} style={{ padding: '8px 12px', background: 'var(--surface-subtle)', border: '1px solid var(--border)', borderRadius: 'var(--radius)', fontSize: '14px', maxWidth: '300px' }}>
                <option value="overlay">Overlay</option>
                <option value="actuation-only">Actuation-only</option>
                <option value="sensors-only">Sensors-only</option>
              </select>
            </label>
          </div>
        </section>
      </div>
    </div>
  );
}
