/**
 * Drive console — see + control over CAN.
 *
 * UX patterns from:
 *  - tricycle_kinematics_simulator.html (Adaptive/Direct, ego canvas, gear N/D/S/R)
 *  - leadmate robot_control (keycaps, arm lock, gauges, speed sliders, ESTOP)
 *
 * Transport is CAN (virtual/physical via backend), not ROS cmd_vel.
 */
import { useCallback, useEffect, useRef, useState, type PointerEvent as ReactPointerEvent } from 'react'
import { useAppStore, type MessageState } from '../store'
import { api } from '../api'

type Gear = 'N' | 'D' | 'S' | 'R'
type ShiftMode = 'smart' | 'direct'

type SimState = {
  x: number
  y: number
  theta: number
  v: number
  alpha: number
  omega: number
  isBraking: boolean
  brakePressureKpa: number
  isEstop: boolean
  gear: Gear
  shiftMode: ShiftMode
}

type Hud = {
  speedMmps: number
  yawMradS: number
  thetaDeg: number
  alphaDeg: number
  brakeKpa: number
  radiusText: string
  dynClampDeg: number
  dynSlewDegS: number
}

const PIXELS_PER_METER = 100
const L = 120
const W = 80
const FRICTION = 0.98
const GEARS: Gear[] = ['N', 'D', 'S', 'R']
const GEAR_NUM: Record<Gear, number> = { N: 0, D: 1, S: 2, R: 3 }

const INITIAL: SimState = {
  x: 0,
  y: 0,
  theta: -Math.PI / 2,
  v: 0,
  alpha: 0,
  omega: 0,
  isBraking: false,
  brakePressureKpa: 0,
  isEstop: false,
  gear: 'N',
  shiftMode: 'smart',
}

function numSignal(m: MessageState | undefined, key: string): number | null {
  const v = m?.signals?.[key]?.engineering_value
  if (v == null || v === '') return null
  const n = Number(v)
  return Number.isFinite(n) ? n : null
}

function gearFromCan(m: MessageState | undefined): Gear | null {
  if (!m) return null
  const raw = m.signals?.gear
  if (!raw) return null
  const label = String(raw.enum_label ?? raw.engineering_value ?? '').toUpperCase()
  if (label === 'N' || label === 'D' || label === 'S' || label === 'R') return label
  if (label === 'P') return 'N'
  const n = Number(raw.engineering_value)
  if (n === 0) return 'N'
  if (n === 1) return 'D'
  if (n === 2) return 'S'
  if (n === 3) return 'R'
  return null
}

function computeDynamicLimits(speedPxS: number) {
  const speedKmh = Math.abs(speedPxS / PIXELS_PER_METER) * 3.6
  let limitDeg = 40 - (speedKmh - 2) * (35 / 23)
  limitDeg = Math.max(5, Math.min(40, limitDeg))
  let rateDegS = 125 + (speedKmh - 2) * (400 / 23)
  rateDegS = Math.max(125, Math.min(525, rateDegS))
  return {
    maxAlphaDeg: limitDeg,
    maxAlphaRad: (limitDeg * Math.PI) / 180,
    steerRateDegS: rateDegS,
    steerRateRadS: (rateDegS * Math.PI) / 180,
  }
}

function Gauge({
  label,
  value,
  unit,
  max,
  tone = 'accent',
}: {
  label: string
  value: number
  unit: string
  max: number
  tone?: 'accent' | 'warn' | 'ok' | 'danger'
}) {
  const pct = Math.min(100, (Math.abs(value) / Math.max(1, max)) * 100)
  // Brake (and similar): high fill → danger red; mid → warn
  let t = tone
  if (tone === 'danger') {
    t = pct >= 70 ? 'danger' : pct >= 35 ? 'warn' : 'ok'
  }
  return (
    <div className={`drive-gauge tone-${t}`} data-testid={`gauge-${label}`}>
      <div className="drive-gauge-label">{label}</div>
      <div className="drive-gauge-value mono">
        {Number.isFinite(value) ? value.toFixed(0) : '—'}
        <span className="unit">{unit}</span>
      </div>
      <div className="drive-gauge-bar">
        <div className="drive-gauge-fill" style={{ width: `${pct}%` }} />
      </div>
    </div>
  )
}

/** @deprecated name kept as VehiclePreview for App import stability */
export function VehiclePreview() {
  return <DriveConsole />
}

