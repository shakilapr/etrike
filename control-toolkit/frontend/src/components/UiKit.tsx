/**
 * Component gallery — design-system reference for dense engineering UI.
 * Open from Workspace explorer → System → UI kit.
 */
import { useState } from 'react'
import {
  Badge,
  BusChip,
  Button,
  Input,
  ListRow,
  ListRowMain,
  ListRowMeta,
  ListRowStop,
  Panel,
  PanelTitle,
  Seg,
  SegButton,
  StatusDot,
  Toolbar,
  ToolbarDivider,
  ToolbarGroup,
  ToolbarItem,
} from './ui'
import { WorkspaceShell } from './WorkspaceShell'

export function UiKit() {
  const [seg, setSeg] = useState<'a' | 'b'>('a')
  const [log, setLog] = useState('Ready')

  return (
    <WorkspaceShell
      testId="workspace-ui-kit"
      title="UI kit"
      description="Owned primitives only. Primary fill is opt-in via Button. New UI must use these — do not invent one-off button skins."
    >
      <Panel data-testid="ui-kit-rules">
        <PanelTitle>Rules</PanelTitle>
        <ul className="muted small m-0 pl-4 leading-relaxed">
          <li>
            Primary actions: <code className="mono">Button</code> default variant (or{' '}
            <code className="mono">className=&quot;primary&quot;</code> / <code className="mono">.btn</code>
            ).
          </li>
          <li>Bare <code className="mono">&lt;button&gt;</code> is never auto-primary.</li>
          <li>
            Dense lists: <code className="mono">ListRow</code> + <code className="mono">StatusDot</code>.
          </li>
          <li>Toolbars: <code className="mono">Toolbar</code> / <code className="mono">Seg</code>.</li>
        </ul>
      </Panel>

      <Panel data-testid="ui-kit-buttons">
        <PanelTitle>Button</PanelTitle>
        <div className="flex flex-wrap items-center gap-2">
          <Button data-testid="ui-kit-btn-primary" onClick={() => setLog('primary')}>
            Primary
          </Button>
          <Button variant="secondary" onClick={() => setLog('secondary')}>
            Secondary
          </Button>
          <Button variant="danger" onClick={() => setLog('danger')}>
            Danger
          </Button>
          <Button variant="ghost" onClick={() => setLog('ghost')}>
            Ghost
          </Button>
          <Button variant="outline" size="dense" onClick={() => setLog('outline')}>
            Outline dense
          </Button>
          <Button size="sm" disabled>
            Disabled
          </Button>
        </div>
        <p className="muted small mt-2 mono">last: {log}</p>
      </Panel>

      <Panel data-testid="ui-kit-toolbar">
        <PanelTitle>Toolbar + Seg</PanelTitle>
        <Toolbar>
          <ToolbarGroup>
            <ToolbarItem label="TX">
              <strong className="ok-text">Armed</strong>
            </ToolbarItem>
            <ToolbarDivider />
            <Seg>
              <SegButton active={seg === 'a'} onClick={() => setSeg('a')}>
                Named
              </SegButton>
              <SegButton active={seg === 'b'} onClick={() => setSeg('b')}>
                Raw
              </SegButton>
            </Seg>
          </ToolbarGroup>
          <ToolbarGroup>
            <ToolbarItem label="Wire">
              <span className="mono text-primary text-[11px]">[0x00, 0xFF]</span>
            </ToolbarItem>
            <Button variant="secondary" size="sm">
              Stop all
            </Button>
          </ToolbarGroup>
        </Toolbar>
      </Panel>

      <Panel data-testid="ui-kit-list">
        <PanelTitle trailing={<Badge tone="ok">2</Badge>}>ListRow</PanelTitle>
        <ListRow>
          <ListRowMain type="button" onClick={() => setLog('row1')}>
            <BusChip bus="high" />
            <ListRowMeta>
              <span className="mono font-semibold">0x300</span>
              <span className="text-text-secondary truncate">HOST_DRIVE_CMD</span>
              <span className="mono text-text-tertiary text-[10px]">10 ms · ok</span>
            </ListRowMeta>
          </ListRowMain>
          <ListRowStop onClick={() => setLog('stop1')}>Stop</ListRowStop>
        </ListRow>
        <ListRow>
          <ListRowMain type="button" onClick={() => setLog('row2')}>
            <BusChip bus="low" />
            <ListRowMeta>
              <span className="mono font-semibold">0x7FE</span>
              <span className="text-text-secondary truncate">SYS_HEARTBEAT</span>
              <span className="mono text-text-tertiary text-[10px]">100 ms · ok</span>
            </ListRowMeta>
          </ListRowMain>
          <ListRowStop onClick={() => setLog('stop2')}>Stop</ListRowStop>
        </ListRow>
      </Panel>

      <Panel data-testid="ui-kit-status">
        <PanelTitle>StatusDot + Input</PanelTitle>
        <div className="mb-3 flex flex-wrap items-center gap-4 text-xs">
          <span className="inline-flex items-center gap-1.5">
            <StatusDot tone="live" /> live
          </span>
          <span className="inline-flex items-center gap-1.5">
            <StatusDot tone="warning" /> warning
          </span>
          <span className="inline-flex items-center gap-1.5">
            <StatusDot tone="danger" /> dead
          </span>
          <span className="inline-flex items-center gap-1.5">
            <StatusDot tone="muted" /> muted
          </span>
        </div>
        <Input placeholder="Filter…" className="max-w-xs" />
      </Panel>
    </WorkspaceShell>
  )
}
