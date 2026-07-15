# E-Trike Control Toolkit — UI Design Guidelines

For this engineering web software, the UI must be **calm, dense, precise, and predictable**—not “minimal” in the sense of hiding information.

A professional interface usually looks expensive because everything is aligned, consistent, and intentional—not because it uses gradients, glass effects, or large decorative cards. 

This document defines the visual and structural rules for the React frontend, building on the requirements in `architecture-control-toolkit.md`.

## 1. Design around work, not decoration

The screen should answer four questions immediately:

1. Where am I?
2. What object or system am I working on?
3. What is its current state?
4. What can I do next?

Use a stable application shell tailored to the Control Toolkit:

* **Top bar:** active profile (Full Vehicle, Bench Test, Pure Software), USB adapter state, bus activity, vehicle mode/power, ESTOP, and stream quality badge. *(Note: No user profiles or accounts are needed for this local bench tool).*
* **Left navigation (Fixed Sidebar):** Modules and major workspaces (Overview, Network, Live CAN, Control, Bench, CAN Dictionary, Diagnostics, Settings). This sidebar must remain **fixed** and stable.
* **Main workspace:** Live data tables, control panels, topology diagrams, or raw frame streams.
* **Optional right panel/drawer:** Message details, bit/byte maps, properties, logs.

Keep navigation stable between screens. Do not move primary controls around based on page content.

---

## 2. Use controlled information density

Engineering users often need more information visible, but density should come from good layout—not tiny text or miniature controls. Achieve density through layout decisions rather than simply shrinking components.

A strong default:

| Element                      | Suggested size |
| ---------------------------- | -------------: |
| Top application bar          |       48–56 px |
| Fixed Sidebar                |     224–256 px |
| Standard controls            |       36–40 px |
| Dense toolbar controls       |       32–36 px |
| Page padding                 |       24–32 px |
| Panel padding                |       16–24 px |
| Gap between related controls |        8–12 px |
| Gap between sections         |       24–32 px |

Use a **4 px base grid**, with most spacing using:

`4, 8, 12, 16, 24, 32, 48`

Avoid random values such as 13 px, 19 px, or 27 px unless there is a real reason.

---

## 3. Keep typography restrained

Use one interface typeface and one monospace typeface.

Example:

* UI: Inter, IBM Plex Sans, Source Sans 3
* Technical values: IBM Plex Mono, JetBrains Mono

Suggested hierarchy:

* Page title: 22–28 px, weight 600
* Section heading: 16–18 px, weight 600
* Normal interface text: 14–16 px
* Table text: 13–14 px
* Metadata: 12–13 px

Rules:

* Use sentence case: `Simulation settings` (Avoid: `SIMULATION SETTINGS`)
* Use bold sparingly
* Use monospace only for IDs, CAN payloads, code, hex values, and technical measurements.
* Use tabular numerals for changing measurements and metrics (crucial for live CAN data).

Keep long documentation text narrow enough to read comfortably (roughly 75 characters max).

---

## 4. Use mostly neutral colors

A clean engineering palette is usually:

* 80–90% neutral surfaces
* One primary accent
* Semantic colors for state

Example (Dark/Automotive Theme compatible):

* Background: dark/light neutral
* Workspace: contrasting solid surface
* Borders: cool neutral gray
* Primary action: restrained blue
* Success: green
* Warning: amber
* Error/Danger: red
* Informational: blue

Do not use status colors decoratively.

Red should mean failure, danger, invalid, offline, or destructive (e.g., ESTOP)—not “important.”

Never communicate state using color alone. Add text, shape, icon, or pattern:

✅ `● Operational`
❌ Green dot with no label

---

## 5. Prefer borders over shadows

For technical software:

* Use 1 px borders to separate panels
* Use background changes for hierarchy
* Reserve shadows for floating elements:
  * menus
  * dialogs
  * popovers
  * dragged objects

Avoid putting every section inside a floating card. Too many cards make technical software look like a startup analytics template rather than an engineering tool.

Use modest corner radii (e.g., Inputs: 4–6 px, Panels: 4–8 px). Avoid giant 16–24 px rounded corners.

---

## 6. Treat tables as first-class interfaces

The Control Toolkit succeeds or fails based on its tables (e.g., Live CAN, Diagnostics).