export function DriveConsole() {
  const messages = useAppStore((s) => s.messages)
  const status = useAppStore((s) => s.status)
  const setStatus = useAppStore((s) => s.setStatus)
  const quality = useAppStore((s) => s.streamQuality)

  const rootRef = useRef<HTMLDivElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const wrapRef = useRef<HTMLDivElement>(null)
  const stateRef = useRef<SimState>({ ...INITIAL })
  const keysRef = useRef<Record<string, boolean>>({})
  const lastTimeRef = useRef(performance.now())
  const rafRef = useRef(0)
  const seqRef = useRef(0)
  const armedRef = useRef(false)
  const gearRef = useRef<Gear>('N')
  const shiftRef = useRef<ShiftMode>('smart')
  /** Coalesce intent POSTs so out-of-order 409s don't spam the log. */
  const intentInFlightRef = useRef(false)
  const intentQueuedRef = useRef(false)
  /** Latest backend-shaped command for canvas fallback when bus decode is lagging. */
  const shapedRef = useRef({ speed: 0, yaw: 0, gear: 0 as number })

  const [armed, setArmed] = useState(false)
  const [busy, setBusy] = useState(false)
  const [log, setLog] = useState('')
  const [shiftMode, setShiftMode] = useState<ShiftMode>('smart')
  const [gear, setGear] = useState<Gear>('N')
  // Firmware-aligned caps (shared_config / host.yaml): speed ≤ 3000, yaw ≤ 3000.
  const [maxSpeedMmps, setMaxSpeedMmps] = useState(3000)
  const [maxYawMrad, setMaxYawMrad] = useState(3000)
  const [keyUi, setKeyUi] = useState<Record<string, boolean>>({})
  const [ctrlSnap, setCtrlSnap] = useState<Record<string, unknown> | null>(null)
  const [focused, setFocused] = useState(false)
  const [hud, setHud] = useState<Hud>({
    speedMmps: 0,
    yawMradS: 0,
    thetaDeg: 0,
    alphaDeg: 0,
    brakeKpa: 0,
    radiusText: 'Straight (∞)',
    dynClampDeg: 40,
    dynSlewDegS: 125,
  })

  armedRef.current = armed
  gearRef.current = gear
  shiftRef.current = shiftMode

  const driveMsg = messages.find((m) => m.name === 'HOST_DRIVE_CMD')
  const canLive = driveMsg?.freshness?.toLowerCase() === 'live'
  const canSpeed = numSignal(driveMsg, 'speed_mmps')
  const canYaw = numSignal(driveMsg, 'yaw_rate_mrad_s')
  const canGear = gearFromCan(driveMsg)

  const applyGear = useCallback((g: Gear) => {
    stateRef.current.gear = g
    setGear(g)
  }, [])

  const focusDrive = useCallback(() => {
    wrapRef.current?.focus({ preventScroll: true })
    setFocused(true)
  }, [])

  const clearKeys = useCallback(() => {
    keysRef.current = {}
    setKeyUi({})
  }, [])

  useEffect(() => {
    stateRef.current.shiftMode = shiftMode
  }, [shiftMode])
  useEffect(() => {
    stateRef.current.gear = gear
  }, [gear])

  async function ensureArmedPath() {
    let st = await api.status()
    if (!st.session?.session_id) {
      throw new Error('No active session. Start Computer or connect Real in Settings first.')
    }
    if (st.session.bench_tx !== 'enabled') {
      throw new Error(
        'Bench TX is off. Enable Bench TX explicitly (Control or Settings) before arming Drive.',
      )
    }
    setStatus(st)
    return st
  }

  async function armControl() {
    setBusy(true)
    try {
      await ensureArmedPath()
      const { cleanupControlStreams } = await import('../lib/cleanup')
      const clean = await cleanupControlStreams('drive_arm')
      seqRef.current = 0
      // Usable default: leave Adaptive free; in Direct start in Drive gear.
      if (shiftRef.current === 'direct' && gearRef.current === 'N') {
        applyGear('D')
      }
      setArmed(true)
      focusDrive()
      setLog(
        'Armed: keys/keycaps → HOST_DRIVE_CMD on High bus @ 10 ms. Leaving this tab disarms. Canvas follows bus.' +
          (clean.ok ? '' : ` · ${clean.detail}`),
      )
    } catch (e) {
      setLog(String(e))
      setArmed(false)
    } finally {
      setBusy(false)
    }
  }

  const disarmControl = useCallback(async (reason = 'disarm') => {
    setArmed(false)
    clearKeys()
    try {
      await api.controlRelease(reason)
    } catch (e) {
      setLog(`Local control disarmed, but backend release failed: ${String(e)}`)
      return
    }
    setLog(`Disarmed (${reason}). Keyboard drives local sim only.`)
  }, [clearKeys])

  async function fireEstop() {
    setBusy(true)
    setArmed(false)
    clearKeys()
    applyGear('N')
    try {
      await ensureArmedPath()
      seqRef.current += 1
      const r = await api.controlIntent({
        sequence: seqRef.current,
        throttle: 0,
        steer: 0,
        estop: true,
      })
      setCtrlSnap(r.control)
      setLog('ESTOP: SAFETY_ESTOP on high+low CAN')
      setStatus(await api.status())
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  /** True when Drive tab owns keyboard (canvas, side panel, or buttons). */
  const driveOwnsKeyboard = useCallback(() => {
    const root = rootRef.current
    if (!root) return false
    if (root.matches(':focus-within')) return true
    const ae = document.activeElement
    if (ae && root.contains(ae)) return true
    // After click on non-focusable area, body may be active but user is on this tab.
    return focused && (ae === document.body || ae === document.documentElement)
  }, [focused])

  // Keyboard: entire Drive workspace, not only the canvas wrap.
  useEffect(() => {
    const DRIVE_CODES = [
      'ArrowUp',
      'ArrowDown',
      'ArrowLeft',
      'ArrowRight',
      'Space',
      'KeyW',
      'KeyA',
      'KeyS',
      'KeyD',
      'KeyQ',
      'KeyE',
      'ShiftLeft',
      'ShiftRight',
    ]

    const onDown = (e: KeyboardEvent) => {
      if (e.repeat) return
      if (!driveOwnsKeyboard()) return
      // Don't steal typing from inputs/selects/sliders in the side panel.
      const t = e.target as HTMLElement | null
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT')) {
        return
      }
      keysRef.current[e.code] = true
      if (DRIVE_CODES.includes(e.code)) e.preventDefault()
      if (e.code === 'KeyQ') {
        const idx = GEARS.indexOf(gearRef.current)
        if (idx > 0) applyGear(GEARS[idx - 1])
      }
      if (e.code === 'KeyE') {
        const idx = GEARS.indexOf(gearRef.current)
        if (idx < GEARS.length - 1) applyGear(GEARS[idx + 1])
      }
      setKeyUi({ ...keysRef.current })
    }
    const onUp = (e: KeyboardEvent) => {
      if (!keysRef.current[e.code] && !driveOwnsKeyboard()) return
      keysRef.current[e.code] = false
      setKeyUi({ ...keysRef.current })
    }
    // Safety: leave the browser tab → disarm. Do NOT disarm on window blur alone
    // (DevTools / multi-monitor focus was killing control mid-drive).
    const onVis = () => {
      if (document.hidden) {
        clearKeys()
        if (armedRef.current) void disarmControl('tab_hidden')
      }
    }
    window.addEventListener('keydown', onDown)
    window.addEventListener('keyup', onUp)
    document.addEventListener('visibilitychange', onVis)
    return () => {
      window.removeEventListener('keydown', onDown)
      window.removeEventListener('keyup', onUp)
      document.removeEventListener('visibilitychange', onVis)
      // Leaving Drive tab always disarms High-bus intent (safety).
      void api.controlRelease('left_drive_tab').catch(() => undefined)
    }
  }, [applyGear, clearKeys, disarmControl, driveOwnsKeyboard])

  // Armed: publish intent at 20 Hz; coalesce so only one POST is in flight.
  useEffect(() => {
    if (!armed) return

    const sendLatest = () => {
      if (intentInFlightRef.current) {
        intentQueuedRef.current = true
        return
      }
      const k = keysRef.current
      let throttle = 0
      let steer = 0
      if (k.KeyW || k.ArrowUp) throttle += 1
      if (k.KeyS || k.ArrowDown) throttle -= 1
      if (k.KeyA || k.ArrowLeft) steer -= 1
      if (k.KeyD || k.ArrowRight) steer += 1
      // Scale by sliders as fraction of firmware max authority.
      const speedFrac = Math.min(1, maxSpeedMmps / 3000)
      const yawFrac = Math.min(1, maxYawMrad / 3000)
      throttle *= speedFrac
      steer *= yawFrac

      let gearNum = GEAR_NUM[gearRef.current]
      if (shiftRef.current === 'smart') {
        if (throttle > 0 && gearNum === 0) gearNum = 1
        if (throttle < 0) gearNum = 3
      }
      const hard_brake = !!(k.ShiftLeft || k.ShiftRight)
      const estop = !!k.Space
      seqRef.current += 1
      const seq = seqRef.current
      intentInFlightRef.current = true
      intentQueuedRef.current = false
      void api
        .controlIntent({
          sequence: seq,
          source: 'drive_console',
          mode: 'kinematics',
          throttle,
          steer,
          gear: gearNum,
          hard_brake,
          estop,
        })
        .then((r) => {
          setCtrlSnap(r.control)
          shapedRef.current = {
            speed: Number(r.control?.shaped_speed_mmps ?? 0) || 0,
            yaw: Number(r.control?.shaped_yaw_mrad_s ?? 0) || 0,
            gear: Number(r.control?.gear ?? 0) || 0,
          }
          if (estop) {
            setArmed(false)
            applyGear('N')
            clearKeys()
            setLog('ESTOP via Space — control released')
          }
        })
        .catch((e) => {
          const msg = String(e)
          // Only an actual stale-sequence race is ignorable. Session, ownership,
          // and TX-gate conflicts mean authoritative control was lost.
          if (!/stale[_ ]sequence/i.test(msg)) {
            setArmed(false)
            clearKeys()
            applyGear('N')
            setLog(`Control lost: ${msg}`)
            void api.status().then(setStatus).catch(() => undefined)
          }
        })
        .finally(() => {
          intentInFlightRef.current = false
          if (intentQueuedRef.current && armedRef.current) sendLatest()
        })
    }

    const id = window.setInterval(sendLatest, 50)
    return () => {
      window.clearInterval(id)
      intentQueuedRef.current = false
      void api.controlRelease('disable').catch(() => undefined)
    }
  }, [armed, maxSpeedMmps, maxYawMrad, applyGear, clearKeys])

  // Canvas + physics
  useEffect(() => {
    const canvas = canvasRef.current
    const wrap = wrapRef.current
    if (!canvas || !wrap) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    function resize() {
      if (!canvas || !wrap) return
      const r = wrap.getBoundingClientRect()
      const dpr = window.devicePixelRatio || 1
      canvas.width = Math.max(1, Math.floor(r.width * dpr))
      canvas.height = Math.max(1, Math.floor(r.height * dpr))
      canvas.style.width = `${r.width}px`
      canvas.style.height = `${r.height}px`
      ctx!.setTransform(dpr, 0, 0, dpr, 0, 0)
    }
    resize()
    const ro = new ResizeObserver(resize)
    ro.observe(wrap)

    function maxVPx() {
      return (maxSpeedMmps / 1000) * PIXELS_PER_METER
    }
    function maxAlphaRad() {
      return (maxYawMrad / 1000) * 15 * (Math.PI / 180)
    }
    function accel() {
      return maxVPx() / 1.5
    }

    function updateLocal(dt: number) {
      const state = stateRef.current
      const keys = keysRef.current
      const pressingUp = !!(keys.ArrowUp || keys.KeyW)
      const pressingDown = !!(keys.ArrowDown || keys.KeyS)
      const MAX_V_PX = maxVPx()
      const ACCEL = accel()
      state.isEstop = !!keys.Space

      if (state.shiftMode === 'smart') {
        if (keys.Space) {
          if (state.gear !== 'N') applyGear('N')
        } else if (state.v === 0) {
          if (pressingUp && state.gear !== 'D' && state.gear !== 'S') applyGear('D')
          if (pressingDown && state.gear !== 'R') applyGear('R')
        } else if (state.v > 0 && (state.gear === 'D' || state.gear === 'S')) {
          const thr = MAX_V_PX * 0.5
          if (state.v > thr && state.gear === 'D') applyGear('S')
          else if (state.v < thr - 20 && state.gear === 'S') applyGear('D')
        }
        state.isBraking =
          !!keys.Space || (state.v > 0 && pressingDown) || (state.v < 0 && pressingUp)
        if (state.gear === 'N') {
          state.v *= FRICTION
          if (Math.abs(state.v) < 1) state.v = 0
        } else if (state.gear === 'R') {
          if (pressingDown) state.v -= ACCEL * dt
          if (pressingUp) {
            if (state.v < 0) state.v += ACCEL * dt
            if (state.v > 0) state.v = 0
          }
        } else {
          const a = state.gear === 'S' ? ACCEL * 1.5 : ACCEL
          const mx = state.gear === 'S' ? MAX_V_PX * 1.5 : MAX_V_PX
          if (pressingUp) state.v += a * dt
          if (pressingDown) {
            if (state.v > 0) state.v -= ACCEL * dt
            if (state.v < 0) state.v = 0
          }
          if (state.v > mx) state.v = Math.max(mx, state.v - ACCEL * dt)
        }
      } else {
        if (keys.Space && state.gear !== 'N') applyGear('N')
        state.isBraking = !!keys.Space || pressingDown
        if (state.gear === 'N') {
          state.v *= FRICTION
          if (Math.abs(state.v) < 1) state.v = 0
        } else if (state.gear === 'R') {
          if (pressingUp) state.v -= ACCEL * dt
          if (pressingDown) {
            if (state.v < 0) state.v += ACCEL * dt
            if (state.v > 0) state.v = 0
          }
          if (state.v < -MAX_V_PX) state.v = -MAX_V_PX
        } else {
          const a = state.gear === 'S' ? ACCEL * 1.5 : ACCEL
          const mx = state.gear === 'S' ? MAX_V_PX * 1.5 : MAX_V_PX
          if (pressingUp) state.v += a * dt
          if (pressingDown) {
            if (state.v > 0) state.v -= ACCEL * dt
            if (state.v < 0) state.v = 0
          }
          if (state.v > mx) state.v = Math.max(mx, state.v - ACCEL * dt)
        }
      }

      if (!pressingUp && !pressingDown) {
        state.v *= FRICTION
        if (Math.abs(state.v) < 1) state.v = 0
      }
      if (keys.Space) {
        state.v *= 0.8
        state.brakePressureKpa = Math.min(state.brakePressureKpa + 25000 * dt, 5000)
      } else if (state.isBraking) {
        state.brakePressureKpa = Math.min(state.brakePressureKpa + 15000 * dt, 2000)
      } else {
        state.brakePressureKpa = Math.max(state.brakePressureKpa - 20000 * dt, 0)
      }

      const dyn = computeDynamicLimits(state.v)
      if (keys.ArrowLeft || keys.KeyA) state.alpha -= dyn.steerRateRadS * dt
      if (keys.ArrowRight || keys.KeyD) state.alpha += dyn.steerRateRadS * dt
      if (!keys.ArrowLeft && !keys.ArrowRight && !keys.KeyA && !keys.KeyD) state.alpha *= 0.95
      const maxA = Math.min(maxAlphaRad(), dyn.maxAlphaRad)
      state.alpha = Math.max(-maxA, Math.min(maxA, state.alpha))
      state.v = Math.max(-MAX_V_PX, Math.min(MAX_V_PX * 1.5, state.v))
      state.omega = (state.v / L) * Math.tan(state.alpha)
      state.theta += state.omega * dt
      if (state.theta > Math.PI * 2) state.theta -= Math.PI * 2
      if (state.theta < 0) state.theta += Math.PI * 2
      state.x += state.v * Math.cos(state.theta) * dt
      state.y += state.v * Math.sin(state.theta) * dt
    }

    function updateFromCan(dt: number) {
      const state = stateRef.current
      const msgs = useAppStore.getState().messages
      const drive = msgs.find((m) => m.name === 'HOST_DRIVE_CMD')
      // Prefer live bus decode; fall back to last shaped intent so canvas moves immediately on arm+W.
      const fromBusSpeed = numSignal(drive, 'speed_mmps')
      const fromBusYaw = numSignal(drive, 'yaw_rate_mrad_s')
      const speedMmps = fromBusSpeed ?? shapedRef.current.speed
      const yawMrad = fromBusYaw ?? shapedRef.current.yaw
      const g = gearFromCan(drive)
      // While armed, `gear` is the operator's next command. CAN gear remains
      // visible through displayGear but must not overwrite a freshly selected
      // R/N/D/S value before the next intent tick.
      if (!armedRef.current) {
        if (g && g !== state.gear) applyGear(g)
        else if (!g && shapedRef.current.gear) {
          const map: Gear[] = ['N', 'D', 'S', 'R']
          const sg = map[shapedRef.current.gear]
          if (sg && sg !== state.gear) applyGear(sg)
        }
      }
      const targetV = (speedMmps / 1000) * PIXELS_PER_METER
      const omega = yawMrad / 1000
      let targetAlpha = 0
      if (Math.abs(targetV) > 1) targetAlpha = Math.atan((omega * L) / targetV)
      const dyn = computeDynamicLimits(state.v)
      const MAX_ALPHA = Math.min(maxAlphaRad(), dyn.maxAlphaRad)
      targetAlpha = Math.max(-MAX_ALPHA, Math.min(MAX_ALPHA, targetAlpha))
      state.v += (targetV - state.v) * Math.min(1, 8 * dt)
      state.alpha += (targetAlpha - state.alpha) * Math.min(1, 10 * dt)
      state.isBraking = Math.abs(targetV) < Math.abs(state.v) * 0.5 && Math.abs(state.v) > 5
      state.brakePressureKpa = state.isBraking
        ? Math.min(state.brakePressureKpa + 8000 * dt, 2000)
        : Math.max(state.brakePressureKpa - 15000 * dt, 0)
      state.omega = (state.v / L) * Math.tan(state.alpha)
      state.theta += state.omega * dt
      if (state.theta > Math.PI * 2) state.theta -= Math.PI * 2
      if (state.theta < 0) state.theta += Math.PI * 2
      state.x += state.v * Math.cos(state.theta) * dt
      state.y += state.v * Math.sin(state.theta) * dt
    }

    function draw(w: number, h: number) {
      const state = stateRef.current
      // Light stage matching control-ui robot-preview-stage (#f8fafc)
      ctx!.fillStyle = '#f8fafc'
      ctx!.fillRect(0, 0, w, h)
      const egoY = h * 0.75
      const grid = 50
      const ox = w / 2 - state.x
      const oy = egoY - state.y
      ctx!.strokeStyle = '#e2e8f0'
      ctx!.lineWidth = 1
      ctx!.beginPath()
      for (let x = (ox % grid) - grid; x < w; x += grid) {
        ctx!.moveTo(x, 0)
        ctx!.lineTo(x, h)
      }
      for (let y = (oy % grid) - grid; y < h; y += grid) {
        ctx!.moveTo(0, y)
        ctx!.lineTo(w, y)
      }
      ctx!.stroke()

      ctx!.save()
      ctx!.translate(w / 2, egoY)
      ctx!.rotate(state.theta)
      if (Math.abs(state.alpha) > 0.01) {
        const R = L / Math.tan(state.alpha)
        ctx!.strokeStyle = 'rgba(124, 58, 237, 0.4)'
        ctx!.setLineDash([8, 4])
        ctx!.beginPath()
        ctx!.moveTo(0, 0)
        ctx!.lineTo(0, R)
        ctx!.moveTo(L, 0)
        ctx!.lineTo(0, R)
        ctx!.stroke()
        ctx!.setLineDash([])
        ctx!.fillStyle = '#7c3aed'
        ctx!.beginPath()
        ctx!.arc(0, R, 4, 0, Math.PI * 2)
        ctx!.fill()
      }
      ctx!.fillStyle = 'rgba(255, 255, 255, 0.92)'
      ctx!.strokeStyle = '#64748b'
      ctx!.lineWidth = 2
      if (typeof ctx!.roundRect === 'function') {
        ctx!.beginPath()
        ctx!.roundRect(-30, -(W + 40) / 2, L + 40, W + 40, 10)
        ctx!.fill()
        ctx!.stroke()
      } else {
        ctx!.fillRect(-30, -(W + 40) / 2, L + 40, W + 40)
      }
      ctx!.fillStyle = state.isBraking ? '#ba2d36' : '#94a3b8'
      ctx!.fillRect(-32, -(W + 40) / 2 + 10, 6, 20)
      ctx!.fillRect(-32, (W + 40) / 2 - 30, 6, 20)
      ctx!.fillStyle = '#21845a'
      ctx!.fillRect(-17, -W / 2 - 7, 34, 14)
      ctx!.fillRect(-17, W / 2 - 7, 34, 14)
      ctx!.save()
      ctx!.translate(L, 0)
      ctx!.rotate(state.alpha)
      ctx!.fillStyle = '#ba2d36'
      ctx!.fillRect(-17, -7, 34, 14)
      ctx!.restore()
      ctx!.restore()
    }

    let hudTick = 0
    function loop(ts: number) {
      const dt = (ts - lastTimeRef.current) / 1000
      lastTimeRef.current = ts
      if (dt < 0.1) {
        if (armedRef.current) updateFromCan(dt)
        else updateLocal(dt)
      }
      const r = wrap!.getBoundingClientRect()
      draw(r.width, r.height)
      hudTick++
      if (hudTick % 3 === 0) {
        const s = stateRef.current
        const speedMmps = (s.v / PIXELS_PER_METER) * 1000
        setHud({
          speedMmps,
          yawMradS: s.omega * 1000,
          thetaDeg: (((s.theta * 180) / Math.PI) % 360) - ((((s.theta * 180) / Math.PI) % 360) > 180 ? 360 : 0),
          alphaDeg: (s.alpha * 180) / Math.PI,
          brakeKpa: s.brakePressureKpa,
          radiusText:
            Math.abs(s.alpha) < 0.01
              ? 'Straight (∞)'
              : `${Math.abs(L / Math.tan(s.alpha)).toFixed(1)} px`,
          dynClampDeg: computeDynamicLimits(s.v).maxAlphaDeg,
          dynSlewDegS: computeDynamicLimits(s.v).steerRateDegS,
        })
      }
      rafRef.current = requestAnimationFrame(loop)
    }
    rafRef.current = requestAnimationFrame(loop)
    return () => {
      cancelAnimationFrame(rafRef.current)
      ro.disconnect()
    }
  }, [applyGear, maxSpeedMmps, maxYawMrad])

  const k = keyUi
  const benchOn = status?.session?.bench_tx === 'enabled'
  const fullVehicle = (status?.session?.profile ?? status?.profile) === 'full_vehicle'
  const displaySpeed = armed && canSpeed != null ? canSpeed : hud.speedMmps
  const displayYaw = armed && canYaw != null ? canYaw : hud.yawMradS
  const displayGear = armed && canGear ? canGear : gear
  const shapedSpeed =
    ctrlSnap?.shaped_speed_mmps != null ? Number(ctrlSnap.shaped_speed_mmps) : null
  const shapedYaw =
    ctrlSnap?.shaped_yaw_mrad_s != null ? Number(ctrlSnap.shaped_yaw_mrad_s) : null

  useEffect(() => {
    if (fullVehicle && armedRef.current) void disarmControl('full_vehicle_profile')
  }, [fullVehicle, disarmControl])

  /** Hold virtual key (pointer) — works while armed or local sim. */
  function pressVirtual(code: string, down: boolean) {
    if (code === 'KeyQ' && down) {
      const idx = GEARS.indexOf(gearRef.current)
      if (idx > 0) applyGear(GEARS[idx - 1])
      return
    }
    if (code === 'KeyE' && down) {
      const idx = GEARS.indexOf(gearRef.current)
      if (idx < GEARS.length - 1) applyGear(GEARS[idx + 1])
      return
    }
    keysRef.current[code] = down
    setKeyUi({ ...keysRef.current })
    setFocused(true)
  }

  function keycapHandlers(code: string) {
    return {
      onPointerDown: (e: ReactPointerEvent<HTMLButtonElement>) => {
        e.preventDefault()
        // Real pointers support capture; Playwright dispatchEvent / some browsers throw
        // InvalidPointerId when no active pointer exists — never block the press.
        try {
          if (typeof e.pointerId === 'number' && e.pointerId >= 0) {
            e.currentTarget.setPointerCapture(e.pointerId)
          }
        } catch {
          /* ignore InvalidPointerId / NotFoundError from synthetic events */
        }
        pressVirtual(code, true)
      },
      onPointerUp: () => pressVirtual(code, false),
      onPointerCancel: () => pressVirtual(code, false),
      onLostPointerCapture: () => pressVirtual(code, false),
    }
  }

  return (
    <div
      className={`workspace drive-console${focused ? ' drive-focused' : ''}${armed ? ' drive-armed' : ''}`}
      data-testid="workspace-preview"
      ref={rootRef}
      onPointerDownCapture={() => setFocused(true)}
    >
      <header className="drive-topbar" data-testid="drive-topbar">
        <div className="drive-top-title">
          <h1>Drive</h1>
          <p className="muted small">
            High-bus Host kinematics (<span className="mono">HOST_DRIVE_CMD 0x300</span>). For
            Low-bus motor/steer/brake unit tests use <strong>Control → Low bus</strong>.
          </p>
        </div>
        <div className="drive-top-chips" data-testid="drive-status-chips">
          <span className={`chip quality-${quality}`} title="WebSocket stream quality">
            <span className="chip-k">Stream</span>
            <span className="chip-v">
              {quality === 'live'
                ? 'Live'
                : quality === 'delayed'
                  ? 'Delayed'
                  : quality === 'lost'
                    ? 'Lost'
                    : 'Connecting'}
            </span>
          </span>
          <span className={`chip ${benchOn ? 'ok' : ''}`} title="Session Bench TX gate">
            <span className="chip-k">Bench TX</span>
            <span className="chip-v">{benchOn ? 'On' : 'Off'}</span>
          </span>
          <span className={`chip ${armed ? 'ok' : ''}`} data-testid="drive-arm-chip">
            <span className="chip-k">Control</span>
            <span className="chip-v">{armed ? 'Armed' : 'Local'}</span>
          </span>
          <span className={`chip ${canLive ? 'ok' : ''}`} title="HOST_DRIVE_CMD freshness">
            <span className="chip-k">0x300</span>
            <span className="chip-v">{canLive ? 'Live' : 'Idle'}</span>
          </span>
          <span className={`chip ${focused ? 'ok' : ''}`} data-testid="drive-focus-chip">
            <span className="chip-k">Keys</span>
            <span className="chip-v">{focused ? 'Ready' : 'Click UI'}</span>
          </span>
        </div>
        <div className="actions tight drive-top-actions">
          {fullVehicle ? (
            <span className="chip danger" data-testid="drive-mode-restriction">
              Teleoperation hidden in Full Vehicle
            </span>
          ) : !armed ? (
            <button
              type="button"
              data-testid="btn-drive-arm"
              disabled={busy}
              onClick={() => void armControl()}
            >
              Arm CAN control
            </button>
          ) : (
            <button
              type="button"
              className="secondary"
              data-testid="btn-drive-disarm"
              disabled={busy}
              onClick={() => void disarmControl('disarm')}
            >
              Disarm
            </button>
          )}
          {!fullVehicle && <button
            type="button"
            className="danger"
            data-testid="btn-drive-estop"
            disabled={busy}
            onClick={() => void fireEstop()}
          >
            ESTOP
          </button>}
        </div>
      </header>

      <div className="drive-layout">
        <div
          className="preview-canvas-wrap"
          ref={wrapRef}
          tabIndex={0}
          data-testid="preview-canvas-wrap"
          onMouseDown={() => focusDrive()}
          onFocus={() => setFocused(true)}
          onBlur={(e) => {
            // Keep focused flag if focus moves to another Drive control.
            const next = e.relatedTarget as Node | null
            if (next && rootRef.current?.contains(next)) return
            // Delay: click from canvas → button may briefly clear focus.
            window.setTimeout(() => {
              if (!rootRef.current?.matches(':focus-within')) setFocused(false)
            }, 0)
          }}
        >
          <canvas ref={canvasRef} data-testid="preview-canvas" />
          <div
            className={`drive-lock-hint ${armed ? 'armed' : 'muted'}`}
            data-testid="drive-lock-hint"
          >
            {armed
              ? 'Armed — keys & keycaps publish HOST_DRIVE_CMD @ 10 ms'
              : focused
                ? 'Local sim — Arm CAN control to transmit on the bus'
                : 'Click canvas or side panel, then use WASD / keycaps'}
          </div>
        </div>

        <aside className="drive-side panel" data-testid="drive-side">
          {/* Controls first so keycaps/gears are visible without scrolling past gauges */}
          <section className="drive-section" data-testid="drive-controls-section">
            <div className="drive-section-head">
              <h2>Controls</h2>
              <span className="muted small">hold keycaps or use keyboard</span>
            </div>
            <div className="keycap-pad" data-testid="drive-keycaps">
              <div className="keycap-row">
                <button type="button" className={`keycap ${k.KeyQ ? 'active' : ''}`} data-testid="keycap-Q" {...keycapHandlers('KeyQ')}>
                  Q
                </button>
                <button type="button" className={`keycap ${k.KeyW || k.ArrowUp ? 'active' : ''}`} data-testid="keycap-W" {...keycapHandlers('KeyW')}>
                  W
                </button>
                <button type="button" className={`keycap ${k.KeyE ? 'active' : ''}`} data-testid="keycap-E" {...keycapHandlers('KeyE')}>
                  E
                </button>
              </div>
              <div className="keycap-row">
                <button type="button" className={`keycap ${k.KeyA || k.ArrowLeft ? 'active' : ''}`} data-testid="keycap-A" {...keycapHandlers('KeyA')}>
                  A
                </button>
                <button type="button" className={`keycap ${k.KeyS || k.ArrowDown ? 'active' : ''}`} data-testid="keycap-S" {...keycapHandlers('KeyS')}>
                  S
                </button>
                <button type="button" className={`keycap ${k.KeyD || k.ArrowRight ? 'active' : ''}`} data-testid="keycap-D" {...keycapHandlers('KeyD')}>
                  D
                </button>
              </div>
              <div className="keycap-row">
                <button
                  type="button"
                  className={`keycap ${k.ShiftLeft || k.ShiftRight ? 'active' : ''}`}
                  data-testid="keycap-Shift"
                  {...keycapHandlers('ShiftLeft')}
                >
                  Shift
                </button>
                <button
                  type="button"
                  className={`keycap wide ${k.Space ? 'active' : ''}`}
                  data-testid="keycap-Space"
                  {...keycapHandlers('Space')}
                >
                  Space ESTOP
                </button>
              </div>
              {!armed && (
                <div className="keycap-banner" data-testid="keycap-local-banner">
                  Local sim — not on bus until Armed
                </div>
              )}
            </div>
          </section>

          <section className="drive-section" data-testid="drive-shift-section">
            <div className="drive-section-head">
              <h2>Shift mode</h2>
            </div>
            <div className="seg" data-testid="preview-shift-mode">
              <button
                type="button"
                className={shiftMode === 'smart' ? 'seg-btn active' : 'seg-btn'}
                data-testid="preview-mode-adaptive"
                onClick={() => setShiftMode('smart')}
              >
                Adaptive
              </button>
              <button
                type="button"
                className={shiftMode === 'direct' ? 'seg-btn active' : 'seg-btn'}
                data-testid="preview-mode-direct"
                onClick={() => setShiftMode('direct')}
              >
                Direct
              </button>
            </div>
            <p className="muted small preview-mode-blurb" data-testid="preview-mode-blurb">
              {shiftMode === 'smart' ? (
                <>
                  <strong>Adaptive:</strong> auto D/S/R from throttle. Armed: backend also maps
                  throttle → D or R.
                </>
              ) : (
                <>
                  <strong>Direct:</strong> gear only from N/D/S/R or Q/E. Pedals accelerate/brake
                  in the selected gear.
                </>
              )}
            </p>

            <div className="gear-row" data-testid="preview-gears">
              {GEARS.map((g) => (
                <button
                  key={g}
                  type="button"
                  className={gear === g ? 'gear-btn active' : 'gear-btn'}
                  data-testid={`preview-gear-${g}`}
                  onClick={() => {
                    applyGear(g)
                    setFocused(true)
                  }}
                >
                  {g}
                </button>
              ))}
            </div>
          </section>

          <section className="drive-section" data-testid="drive-telemetry-section">
            <div className="drive-section-head">
              <h2>Telemetry</h2>
              <span className="muted small mono">
                {armed ? (canLive ? 'from bus' : 'waiting 0x300') : 'local sim'}
              </span>
            </div>
            <div className="drive-gauges" data-testid="drive-gauges">
              <Gauge
                label="cmd speed"
                value={displaySpeed}
                unit="mm/s"
                max={Math.max(1, maxSpeedMmps)}
                tone="accent"
              />
              <Gauge
                label="cmd yaw"
                value={displayYaw}
                unit="mrad/s"
                max={Math.max(1, maxYawMrad)}
                tone="warn"
              />
              <Gauge label="steer α" value={hud.alphaDeg} unit="°" max={45} tone="ok" />
              <Gauge label="brake" value={hud.brakeKpa} unit="kPa" max={5000} tone="danger" />
            </div>
            <dl className="kv preview-kv" data-testid="preview-telemetry">
              <dt>HOST speed / yaw [0x300]</dt>
              <dd className="mono">
                {displaySpeed.toFixed(0)} mm/s · {displayYaw.toFixed(0)} mrad/s
              </dd>
              <dt>Gear · heading</dt>
              <dd className="mono">
                {displayGear} · θ {hud.thetaDeg.toFixed(1)}°
              </dd>
              <dt>Turn radius</dt>
              <dd className="mono">{hud.radiusText}</dd>
              <dt>Backend shaped</dt>
              <dd className="mono" data-testid="drive-shaped">
                {shapedSpeed != null
                  ? `${shapedSpeed} mm/s · ${String(ctrlSnap?.gear_label ?? '—')} · yaw ${shapedYaw ?? 0}`
                  : armed
                    ? 'waiting intent…'
                    : '— (arm to shape)'}
              </dd>
            </dl>
          </section>

          <section className="drive-section" data-testid="drive-limits-section">
            <div className="drive-section-head">
              <h2>Authority limits</h2>
              <span className="muted small">firmware max 3000</span>
            </div>
            <label className="field">
              <span className="field-label">Max drive speed, mm/s</span>
              <div className="field-row">
                <input
                  type="range"
                  min={0}
                  max={3000}
                  step={50}
                  value={maxSpeedMmps}
                  data-testid="drive-max-speed"
                  onChange={(e) => setMaxSpeedMmps(Number(e.target.value))}
                />
                <span className="mono field-val">{maxSpeedMmps}</span>
              </div>
            </label>
            <label className="field">
              <span className="field-label">Max yaw rate, mrad/s</span>
              <div className="field-row">
                <input
                  type="range"
                  min={0}
                  max={3000}
                  step={50}
                  value={maxYawMrad}
                  data-testid="drive-max-yaw"
                  onChange={(e) => setMaxYawMrad(Number(e.target.value))}
                />
                <span className="mono field-val">{maxYawMrad}</span>
              </div>
            </label>
          </section>

          <ul className="controls-legend muted small" data-testid="preview-controls-legend">
            <li>
              <kbd>W</kbd>/<kbd>S</kbd> throttle · <kbd>A</kbd>/<kbd>D</kbd> yaw
            </li>
            <li>
              <kbd>Q</kbd>/<kbd>E</kbd> gear · <kbd>Shift</kbd> hard brake · <kbd>Space</kbd> ESTOP
            </li>
            <li>Hide browser tab disarms CAN (safety). Click keycaps to hold inputs.</li>
          </ul>

          <pre className="log" data-testid="drive-log">
            {log ||
              'Click the canvas or any Drive control for keyboard focus. Local sim is free; Arm to TX on High bus.'}
          </pre>
        </aside>
      </div>
    </div>
  )
}

