/**
 * Drive console — see + control over CAN.
 *
 * UX patterns from:
 *  - tricycle_kinematics_simulator.html (Adaptive/Direct, ego canvas, gear N/D/S/R)
 *  - leadmate robot_control (keycaps, arm lock, gauges, speed sliders, ESTOP)
 *
 * Transport is CAN (virtual/physical via backend), not ROS cmd_vel.
 */
import { useCallback, useEffect, useRef, useState } from 'react'
import { useAppStore, type MessageState } from './store'
import { api } from './api'

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

function KeyCap({ label, active, wide }: { label: string; active: boolean; wide?: boolean }) {
  return (
    <span
      className={`keycap ${active ? 'active' : ''} ${wide ? 'wide' : ''}`}
      data-active={active ? '1' : '0'}
    >
      {label}
    </span>
  )
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
  return (
    <div className={`drive-gauge tone-${tone}`} data-testid={`gauge-${label}`}>
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

  const [armed, setArmed] = useState(false)
  const [busy, setBusy] = useState(false)
  const [log, setLog] = useState('')
  const [shiftMode, setShiftMode] = useState<ShiftMode>('smart')
  const [gear, setGear] = useState<Gear>('N')
  const [maxSpeedMmps, setMaxSpeedMmps] = useState(3000)
  const [maxYawMrad, setMaxYawMrad] = useState(3000)
  const [keyUi, setKeyUi] = useState<Record<string, boolean>>({})
  const [ctrlSnap, setCtrlSnap] = useState<Record<string, unknown> | null>(null)
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

  useEffect(() => {
    stateRef.current.shiftMode = shiftMode
  }, [shiftMode])
  useEffect(() => {
    stateRef.current.gear = gear
  }, [gear])

  async function ensureArmedPath() {
    let st = await api.status()
    if (!st.session?.session_id) {
      await api.createSession('pure_software')
      st = await api.status()
    }
    if (st.session.bench_tx !== 'enabled') {
      await api.setBenchTx(st.session.session_id!, true, st.session.revision)
      st = await api.status()
    }
    setStatus(st)
    return st
  }

  async function armControl() {
    setBusy(true)
    try {
      await ensureArmedPath()
      setArmed(true)
      setLog('Armed: keyboard → HOST_DRIVE_CMD on CAN (10 ms). Canvas follows bus.')
    } catch (e) {
      setLog(String(e))
      setArmed(false)
    } finally {
      setBusy(false)
    }
  }

  async function disarmControl(reason = 'disarm') {
    setArmed(false)
    try {
      await api.controlRelease(reason)
    } catch {
      /* ignore */
    }
    setLog(`Disarmed (${reason}). Keyboard drives local sim only.`)
  }

  async function fireEstop() {
    setBusy(true)
    try {
      await ensureArmedPath()
      const r = await api.controlIntent({
        sequence: ++seqRef.current,
        throttle: 0,
        steer: 0,
        estop: true,
      })
      setCtrlSnap(r.control)
      setArmed(false)
      applyGear('N')
      setLog('ESTOP: SAFETY_ESTOP on high+low CAN')
      setStatus(await api.status())
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  // Keyboard capture always when console focused
  useEffect(() => {
    const onDown = (e: KeyboardEvent) => {
      if (!wrapRef.current?.contains(document.activeElement) && document.activeElement !== wrapRef.current) {
        // still allow if body focused after click on console
        if (!wrapRef.current?.matches(':focus-within')) return
      }
      keysRef.current[e.code] = true
      if (
        ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Space', 'KeyW', 'KeyA', 'KeyS', 'KeyD'].includes(
          e.code,
        )
      ) {
        e.preventDefault()
      }
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
      keysRef.current[e.code] = false
      setKeyUi({ ...keysRef.current })
    }
    const onBlur = () => {
      keysRef.current = {}
      setKeyUi({})
      if (armedRef.current) void disarmControl('blur')
    }
    const onVis = () => {
      if (document.hidden && armedRef.current) void disarmControl('tab_hidden')
    }
    window.addEventListener('keydown', onDown)
    window.addEventListener('keyup', onUp)
    window.addEventListener('blur', onBlur)
    document.addEventListener('visibilitychange', onVis)
    return () => {
      window.removeEventListener('keydown', onDown)
      window.removeEventListener('keyup', onUp)
      window.removeEventListener('blur', onBlur)
      document.removeEventListener('visibilitychange', onVis)
      void api.controlRelease('unmount').catch(() => undefined)
    }
  }, [applyGear])

  // Armed: publish intent to CAN at 20 Hz (backend shapes + 10 ms TX)
  useEffect(() => {
    if (!armed) return
    const id = window.setInterval(() => {
      const k = keysRef.current
      let throttle = 0
      let steer = 0
      if (k.KeyW || k.ArrowUp) throttle += 1
      if (k.KeyS || k.ArrowDown) throttle -= 1
      if (k.KeyA || k.ArrowLeft) steer -= 1
      if (k.KeyD || k.ArrowRight) steer += 1
      // Scale by sliders as max authority (robot_control speed sliders idea)
      const speedFrac = maxSpeedMmps / 3000
      const yawFrac = maxYawMrad / 3000
      throttle *= Math.min(1, speedFrac)
      steer *= Math.min(1, yawFrac)

      let gearNum = GEAR_NUM[gearRef.current]
      if (shiftRef.current === 'smart') {
        if (throttle > 0 && gearNum === 0) gearNum = 1
        if (throttle < 0) gearNum = 3
      }
      const hard_brake = !!(k.ShiftLeft || k.ShiftRight)
      const estop = !!k.Space
      seqRef.current += 1
      void api
        .controlIntent({
          sequence: seqRef.current,
          source: 'keyboard',
          mode: 'kinematics',
          throttle,
          steer,
          gear: gearNum,
          hard_brake,
          estop,
        })
        .then((r) => {
          setCtrlSnap(r.control)
          if (estop) {
            setArmed(false)
            applyGear('N')
          }
        })
        .catch((e) => setLog(String(e)))
    }, 50)
    return () => {
      window.clearInterval(id)
      void api.controlRelease('disable').catch(() => undefined)
    }
  }, [armed, maxSpeedMmps, maxYawMrad, applyGear])

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
      const speedMmps = numSignal(drive, 'speed_mmps') ?? 0
      const yawMrad = numSignal(drive, 'yaw_rate_mrad_s') ?? 0
      const g = gearFromCan(drive)
      if (g && g !== state.gear) applyGear(g)
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

  return (
    <div className="workspace drive-console" data-testid="workspace-preview">
      <header className="drive-topbar" data-testid="drive-topbar">
        <div>
          <h1>Drive</h1>
          <p className="muted small">
            High-bus Host kinematics only (HOST_DRIVE_CMD 0x300). Not Low-bus direct
            actuators — use Control → Low bus for motor/steer/brake unit tests.
          </p>
        </div>
        <div className="drive-top-chips">
          <span className={`chip quality-${quality}`}>
            <span className="chip-k">Stream</span>
            <span className="chip-v">
              ●{' '}
              {quality === 'live'
                ? 'Live'
                : quality === 'delayed'
                  ? 'Delayed'
                  : quality === 'lost'
                    ? 'Lost'
                    : 'Connecting'}
            </span>
          </span>
          <span className={`chip ${benchOn ? 'ok' : 'muted'}`}>
            <span className="chip-k">Bench TX</span>
            <span className="chip-v">{benchOn ? '● Enabled' : '● Disabled'}</span>
          </span>
          <span className={`chip ${armed ? 'ok' : ''}`}>
            <span className="chip-k">Control</span>
            <span className="chip-v">{armed ? '● Armed' : '● Local sim'}</span>
          </span>
          <span className={`chip ${canLive ? 'ok' : 'muted'}`}>
            <span className="chip-k">0x300</span>
            <span className="chip-v">{canLive ? '● Live' : '● Missing'}</span>
          </span>
        </div>
        <div className="actions tight">
          {!armed ? (
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
              Disarm control
            </button>
          )}
          <button
            type="button"
            className="danger"
            data-testid="btn-drive-estop"
            disabled={busy}
            onClick={() => void fireEstop()}
          >
            Inject ESTOP
          </button>
        </div>
      </header>

      <div className="drive-layout">
        <div
          className="preview-canvas-wrap"
          ref={wrapRef}
          tabIndex={0}
          data-testid="preview-canvas-wrap"
          onMouseDown={() => wrapRef.current?.focus()}
        >
          <canvas ref={canvasRef} data-testid="preview-canvas" />
          {!armed && (
            <div className="drive-lock-hint muted">
              Local sim — arm to publish Host intent on High bus (0x300)
            </div>
          )}
          {armed && (
            <div className="drive-lock-hint armed">
              Armed — High bus HOST_DRIVE_CMD @ 10 ms; keys send Host intent
            </div>
          )}
        </div>

        <aside className="drive-side panel">
          <h2>Telemetry</h2>
          <div className="drive-gauges" data-testid="drive-gauges">
            <Gauge
              label="cmd speed"
              value={armed && canSpeed != null ? canSpeed : hud.speedMmps}
              unit="mm/s"
              max={maxSpeedMmps}
              tone="accent"
            />
            <Gauge
              label="cmd yaw"
              value={armed && canYaw != null ? canYaw : hud.yawMradS}
              unit="mrad/s"
              max={maxYawMrad}
              tone="warn"
            />
            <Gauge label="steer α" value={hud.alphaDeg} unit="°" max={45} tone="ok" />
            <Gauge label="brake" value={hud.brakeKpa} unit="kPa" max={5000} tone="danger" />
          </div>
          <dl className="kv preview-kv" data-testid="preview-telemetry">
            <dt>HOST_DriveSpeed [0x300]</dt>
            <dd className="mono">
              {(armed && canSpeed != null ? canSpeed : hud.speedMmps).toFixed(0)} mm/s
            </dd>
            <dt>HOST_YawRate [0x300]</dt>
            <dd className="mono">
              {(armed && canYaw != null ? canYaw : hud.yawMradS).toFixed(0)} mrad/s
            </dd>
            <dt>Gear</dt>
            <dd className="mono">
              {armed && canGear ? canGear : gear} · θ {hud.thetaDeg.toFixed(1)}°
            </dd>
            <dt>Turn ρ</dt>
            <dd className="mono">{hud.radiusText}</dd>
            {ctrlSnap && (
              <>
                <dt>Backend shape</dt>
                <dd className="mono">
                  {String(ctrlSnap.shaped_speed_mmps)} mm/s · {String(ctrlSnap.gear_label)}
                </dd>
              </>
            )}
          </dl>

          <h2 className="mt-section">Drive</h2>
          <div className="keycap-pad" data-testid="drive-keycaps">
            <div className="keycap-row">
              <KeyCap label="Q" active={!!k.KeyQ} />
              <KeyCap label="W" active={!!(k.KeyW || k.ArrowUp)} />
              <KeyCap label="E" active={!!k.KeyE} />
            </div>
            <div className="keycap-row">
              <KeyCap label="A" active={!!(k.KeyA || k.ArrowLeft)} />
              <KeyCap label="S" active={!!(k.KeyS || k.ArrowDown)} />
              <KeyCap label="D" active={!!(k.KeyD || k.ArrowRight)} />
            </div>
            <div className="keycap-row">
              <KeyCap label="Shift" active={!!(k.ShiftLeft || k.ShiftRight)} />
              <KeyCap label="Space ESTOP" active={!!k.Space} wide />
            </div>
            {!armed && <div className="keycap-lock">Local only — not on bus</div>}
          </div>

          <h2 className="mt-section">Shift mode</h2>
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
                <strong>Adaptive:</strong> auto D/S/R from pedals (HTML sim). When armed, throttle
                maps gear toward D or R on the bus.
              </>
            ) : (
              <>
                <strong>Direct:</strong> gear only from N/D/S/R buttons or Q/E. Pedals act as
                accelerator/brake in that gear.
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
                onClick={() => applyGear(g)}
              >
                {g}
              </button>
            ))}
          </div>

          <label className="field mt-section">
            <span className="field-label">Max drive speed, mm/s</span>
            <div className="field-row">
              <input
                type="range"
                min={0}
                max={6000}
                step={100}
                value={maxSpeedMmps}
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
                onChange={(e) => setMaxYawMrad(Number(e.target.value))}
              />
              <span className="mono field-val">{maxYawMrad}</span>
            </div>
          </label>

          <ul className="controls-legend muted small" data-testid="preview-controls-legend">
            <li>
              <kbd>W</kbd>/<kbd>S</kbd> throttle · <kbd>A</kbd>/<kbd>D</kbd> yaw
            </li>
            <li>
              <kbd>Q</kbd>/<kbd>E</kbd> gear · <kbd>Shift</kbd> hard brake · <kbd>Space</kbd> ESTOP
            </li>
            <li>Blur / tab hide disarms CAN control (like robot_control lock)</li>
          </ul>

          <pre className="log" data-testid="drive-log">
            {log || 'Click canvas for focus. Local sim is free. Arm CAN control to transmit on the bus.'}
          </pre>
        </aside>
      </div>
    </div>
  )
}
