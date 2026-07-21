import fs from 'node:fs'

function ensureImport(s, after, importLine) {
  if (s.includes('WorkspaceShell')) return s
  if (!s.includes(after)) {
    // fall back: after last import
    const lines = s.split(/\r?\n/)
    let last = 0
    for (let i = 0; i < lines.length; i++) if (lines[i].startsWith('import ')) last = i
    lines.splice(last + 1, 0, importLine)
    return lines.join('\n')
  }
  return s.replace(after, after + importLine + '\n')
}

function closeShell(s) {
  return s.replace(/\n    <\/div>\n  \)\n}\s*$/, '\n    </WorkspaceShell>\n  )\n}\n')
}

// Overview
{
  let s = fs.readFileSync('src/components/Overview.tsx', 'utf8')
  s = ensureImport(s, "import { MeterBar, MetricCard, StatusPill } from './primitives'\n", "import { WorkspaceShell } from './WorkspaceShell'")
  s = s.replace(
    /return \(\s*<div className="workspace" data-testid="workspace-overview">\s*<header className="ws-header">\s*<h1>Overview<\/h1>\s*<p className="muted">\s*Vehicle state and immediate health · session \{ses\?\.session_id \?\? 'none'\} ·\{' '\}\s*\{messages\.length\} live messages\s*<\/p>\s*<\/header>/,
    `return (
    <WorkspaceShell
      testId="workspace-overview"
      title="Overview"
      description={\`Vehicle state and immediate health · session \${ses?.session_id ?? 'none'} · \${messages.length} live messages\`}
    >`,
  )
  s = closeShell(s)
  fs.writeFileSync('src/components/Overview.tsx', s)
  console.log('Overview', s.includes('WorkspaceShell'))
}

// Network
{
  let s = fs.readFileSync('src/components/Network.tsx', 'utf8')
  s = ensureImport(s, "import { LivenessBadge } from './primitives'\n", "import { WorkspaceShell } from './WorkspaceShell'")
  s = s.replace(
    /return \(\s*<div className="workspace" data-testid="workspace-network">\s*<header className="ws-header">\s*<h1>Network<\/h1>\s*<p className="muted">\s*ECU topology and bus health · High and Low never collapsed into one lamp\s*<\/p>\s*<\/header>/,
    `return (
    <WorkspaceShell
      testId="workspace-network"
      title="Network"
      description="ECU topology and bus health · High and Low never collapsed into one lamp"
    >`,
  )
  s = closeShell(s)
  fs.writeFileSync('src/components/Network.tsx', s)
  console.log('Network', s.includes('WorkspaceShell'))
}

// Settings — read actual header text first-ish via loose match
{
  let s = fs.readFileSync('src/components/Settings.tsx', 'utf8')
  s = ensureImport(s, "import { useAppStore } from '../store'\n", "import { WorkspaceShell } from './WorkspaceShell'")
  s = s.replace(
    /return \(\s*<div className="workspace" data-testid="workspace-settings">\s*<header className="ws-header">\s*<h1>Settings<\/h1>\s*<p className="muted">[\s\S]*?<\/p>\s*<\/header>/,
    `return (
    <WorkspaceShell
      testId="workspace-settings"
      title="Settings"
      description={<>Session, transport, adapter, and protocol · live from <span className="mono">GET /api/v1/settings</span></>}
    >`,
  )
  s = closeShell(s)
  fs.writeFileSync('src/components/Settings.tsx', s)
  console.log('Settings', s.includes('<WorkspaceShell'))
}

// Logs
{
  let s = fs.readFileSync('src/components/Logs.tsx', 'utf8')
  s = ensureImport(s, "import { api } from '../api'\n", "import { WorkspaceShell } from './WorkspaceShell'")
  s = s.replace(
    /return \(\s*<div className="workspace" data-testid="workspace-logs">\s*<header className="ws-header">\s*<h1>Logging<\/h1>\s*<p className="muted">[\s\S]*?<\/p>\s*<\/header>/,
    `return (
    <WorkspaceShell
      testId="workspace-logs"
      title="Logging"
      description="Operational audit trail (architecture §7 / §14). Session, transport, control, and safety events."
    >`,
  )
  s = closeShell(s)
  fs.writeFileSync('src/components/Logs.tsx', s)
  console.log('Logs', s.includes('<WorkspaceShell'))
}

// Bench
{
  let s = fs.readFileSync('src/components/Bench.tsx', 'utf8')
  s = ensureImport(s, "import { useAppStore } from '../store'\n", "import { WorkspaceShell } from './WorkspaceShell'")
  s = s.replace(
    /return \(\s*<div className="workspace bench-workspace" data-testid="workspace-bench">\s*<header className="ws-header">\s*<h1>Bench<\/h1>\s*<p className="muted">[\s\S]*?<\/p>\s*<\/header>/,
    `return (
    <WorkspaceShell
      testId="workspace-bench"
      className="bench-workspace"
      title="Bench"
      description="Physical ECU under test and synthetic peers. Physical Bench TX is the safety gate for bus activity."
    >`,
  )
  s = closeShell(s)
  fs.writeFileSync('src/components/Bench.tsx', s)
  console.log('Bench', s.includes('<WorkspaceShell'))
}

// Diagnostics
{
  let s = fs.readFileSync('src/components/Diagnostics.tsx', 'utf8')
  s = ensureImport(s, "import { useAppStore } from '../store'\n", "import { WorkspaceShell } from './WorkspaceShell'")
  s = s.replace(
    /return \(\s*<div className="workspace diagnostics-workspace" data-testid="workspace-diagnostics">\s*<header className="ws-header">\s*<h1>Diagnostics<\/h1>\s*<p className="muted">[\s\S]*?<\/p>\s*<\/header>/,
    `return (
    <WorkspaceShell
      testId="workspace-diagnostics"
      className="diagnostics-workspace"
      title="Diagnostics"
      description="Protocol health, verification recipes, and episode capture."
    >`,
  )
  s = closeShell(s)
  fs.writeFileSync('src/components/Diagnostics.tsx', s)
  console.log('Diagnostics', s.includes('<WorkspaceShell'))
}

// Control
{
  let s = fs.readFileSync('src/components/Control.tsx', 'utf8')
  s = ensureImport(s, "import { NumericDraft } from './NumericDraft'\n", "import { WorkspaceShell } from './WorkspaceShell'")
  s = s.replace(
    /return \(\s*<div className="workspace control-workspace" data-testid="workspace-control">\s*<header className="ws-header">\s*<h1>Control<\/h1>\s*<p className="muted">\s*Pick <strong>one<\/strong> path below\. High and Low motion are exclusive \(backend\s*cancels the other\)\. HMI is mode\/power only — not drive\.\s*<\/p>\s*<\/header>/,
    `return (
    <WorkspaceShell
      testId="workspace-control"
      className="control-workspace"
      title="Control"
      description={<>Pick <strong>one</strong> path below. High and Low motion are exclusive (backend cancels the other). HMI is mode/power only — not drive.</>}
    >`,
  )
  s = closeShell(s)
  fs.writeFileSync('src/components/Control.tsx', s)
  console.log('Control', s.includes('<WorkspaceShell'))
}

// LiveCan
{
  let s = fs.readFileSync('src/components/LiveCan.tsx', 'utf8')
  s = ensureImport(s, "import { FreshnessBadge } from './FreshnessBadge'\n", "import { WorkspaceShell } from './WorkspaceShell'")
  s = s.replace(
    /return \(\s*<div\s+className=\{cn\(\s*'flex max-w-none flex-col gap-4 px-\[22px\] py-5',\s*'workspace live-layout',\s*\)\}\s+data-testid="workspace-live"\s*>\s*<header className="ws-header m-0">\s*<h1 className="m-0 text-\[22px\] font-bold tracking-tight text-\[var\(--text\)\]">\s*Live CAN\s*<\/h1>\s*<p className="muted mt-1 max-w-\[72ch\] text-\[13px\] text-\[var\(--text-secondary\)\]">\s*\{viewMode === 'latest'\s*\? `Latest-by-message · updates in place · \${filtered\.length} rows`\s*: `Chronological stream · \${chronoFiltered\.length} frames \(pause freezes rendering, not capture\)`\}\s*<\/p>\s*<\/header>/,
    `return (
    <WorkspaceShell
      testId="workspace-live"
      className="live-layout max-w-none"
      title="Live CAN"
      description={
        viewMode === 'latest'
          ? \`Latest-by-message · updates in place · \${filtered.length} rows\`
          : \`Chronological stream · \${chronoFiltered.length} frames (pause freezes rendering, not capture)\`
      }
    >`,
  )
  s = closeShell(s)
  fs.writeFileSync('src/components/LiveCan.tsx', s)
  console.log('LiveCan', s.includes('<WorkspaceShell'))
}

// CanDictionary / DriveConsole — className only (keep custom headers for complex UIs)
for (const [file] of [
  ['src/components/CanDictionary.tsx', 'dict-workspace'],
  ['src/components/DriveConsole.tsx', null],
]) {
  let s = fs.readFileSync(file, 'utf8')
  if (file.includes('CanDictionary')) {
    s = s.replace(
      'className="workspace dict-workspace"',
      'className="workspace dict-workspace flex max-w-[1600px] flex-col gap-4 px-[22px] py-5"',
    )
  }
  if (file.includes('DriveConsole')) {
    s = s.replace(
      /className=\{?["']workspace[^"']*["']\}?/,
      (m) => m, // leave if complex
    )
  }
  fs.writeFileSync(file, s)
  console.log('class tweak', file)
}

console.log('done')
