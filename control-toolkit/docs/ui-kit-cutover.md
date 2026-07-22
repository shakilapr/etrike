# UI kit cutover

**Status:** Foundations + density scale (2026-07-22)

## Rules

1. **Primary fill is opt-in** — only `Button` default, or `className="primary"` / `.btn`.
2. **Bare `<button>` is never auto-primary** (global catch-all removed).
3. **New UI uses `src/components/ui/*`** — Button, Input, Seg, Toolbar, ListRow, Panel, StatusDot.
4. **Gallery:** Workspace explorer → System → **UI kit** (`workspace-ui-kit`).
5. **One density scale** (do not invent heights):

| Token | Value | Use |
|-------|------:|-----|
| `--control-h` | 32px | Buttons, inputs, selects, seg, nav rows |
| `--control-h-dense` | 28px | Chips, compact stops |
| `--font-size-title` | 18px | Workspace `h1` |
| `--font-size-section` | 13px | Panel `h2` |
| `--font-size-body` | 13px | App body |
| `--font-size-ui` | 12px | Controls, nav |
| `--font-size-label` | 11px | Field labels, chips |
| `--font-size-meta` | 10.5px | Meta-k uppercase |

## Landed

- Inverted primary button CSS (opt-in only).
- Kit primitives: Button, ListRow/BusChip, Toolbar, Panel, StatusDot.
- Inject toolbar / Active TX rail / actions use kit components.
- Intentional primary classes restored on Control / Settings / Bench / Diagnostics / sidebar actions that relied on the old catch-all.
- UI kit gallery workspace.
- Global density: 32px controls, tighter titles/padding, unified nav/seg/topbar mode/input heights.
- **CSS split:** monolithic `App.css` → `src/styles/*.css` modules; `App.css` is import-only (see `styles/README.md`).

## Next (page-by-page)

1. Control + Drive → Button/Seg only.
2. Live CAN / Network tables stay specialized; chrome via kit.
3. Delete unused App.css islands as pages migrate.
4. Optional lint: ban bare `className="primary"` in favor of `Button`.
