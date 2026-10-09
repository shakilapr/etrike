/**
 * Pure computation of live CAN bus load, throughput, and error metrics.
 * Operates across High (CH0) and Low (CH1) buses at 500 kbps bit timing.
 */

import type { MessageState, Status } from '../store'
import { ECU_PROBES } from './ecuPresence'
import { findMsg, frameRecent, signalNum, signalIsOn } from './signals'

export interface BusChannelMetrics {
  bus: 'high' | 'low'
  label: string
  bitrateKbps: number
  activity: 'active' | 'quiet' | 'unseen'
  tone: 'ok' | 'warn' | 'danger' | 'muted'

  // Physical Load
  loadPct: number       // 0 - 100%
  peakPct: number       // Rolling 10-60s max
  fps: number           // Frames per second
  kbps: number          // Payload data rate kB/s

  // Hardware Error Tracking (ECU Diagnostics Telemetry)
  tec: number | null    // Transmit Error Counter
  rec: number | null    // Receive Error Counter
  busOff: boolean
  rxInvalidPerSec: number
  rxOverflowDrops: number

  // Topology Nodes
  activeNodes: number
  expectedNodes: number
  missingNodes: string[]
}

export interface BusMetricsSnapshot {
  high: BusChannelMetrics
  low: BusChannelMetrics
}

/**
 * Standard Classic CAN frame bit estimation:
 * SOF(1) + ID(11/29) + RTR/IDE/r0(3) + DLC(4) + Data(8*DLC) + CRC(15) + CRC_DEL(1) + ACK(2) + EOF(7) + IFS(3)
 * Plus stuffing bits average ~ 15-20%.
 * Approx ~ 47 + 8 * DLC + stuff bits.
 * Standard DLC=8 frame ≈ 111-125 bits on the wire.
 */
function estimateFrameBits(dlc: number, isExtended = false): number {
  const baseBits = isExtended ? 67 : 47
  const totalBits = baseBits + dlc * 8
  // Stuff bit factor ~1.15
  return Math.round(totalBits * 1.15)
}

/**
 * Internal tracker for rolling frames & peak calculation.
 */
class BusLoadTracker {
  private highPeak = 0
  private lowPeak = 0
  private peakResetTime = 0

  // Smoothed internal values to stop jitter
  private smoothHighLoad = 0
  private smoothLowLoad = 0
  private smoothHighFps = 0
  private smoothLowFps = 0
  private smoothHighKbps = 0
  private smoothLowKbps = 0
  private initialized = false

