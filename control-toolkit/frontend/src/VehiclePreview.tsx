/**
 * 2D tricycle kinematics preview — React port of tricycle_kinematics_simulator.html.
 * Ego-centered canvas with protocol-named signals (HOST_DRIVE_CMD 0x300, etc.).
 * Modes: keyboard local sim, or follow live CAN drive command.
 */
import { useCallback, useEffect, useRef, useState } from 'react'
import { useAppStore, type MessageState } from './store'

type Gear = 'P' | 'R' | 'D' | 'S'
type ShiftMode = 'smart' | 'direct'
type DriveSource = 'keyboard' | 'can'

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
const GEARS: Gear[] = ['P', 'R', 'D', 'S']

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
  gear: 'P',
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
  if (label === 'P' || label === 'R' || label === 'D' || label === 'S') return label
  const n = Number(raw.engineering_value)
  if (n === 0) return 'P'
  if (n === 1) return 'R'
  if (n === 2) return 'D'
  if (n === 3) return 'S'
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

export function VehiclePreview() {
  const messages = useAppStore((s) => s.messages)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const wrapRef = useRef<HTMLDivElement>(null)
  const stateRef = useRef<SimState>({ ...INITIAL })
  const keysRef = useRef<Record<string, boolean>>({})
  const lastTimeRef = useRef(performance.now())
  const rafRef = useRef(0)
  const focusedRef = useRef(false)

  const [driveSource, setDriveSource] = useState<DriveSource>('keyboard')
  const [shiftMode, setShiftMode] = useState<ShiftMode>('smart')
  const [gear, setGear] = useState<Gear>('P')
  const [maxSpeedMmps, setMaxSpeedMmps] = useState(3000)
  const [maxYawMrad, setMaxYawMrad] = useState(3000)
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

  const driveMsg = messages.find((m) => m.name === 'HOST_DRIVE_CMD')
  const canLive = driveMsg?.freshness?.toLowerCase() === 'live'

  // Keep sim refs in sync with UI controls
  useEffect(() => {
    stateRef.current.shiftMode = shiftMode
  }, [shiftMode])
  useEffect(() => {
    stateRef.current.gear = gear
  }, [gear])

  const applyGear = useCallback((g: Gear) => {
    stateRef.current.gear = g
    setGear(g)
  }, [])

  // Keyboard only when preview is focused / keyboard mode
  useEffect(() => {
    const onDown = (e: KeyboardEvent) => {
      if (!focusedRef.current || driveSource !== 'keyboard') return
      const code = e.code
      if (
        [
          'ArrowUp',
          'ArrowDown',
          'ArrowLeft',
          'ArrowRight',
          'KeyW',
          'KeyS',
          'KeyA',
          'KeyD',
          'Space',
        ].includes(code)
      ) {
        e.preventDefault()
        keysRef.current[code] = true
      }
      if (code === 'KeyQ') {
        const idx = GEARS.indexOf(stateRef.current.gear)
        if (idx > 0) applyGear(GEARS[idx - 1])
      }
      if (code === 'KeyE') {
        const idx = GEARS.indexOf(stateRef.current.gear)
        if (idx < GEARS.length - 1) applyGear(GEARS[idx + 1])
      }
    }
    const onUp = (e: KeyboardEvent) => {
      keysRef.current[e.code] = false
    }
    window.addEventListener('keydown', onDown)
    window.addEventListener('keyup', onUp)
    return () => {
      window.removeEventListener('keydown', onDown)
      window.removeEventListener('keyup', onUp)
    }
  }, [applyGear, driveSource])

  // Animation loop
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

    function updateKeyboard(dt: number) {
      const state = stateRef.current
      const keys = keysRef.current
      const pressingUp = !!(keys.ArrowUp || keys.KeyW)
      const pressingDown = !!(keys.ArrowDown || keys.KeyS)
      const MAX_V_PX = maxVPx()
      const ACCEL = accel()
      const MAX_ALPHA = maxAlphaRad()

      state.isEstop = !!keys.Space

      if (state.shiftMode === 'smart') {
        if (keys.Space) {
          if (state.gear !== 'P') applyGear('P')
        } else if (state.v === 0) {
          if (pressingUp && state.gear !== 'D' && state.gear !== 'S') applyGear('D')
          if (pressingDown && state.gear !== 'R') applyGear('R')
        } else if (state.v > 0 && (state.gear === 'D' || state.gear === 'S')) {
          const shiftThreshold = MAX_V_PX * 0.5
          if (state.v > shiftThreshold && state.gear === 'D') applyGear('S')
          else if (state.v < shiftThreshold - 20 && state.gear === 'S') applyGear('D')
        }

        state.isBraking =
          !!keys.Space || (state.v > 0 && pressingDown) || (state.v < 0 && pressingUp)

        if (state.gear === 'P') {
          state.v *= FRICTION
          if (Math.abs(state.v) < 1) state.v = 0
        } else if (state.gear === 'R') {
          if (pressingDown) state.v -= ACCEL * dt
          if (pressingUp) {
            if (state.v < 0) state.v += ACCEL * dt
            if (state.v > 0) state.v = 0
          }
        } else if (state.gear === 'D' || state.gear === 'S') {
          const currentAccel = state.gear === 'S' ? ACCEL * 1.5 : ACCEL
          const currentMax = state.gear === 'S' ? MAX_V_PX * 1.5 : MAX_V_PX
          if (pressingUp) state.v += currentAccel * dt
          if (pressingDown) {
            if (state.v > 0) state.v -= ACCEL * dt
            if (state.v < 0) state.v = 0
          }
          if (state.v > currentMax) {
            state.v -= ACCEL * dt
            if (state.v < currentMax) state.v = currentMax
          } else if (state.v < -currentMax) {
            state.v += ACCEL * dt
            if (state.v > -currentMax) state.v = -currentMax
          }
        }
      } else {
        if (keys.Space && state.gear !== 'P') applyGear('P')
        state.isBraking = !!keys.Space || pressingDown
        if (state.gear === 'P') {
          state.v *= FRICTION
          if (Math.abs(state.v) < 1) state.v = 0
        } else if (state.gear === 'R') {
          if (pressingUp) state.v -= ACCEL * dt
          if (pressingDown) {
            if (state.v < 0) state.v += ACCEL * dt
            if (state.v > 0) state.v = 0
          }
          if (state.v < -MAX_V_PX) state.v = -MAX_V_PX
        } else if (state.gear === 'D' || state.gear === 'S') {
          const currentAccel = state.gear === 'S' ? ACCEL * 1.5 : ACCEL
          const currentMax = state.gear === 'S' ? MAX_V_PX * 1.5 : MAX_V_PX
          if (pressingUp) state.v += currentAccel * dt
          if (pressingDown) {
            if (state.v > 0) state.v -= ACCEL * dt
            if (state.v < 0) state.v = 0
          }
          if (state.v > currentMax) {
            state.v -= ACCEL * dt
            if (state.v < currentMax) state.v = currentMax
          }
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

      const dynLimits = computeDynamicLimits(state.v)
      const steerRate = dynLimits.steerRateRadS
      if (keys.ArrowLeft || keys.KeyA) state.alpha -= steerRate * dt
      if (keys.ArrowRight || keys.KeyD) state.alpha += steerRate * dt
      if (!keys.ArrowLeft && !keys.ArrowRight && !keys.KeyA && !keys.KeyD) {
        state.alpha *= 0.95
      }

      const actualMaxAlpha = Math.min(MAX_ALPHA, dynLimits.maxAlphaRad)
      state.alpha = Math.max(-actualMaxAlpha, Math.min(actualMaxAlpha, state.alpha))
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

      // Target velocity in px/s from HOST_DriveSpeed
      const targetV = (speedMmps / 1000) * PIXELS_PER_METER
      // Yaw rate mrad/s → rad/s; invert bicycle model for steer angle
      const omega = yawMrad / 1000
      let targetAlpha = 0
      if (Math.abs(targetV) > 1) {
        targetAlpha = Math.atan((omega * L) / targetV)
      }
      const dynLimits = computeDynamicLimits(state.v)
      const MAX_ALPHA = Math.min(maxAlphaRad(), dynLimits.maxAlphaRad)
      targetAlpha = Math.max(-MAX_ALPHA, Math.min(MAX_ALPHA, targetAlpha))

      // Smooth toward CAN targets
      state.v += (targetV - state.v) * Math.min(1, 8 * dt)
      state.alpha += (targetAlpha - state.alpha) * Math.min(1, 10 * dt)
      state.isBraking = Math.abs(targetV) < Math.abs(state.v) * 0.5 && Math.abs(state.v) > 5
      state.brakePressureKpa = state.isBraking
        ? Math.min(state.brakePressureKpa + 8000 * dt, 2000)
        : Math.max(state.brakePressureKpa - 15000 * dt, 0)
      state.isEstop = false

      state.omega = (state.v / L) * Math.tan(state.alpha)
      state.theta += state.omega * dt
      if (state.theta > Math.PI * 2) state.theta -= Math.PI * 2
      if (state.theta < 0) state.theta += Math.PI * 2
      state.x += state.v * Math.cos(state.theta) * dt
      state.y += state.v * Math.sin(state.theta) * dt
    }

    function drawGrid(centerX: number, centerY: number, w: number, h: number) {
      const gridSize = 50
      const egoScreenY = h * 0.75
      const originX = w / 2 - centerX
      const originY = egoScreenY - centerY
      const offsetX = originX % gridSize
      const offsetY = originY % gridSize

      ctx!.strokeStyle = '#2a313b'
      ctx!.lineWidth = 1
      ctx!.beginPath()
      for (let x = offsetX - gridSize; x < w; x += gridSize) {
        ctx!.moveTo(x, 0)
        ctx!.lineTo(x, h)
      }
      for (let y = offsetY - gridSize; y < h; y += gridSize) {
        ctx!.moveTo(0, y)
        ctx!.lineTo(w, y)
      }
      ctx!.stroke()

      ctx!.lineWidth = 2
      ctx!.strokeStyle = 'rgba(231, 236, 242, 0.12)'
      ctx!.beginPath()
      ctx!.moveTo(originX - 1000, originY)
      ctx!.lineTo(originX + 1000, originY)
      ctx!.moveTo(originX, originY - 1000)
      ctx!.lineTo(originX, originY + 1000)
      ctx!.stroke()

      ctx!.fillStyle = 'rgba(139, 150, 165, 0.8)'
      ctx!.font = '12px ui-monospace, monospace'
      ctx!.fillText('Origin (0,0)', originX + 5, originY - 5)
    }

    function drawVehicle(w: number, h: number) {
      const state = stateRef.current
      const MAX_V_PX = maxVPx()
      ctx!.save()
      const egoScreenY = h * 0.75
      ctx!.translate(w / 2, egoScreenY)

      ctx!.strokeStyle = 'rgba(231, 236, 242, 0.2)'
      ctx!.setLineDash([5, 5])
      ctx!.beginPath()
      ctx!.moveTo(0, 0)
      ctx!.lineTo(150, 0)
      ctx!.stroke()

      ctx!.rotate(state.theta)

      if (Math.abs(state.alpha) > 0.01) {
        const R = L / Math.tan(state.alpha)
        ctx!.strokeStyle = 'rgba(168, 85, 247, 0.45)'
        ctx!.setLineDash([8, 4])
        ctx!.beginPath()
        ctx!.moveTo(0, 0)
        ctx!.lineTo(0, R)
        ctx!.moveTo(L, 0)
        ctx!.lineTo(0, R)
        ctx!.stroke()
        ctx!.fillStyle = '#a855f7'
        ctx!.beginPath()
        ctx!.arc(0, R, 4, 0, Math.PI * 2)
        ctx!.fill()
        ctx!.fillText('C (ICR)', 10, R)
        ctx!.setLineDash([])
      }

      ctx!.setLineDash([])
      ctx!.strokeStyle = '#94a3b8'
      ctx!.lineWidth = 2
      ctx!.fillStyle = 'rgba(23, 27, 33, 0.85)'
      const cWidth = L + 40
      const cHeight = W + 40
      ctx!.beginPath()
      if (typeof ctx!.roundRect === 'function') {
        ctx!.roundRect(-30, -cHeight / 2, cWidth, cHeight, 12)
      } else {
        ctx!.rect(-30, -cHeight / 2, cWidth, cHeight)
      }
      ctx!.fill()
      ctx!.stroke()

      // Brake lights
      ctx!.save()
      if (state.isBraking) {
        ctx!.fillStyle = '#ef4444'
        ctx!.shadowColor = '#ef4444'
        ctx!.shadowBlur = state.isEstop ? 20 : 10
      } else {
        ctx!.fillStyle = '#475569'
        ctx!.shadowBlur = 0
      }
      ctx!.fillRect(-32, -cHeight / 2 + 10, 6, 20)
      ctx!.fillRect(-32, cHeight / 2 - 30, 6, 20)
      ctx!.restore()

      ctx!.strokeStyle = '#64748b'
      ctx!.setLineDash([4, 4])
      ctx!.beginPath()
      ctx!.moveTo(-40, 0)
      ctx!.lineTo(L + 60, 0)
      ctx!.stroke()
      ctx!.setLineDash([])
      ctx!.beginPath()
      ctx!.moveTo(0, -W / 2)
      ctx!.lineTo(0, W / 2)
      ctx!.stroke()

      const wLen = 34
      const wThick = 14
      ctx!.fillStyle = '#34d399'
      ctx!.fillRect(-wLen / 2, -W / 2 - wThick / 2, wLen, wThick)
      ctx!.strokeRect(-wLen / 2, -W / 2 - wThick / 2, wLen, wThick)
      ctx!.fillRect(-wLen / 2, W / 2 - wThick / 2, wLen, wThick)
      ctx!.strokeRect(-wLen / 2, W / 2 - wThick / 2, wLen, wThick)

      ctx!.save()
      ctx!.translate(L, 0)
      ctx!.strokeStyle = 'rgba(231, 236, 242, 0.25)'
      ctx!.setLineDash([5, 5])
      ctx!.beginPath()
      ctx!.moveTo(0, 0)
      ctx!.lineTo(80, 0)
      ctx!.stroke()
      ctx!.setLineDash([])
      ctx!.rotate(state.alpha)
      ctx!.fillStyle = '#ef4444'
      ctx!.fillRect(-wLen / 2, -wThick / 2, wLen, wThick)
      ctx!.strokeRect(-wLen / 2, -wThick / 2, wLen, wThick)
      if (Math.abs(state.v) > 1) {
        const vLen = (state.v / Math.max(1, MAX_V_PX)) * 60
        ctx!.strokeStyle = '#a855f7'
        ctx!.lineWidth = 2
        ctx!.beginPath()
        ctx!.moveTo(0, 0)
        ctx!.lineTo(vLen, 0)
        ctx!.lineTo(vLen - 6, -4)
        ctx!.moveTo(vLen, 0)
        ctx!.lineTo(vLen - 6, 4)
        ctx!.stroke()
        ctx!.fillStyle = '#a855f7'
        ctx!.fillText('Vs', vLen + 5, 5)
      }
      ctx!.restore()

      ctx!.fillStyle = '#e7ecf2'
      ctx!.font = '13px Inter, sans-serif'
      ctx!.fillText('L', L / 2 - 4, -10)
      ctx!.beginPath()
      ctx!.moveTo(0, -5)
      ctx!.lineTo(0, 5)
      ctx!.moveTo(L, -5)
      ctx!.lineTo(L, 5)
      ctx!.moveTo(0, 0)
      ctx!.lineTo(L, 0)
      ctx!.strokeStyle = '#94a3b8'
      ctx!.lineWidth = 1
      ctx!.stroke()

      ctx!.restore()
    }

    function publishHud() {
      const state = stateRef.current
      const speedMmps = (state.v / PIXELS_PER_METER) * 1000
      const yawMradS = state.omega * 1000
      let degTheta = ((state.theta * 180) / Math.PI) % 360
      if (degTheta > 180) degTheta -= 360
      const steerDeg = (state.alpha * 180) / Math.PI
      const radiusText =
        Math.abs(state.alpha) < 0.01
          ? 'Straight (∞)'
          : `${Math.abs(L / Math.tan(state.alpha)).toFixed(1)} px`
      const dyn = computeDynamicLimits(state.v)
      setHud({
        speedMmps,
        yawMradS,
        thetaDeg: degTheta,
        alphaDeg: steerDeg,
        brakeKpa: state.brakePressureKpa,
        radiusText,
        dynClampDeg: dyn.maxAlphaDeg,
        dynSlewDegS: dyn.steerRateDegS,
      })
    }

    let hudTick = 0
    function loop(ts: number) {
      const dt = (ts - lastTimeRef.current) / 1000
      lastTimeRef.current = ts
      if (dt < 0.1) {
        if (driveSource === 'can') updateFromCan(dt)
        else updateKeyboard(dt)
      }
      const r = wrap!.getBoundingClientRect()
      const w = r.width
      const h = r.height
      ctx!.fillStyle = '#0f1216'
      ctx!.fillRect(0, 0, w, h)
      const state = stateRef.current
      drawGrid(state.x, state.y, w, h)
      drawVehicle(w, h)
      hudTick += 1
      if (hudTick % 3 === 0) publishHud()
      rafRef.current = requestAnimationFrame(loop)
    }
    rafRef.current = requestAnimationFrame(loop)

    return () => {
      cancelAnimationFrame(rafRef.current)
      ro.disconnect()
    }
  }, [applyGear, driveSource, maxSpeedMmps, maxYawMrad])

  function resetPose() {
    stateRef.current = {
      ...INITIAL,
      gear: stateRef.current.gear,
      shiftMode: stateRef.current.shiftMode,
    }
    setHud((h) => ({ ...h, speedMmps: 0, yawMradS: 0, alphaDeg: 0, brakeKpa: 0 }))
  }

  return (
    <div className="workspace preview-ws" data-testid="workspace-preview">
      <header className="ws-header">
        <h1>Vehicle preview</h1>
        <p className="muted">
          2D tricycle kinematics · ego-centered · protocol signals on HOST_DRIVE_CMD [0x300]
        </p>
      </header>

      <div className="preview-layout">
        <div
          className="preview-canvas-wrap"
          ref={wrapRef}
          tabIndex={0}
          data-testid="preview-canvas-wrap"
          onFocus={() => {
            focusedRef.current = true
          }}
          onBlur={() => {
            focusedRef.current = false
            keysRef.current = {}
          }}
          onMouseDown={() => wrapRef.current?.focus()}
        >
          <canvas ref={canvasRef} data-testid="preview-canvas" />
          <div className="preview-focus-hint muted">
            Click canvas to focus · keyboard controls when source is Keyboard
          </div>
        </div>

        <aside className="preview-side panel">
          <h2>Drive source</h2>
          <div className="seg" data-testid="preview-source">
            <button
              type="button"
              className={driveSource === 'keyboard' ? 'seg-btn active' : 'seg-btn'}
              data-testid="preview-src-keyboard"
              onClick={() => setDriveSource('keyboard')}
            >
              Keyboard
            </button>
            <button
              type="button"
              className={driveSource === 'can' ? 'seg-btn active' : 'seg-btn'}
              data-testid="preview-src-can"
              onClick={() => setDriveSource('can')}
            >
              Follow CAN
            </button>
          </div>
          <p className="muted small" style={{ marginTop: 8 }}>
            CAN drive:{' '}
            {canLive ? (
              <span className="fresh fresh-live">live</span>
            ) : (
              <span className="fresh fresh-missing">missing</span>
            )}{' '}
            · inject from Control workspace to animate Follow CAN
          </p>

          <h2 className="mt-section">Telemetry</h2>
          <dl className="kv preview-kv" data-testid="preview-telemetry">
            <dt>HOST_DriveSpeed [0x300]</dt>
            <dd className="mono">
              {hud.speedMmps.toFixed(0)} mm/s ({((Math.abs(hud.speedMmps) * 3.6) / 1000).toFixed(1)}{' '}
              km/h)
            </dd>
            <dt>HOST_YawRate [0x300]</dt>
            <dd className="mono">
              {hud.yawMradS.toFixed(0)} mrad/s ({((hud.yawMradS / 1000) * (180 / Math.PI)).toFixed(1)}{' '}
              °/s)
            </dd>
            <dt>Heading θ</dt>
            <dd className="mono">{hud.thetaDeg.toFixed(1)}°</dd>
            <dt>VCU_SES_Tgt_StrAngle [0x169]</dt>
            <dd className="mono">
              {hud.alphaDeg.toFixed(1)}° (raw {(hud.alphaDeg * 10).toFixed(0)})
            </dd>
            <dt>RT_BrakePressure [0x205]</dt>
            <dd className="mono">{hud.brakeKpa.toFixed(0)} kPa</dd>
            <dt>Turn radius ρ</dt>
            <dd className="mono">{hud.radiusText}</dd>
          </dl>
          <div className="preview-dyn muted small">
            Dynamic clamp: <strong>{hud.dynClampDeg.toFixed(1)}°</strong>
            {' · '}
            Slew: <strong>{hud.dynSlewDegS.toFixed(0)}°/s</strong>
          </div>

          <h2 className="mt-section">
            HOST_Gear <span className="muted mono small">[0x300]</span>
          </h2>
          <div className="gear-row" data-testid="preview-gears">
            {GEARS.map((g) => (
              <button
                key={g}
                type="button"
                className={gear === g ? 'gear-btn active' : 'gear-btn'}
                data-testid={`preview-gear-${g}`}
                disabled={driveSource === 'can'}
                onClick={() => applyGear(g)}
              >
                {g}
              </button>
            ))}
          </div>

          <h2 className="mt-section">Shift mode</h2>
          <div className="seg">
            <button
              type="button"
              className={shiftMode === 'smart' ? 'seg-btn active' : 'seg-btn'}
              disabled={driveSource === 'can'}
              onClick={() => setShiftMode('smart')}
            >
              Adaptive
            </button>
            <button
              type="button"
              className={shiftMode === 'direct' ? 'seg-btn active' : 'seg-btn'}
              disabled={driveSource === 'can'}
              onClick={() => setShiftMode('direct')}
            >
              Direct
            </button>
          </div>

          <label className="field mt-section">
            <span className="field-label">
              Max drive speed <span className="muted">mm/s</span>
            </span>
            <div className="field-row">
              <input
                type="range"
                min={0}
                max={6000}
                step={100}
                value={maxSpeedMmps}
                onChange={(e) => setMaxSpeedMmps(Number(e.target.value))}
                disabled={driveSource === 'can'}
              />
              <span className="mono field-val">{maxSpeedMmps}</span>
            </div>
            <span className="field-hint">Allowed range: 0 to 6000 (HOST_DriveSpeed)</span>
          </label>

          <label className="field">
            <span className="field-label">
              Max yaw rate <span className="muted">mrad/s</span>
            </span>
            <div className="field-row">
              <input
                type="range"
                min={0}
                max={3000}
                step={50}
                value={maxYawMrad}
                onChange={(e) => setMaxYawMrad(Number(e.target.value))}
                disabled={driveSource === 'can'}
              />
              <span className="mono field-val">{maxYawMrad}</span>
            </div>
            <span className="field-hint">
              {((maxYawMrad / 1000) * (180 / Math.PI)).toFixed(1)} °/s · HOST_YawRate
            </span>
          </label>

          <h2 className="mt-section">Controls</h2>
          <ul className="controls-legend muted small">
            {shiftMode === 'smart' ? (
              <>
                <li>
                  <kbd>W</kbd>/<kbd>↑</kbd> Gas (auto D/S)
                </li>
                <li>
                  <kbd>S</kbd>/<kbd>↓</kbd> Brake (auto R)
                </li>
              </>
            ) : (
              <>
                <li>
                  <kbd>W</kbd>/<kbd>↑</kbd> Accelerator
                </li>
                <li>
                  <kbd>S</kbd>/<kbd>↓</kbd> Brake pedal
                </li>
              </>
            )}
            <li>
              <kbd>A</kbd>/<kbd>D</kbd> or arrows · steer
            </li>
            <li>
              <kbd>Space</kbd> E-stop (auto P)
            </li>
            <li>
              <kbd>Q</kbd>/<kbd>E</kbd> gear override
            </li>
          </ul>

          <div className="actions tight">
            <button type="button" className="secondary" data-testid="preview-reset" onClick={resetPose}>
              Reset pose
            </button>
          </div>
        </aside>
      </div>
    </div>
  )
}
