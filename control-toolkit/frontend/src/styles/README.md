# Styles layout

`App.css` is only an **import barrel**. Edit the modules below — cascade order matters.

| File | Owns |
|------|------|
| `00-tokens-base.css` | `:root` tokens, density scale, reset, fonts |
| `10-topbar.css` | Top health bar, ECU rail, chips |
| `20-shell-sidebar.css` | App body, sidebar shell, activity bar |
| `21-control-toolbox.css` | Control activity sidebar |
| `22-sidebar-nav-vehicle.css` | Nav items, vehicle card, monitor list |
| `30-workspace.css` | Workspace shell, panels, headings |
| `31-safety-strip.css` | Overview safety strip |
| `32-meters.css` | Graphical meters |
| `33-metric-cards.css` | Metric cards |
| `40-tables.css` | Data tables |
| `41-freshness.css` | Freshness / liveness badges |
| `50-network.css` | Network topology |
| `51-live-can.css` | Live CAN workspace |
| `60-forms-buttons.css` | Buttons (primary opt-in), forms, actions |
| `70-inject.css` | Inject workspace |
| `80-dictionary.css` | Dictionary workspace |
| `90-logging.css` | Logging workspace |
| `91-drive-console.css` | Drive console |

**Rule:** Prefer `src/components/ui/*` for new controls. Add CSS here only for layout that cannot be a primitive yet. Do not reintroduce a global “all buttons are primary” rule (`60-forms-buttons.css`).