  compute(
    messages: MessageState[],
    status: Status | null,
    nowMs: number = Date.now(),
  ): BusMetricsSnapshot {

    // Reset peak every 60 seconds to stay responsive
    if (nowMs - this.peakResetTime > 60000) {
      this.highPeak = 0
      this.lowPeak = 0
      this.peakResetTime = nowMs
    }

    // 1. Calculate rate and bits from live message list
    let highFpsSum = 0
    let lowFpsSum = 0
    let highBitsSum = 0
    let lowBitsSum = 0
    let highBytesSum = 0
    let lowBytesSum = 0

    for (const m of messages) {
      if (!frameRecent(m, 2500)) continue
      const rate = m.observed_rate_hz ?? 0
      if (rate <= 0) continue

      // Approximate DLC from signals or default 8
      const dlc = 8
      const bits = estimateFrameBits(dlc) * rate
      const bytes = dlc * rate

      if (m.bus === 'high') {
        highFpsSum += rate
        highBitsSum += bits
        highBytesSum += bytes
      } else if (m.bus === 'low') {
        lowFpsSum += rate
        lowBitsSum += bits
        lowBytesSum += bytes
      }
    }

    // Bus capacity: 500,000 bps
    const BUS_CAPACITY_BPS = 500000
    const rawHighLoad = (highBitsSum / BUS_CAPACITY_BPS) * 100
    const rawLowLoad = (lowBitsSum / BUS_CAPACITY_BPS) * 100
    const rawHighKbps = highBytesSum / 1024
    const rawLowKbps = lowBytesSum / 1024

    if (!this.initialized) {
      this.smoothHighLoad = rawHighLoad
      this.smoothLowLoad = rawLowLoad
      this.smoothHighFps = highFpsSum
      this.smoothLowFps = lowFpsSum
      this.smoothHighKbps = rawHighKbps
      this.smoothLowKbps = rawLowKbps
      this.initialized = true
    } else {
      // Exponential moving average (alpha = 0.35) for smooth, stable metrics
      const alpha = 0.35
      this.smoothHighLoad = this.smoothHighLoad * (1 - alpha) + rawHighLoad * alpha
      this.smoothLowLoad = this.smoothLowLoad * (1 - alpha) + rawLowLoad * alpha
      this.smoothHighFps = this.smoothHighFps * (1 - alpha) + highFpsSum * alpha
      this.smoothLowFps = this.smoothLowFps * (1 - alpha) + lowFpsSum * alpha
      this.smoothHighKbps = this.smoothHighKbps * (1 - alpha) + rawHighKbps * alpha
      this.smoothLowKbps = this.smoothLowKbps * (1 - alpha) + rawLowKbps * alpha
    }

    const highLoadPct = Math.min(Math.round(this.smoothHighLoad), 100)
    const lowLoadPct = Math.min(Math.round(this.smoothLowLoad), 100)

    if (highLoadPct > this.highPeak) this.highPeak = highLoadPct
    if (lowLoadPct > this.lowPeak) this.lowPeak = lowLoadPct

    // 2. Hardware / Driver stats from status.adapter
    const highCh = status?.adapter?.channels?.high
    const lowCh = status?.adapter?.channels?.low

    const highActivity = (highCh?.activity?.toLowerCase() as 'active' | 'quiet' | 'unseen') || 'unseen'
    const lowActivity = (lowCh?.activity?.toLowerCase() as 'active' | 'quiet' | 'unseen') || 'unseen'

    // 3. Extract ECU Diagnostics (MCP2515 for High bus on RT, TWAI for Low bus on SYS)
    const rtDiag = findMsg(messages, 'RT_DIAG_RPT', 'high')
    const rtDiagRecent = frameRecent(rtDiag)
    const mcpTec = rtDiagRecent ? signalNum(rtDiag, 'mcp_tec') : null
    const mcpRec = rtDiagRecent ? signalNum(rtDiag, 'mcp_rec') : null
    const mcpBusOff = rtDiagRecent ? signalIsOn(rtDiag, 'mcp_bus_off') : false

    const sysDiag = findMsg(messages, 'SYS_DIAG_RPT', 'low')
    const sysDiagRecent = frameRecent(sysDiag)
    const twaiTec = sysDiagRecent ? signalNum(sysDiag, 'tec') : null
    const twaiRec = sysDiagRecent ? signalNum(sysDiag, 'rec') : null
    const rtStateLow = findMsg(messages, 'RT_STATE_RPT', 'low')
    const twaiBusOff = rtStateLow ? signalIsOn(rtStateLow, 'mcp_bus_off') : false

    // 4. Node presence roster
    const highExpected = ECU_PROBES.filter((p) => p.bus === 'high')
    const lowExpected = ECU_PROBES.filter((p) => p.bus === 'low')

    const checkProbeActive = (p: typeof ECU_PROBES[0]) => {
      const primary = messages.find(
        (m) => m.bus === p.bus && (m.can_id === p.can_id || (p.names && p.names.includes(m.name || ''))),
      )
      if (frameRecent(primary, 3000)) return true
      if (p.presenceFallbacks?.length) {
        for (const fb of p.presenceFallbacks) {
          const fbMsg = messages.find(
            (m) => m.bus === fb.bus && (m.can_id === fb.can_id || (fb.names && fb.names.includes(m.name || ''))),
          )
          if (frameRecent(fbMsg, 3000)) return true
        }
      }
      return false
    }

    const highMissing: string[] = []
    let highActiveNodes = 0
    for (const p of highExpected) {
      if (checkProbeActive(p)) {
        highActiveNodes++
      } else {
        highMissing.push(p.short)
      }
    }

    const lowMissing: string[] = []
    let lowActiveNodes = 0
    for (const p of lowExpected) {
      if (checkProbeActive(p)) {
        lowActiveNodes++
      } else {
        lowMissing.push(p.short)
      }
    }

    // 5. Evaluate overall channel tone
    const evalTone = (
      activity: string,
      loadPct: number,
      tec: number | null,
      busOff: boolean,
      missingCount: number,
      drops: number,
    ): 'ok' | 'warn' | 'danger' | 'muted' => {
      if (activity === 'unseen' || activity === 'quiet') return 'muted'
      if (busOff || (tec != null && tec >= 128) || loadPct > 80 || missingCount >= 2) return 'danger'
      if ((tec != null && tec > 0) || loadPct > 50 || missingCount > 0 || drops > 0) return 'warn'
      return 'ok'
    }

    const highDrops = highCh?.rx_overflow ?? 0
    const lowDrops = lowCh?.rx_overflow ?? 0

    return {
      high: {
        bus: 'high',
        label: 'HIGH BUS (CH0)',
        bitrateKbps: 500,
        activity: highActivity,
        tone: evalTone(highActivity, highLoadPct, mcpTec, mcpBusOff, highMissing.length, highDrops),
        loadPct: highLoadPct,
        peakPct: Math.max(highLoadPct, this.highPeak),
        fps: Math.round(this.smoothHighFps),
        kbps: Number(this.smoothHighKbps.toFixed(1)),
        tec: mcpTec,
        rec: mcpRec,
        busOff: mcpBusOff,
        rxInvalidPerSec: highCh?.rx_invalid ? Math.min(highCh.rx_invalid, 99) : 0,
        rxOverflowDrops: highDrops,
        activeNodes: highActiveNodes,
        expectedNodes: highExpected.length,
        missingNodes: highMissing,
      },
      low: {
        bus: 'low',
        label: 'LOW BUS (CH1)',
        bitrateKbps: 500,
        activity: lowActivity,
        tone: evalTone(lowActivity, lowLoadPct, twaiTec, twaiBusOff, lowMissing.length, lowDrops),
        loadPct: lowLoadPct,
        peakPct: Math.max(lowLoadPct, this.lowPeak),
        fps: Math.round(this.smoothLowFps),
        kbps: Number(this.smoothLowKbps.toFixed(1)),
        tec: twaiTec,
        rec: twaiRec,
        busOff: twaiBusOff,
        rxInvalidPerSec: lowCh?.rx_invalid ? Math.min(lowCh.rx_invalid, 99) : 0,
        rxOverflowDrops: lowDrops,
        activeNodes: lowActiveNodes,
        expectedNodes: lowExpected.length,
        missingNodes: lowMissing,
      },
    }
  }
}

export const busLoadTracker = new BusLoadTracker()
