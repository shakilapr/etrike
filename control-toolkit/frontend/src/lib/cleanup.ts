import { api } from '../api'

/**
 * Best-effort release of motion / analysis ownership.
 * Surfaces the first failure instead of silent swallow.
 */
export async function cleanupControlStreams(
  reason: string,
  opts?: { direct?: boolean; analysis?: boolean },
): Promise<{ ok: boolean; detail: string }> {
  const errors: string[] = []
  try {
    await api.controlRelease(reason)
  } catch (e) {
    errors.push(`release: ${String(e)}`)
  }
  if (opts?.analysis !== false) {
    try {
      await api.stopAnalysis()
    } catch (e) {
      errors.push(`analysis: ${String(e)}`)
    }
  }
  if (opts?.direct !== false) {
    for (const channel of ['motor', 'steering', 'brake'] as const) {
      try {
        await api.controlDirect({ channel, enabled: false })
      } catch (e) {
        errors.push(`${channel}: ${String(e)}`)
      }
    }
  }
  if (errors.length) {
    return { ok: false, detail: `Cleanup partial (${reason}): ${errors[0]}` }
  }
  return { ok: true, detail: `Cleanup ok (${reason})` }
}

/** True when an intent error is only a stale sequence race (ignore). */
export function isStaleSequenceError(msg: string): boolean {
  return /stale[_ ]sequence|intent sequence \d+ < current/i.test(msg)
}