Provide:

* Sticky column headers
* Sorting & filtering
* Column resizing
* Clear selected-row state
* Virtualization for high-frequency or large datasets (mandatory for the Chronological monitor).

Alignment:

* Text → left
* Numbers → right
* Status → left or centered
* Actions → right

Put units in the column heading (e.g., `Speed, m/s`) rather than repeating them inside every cell, unless values use mixed units. Give tables enough horizontal space; avoid placing serious data tables inside modals.

---

## 7. Make forms explicit

For engineering inputs (e.g., Actuator Control, HMI overrides):

* Put labels above fields
* Show units beside values
* Show YAML-defined limits and constraints
* Use safe defaults
* Distinguish editable, read-only, and disabled values
* Preserve entered data after an error

Example:

```text
Target Steering Angle
[ 32.5              ] degrees

Allowed range: -90.0 to 90.0
```

Validate near the affected field. Tell the user what is wrong, why, and how to correct it.

---

## 8. Make actions predictable

Use one visually dominant action per region (e.g., `Inject Frame`, `Start Recording`).

Use verbs that describe the actual operation rather than generic terms like "Submit" or "Proceed."

Separate destructive actions (e.g., ESTOP, Delete Recording) from normal controls, and require confirmation only when consequences are serious.

---

## 9. Define every component state

Every interactive component should have deliberate states: Default, Hover, Focus, Active, Selected, Disabled, Read-only, Loading, Warning, Error, Success.

Keyboard focus must be clearly visible. Do not hide essential actions exclusively on hover.

---

## 10. Make system behavior visible

Engineering users need confidence that the system is doing what they asked.

Show:

* Injecting...
* Queued
* Recording active
* Connection lost / Retrying...
* Stream: LIVE / DELAYED / DROPPING

Never leave users wondering whether their click worked. When waiting for a frame to inject, the UI must reflect the exact `Submitted` vs `Failed` state from the backend.

---

## 11. Use status language consistently

Choose one vocabulary and enforce it throughout the product, especially for freshness and liveness:

```text
Live
Late
Missing
Frozen
Invalid
Recovering
```

Do not interchange terms like `Disconnected`, `Offline`, and `Quiet` carelessly.

---

## 12. Optimize for expert workflows

Professional engineering tools should support speed after users learn the system.

Useful features:

* Keyboard shortcuts (e.g., WASD for kinematics, Space for ESTOP)
* Command palette
* Saved view filters (e.g., "Show only MTR faults")
* Persistent workspace state (returning to the CAN Dictionary keeps your place)

Do not force expert users through wizards for routine bench testing operations.

---

## 13. Keep tooltips nonessential

Tooltips are useful for:

* Explaining unfamiliar icons
* Showing full values for truncated hex payloads
* Giving brief technical definitions from the CAN dictionary

Do not place critical safety instructions (like how to clear an ESTOP) only inside a tooltip.

---

## 14. Design responsive behavior intentionally

The Control Toolkit optimizes for desktop engineering workstations. 

At narrower widths, preserve the core workspace and data tables. For wide CAN data, horizontal scrolling is often more honest than destroying the table structure into dozens of unrelated mobile cards. The left sidebar should remain fixed, but secondary property panels can collapse.

---

## 15. Avoid these “cheap-looking” patterns

Avoid:

* Gradient backgrounds everywhere
* Glassmorphism and excessive shadows
* Huge rounded cards
* Giant empty dashboard cards
* Excessive animation (especially on live CAN gauges)
* Modals for normal navigation
* Toast messages containing important persistent errors

Professional engineering software should feel closer to a **well-designed instrument panel** than a marketing website.

---

## A strong visual baseline

```text
Background:       #F5F7F9 (or dark equivalent)
Primary surface:  #FFFFFF (or dark equivalent)
Primary text:     #1B1F23
Secondary text:   #5B6573
Border:           #D8DEE5
Primary action:   restrained blue

Base font:        14 px
Control height:   40 px
Dense control:    32–36 px
Border radius:    6 px
Panel padding:    20–24 px
Table cell:       12–16 px horizontal padding
Animation:        120–180 ms (only where necessary)
```

The main rule is: **Reduce visual noise, not useful information.** A clean engineering UI exposes complexity in a structured way instead of hiding it.
