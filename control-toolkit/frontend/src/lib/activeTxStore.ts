import { create } from 'zustand'
import { api } from '../api'
import {
  type ActiveJob,
  type CatalogMsg,
  type PausedJob,
  jobLabel,
} from './activeTx'

type ActiveTxState = {
  jobs: ActiveJob[]
  paused: PausedJob[]
  catalog: CatalogMsg[]
  expandedId: string | null
  busy: boolean
  note: string
  setExpandedId: (id: string | null) => void
  setCatalog: (c: CatalogMsg[]) => void
  refreshJobs: () => Promise<void>
  loadCatalog: () => Promise<void>
  pauseJob: (job: ActiveJob) => Promise<void>
  playPaused: (p: PausedJob) => Promise<void>
  removeRunning: (jobId: string) => Promise<void>
  removePaused: (pauseId: string) => void
  stopAll: () => Promise<void>
}

export const useActiveTxStore = create<ActiveTxState>((set, get) => ({
  jobs: [],
  paused: [],
  catalog: [],
  expandedId: null,
  busy: false,
  note: '',

  setExpandedId: (expandedId) => set({ expandedId }),
  setCatalog: (catalog) => set({ catalog }),

  loadCatalog: async () => {
    try {
      const d = await api.protocolDictionary()
      const msgs = (d.messages || []) as Array<{
        canonicalKey: string
        name: string
        can_id: number
        bus: string
      }>
      set({
        catalog: msgs.map((m) => ({
          canonicalKey: m.canonicalKey,
          name: m.name,
          can_id: m.can_id,
          bus: m.bus,
        })),
      })
    } catch {
      /* ignore — labels fall back to key/id */
    }
  },

  refreshJobs: async () => {
    try {
      const res = await api.injectionJobs()
      set({ jobs: (res.jobs || []) as ActiveJob[] })
    } catch {
      /* poll errors ignored */
    }
  },

  pauseJob: async (job) => {
    set({ busy: true })
    try {
      await api.cancelInjection(job.job_id)
      const name = jobLabel(job, get().catalog)
      set((s) => ({
        paused: [
          ...s.paused.filter((p) => p.pause_id !== job.job_id),
          {
            pause_id: job.job_id,
            bus: job.bus,
            key: job.key,
            can_id: job.can_id,
            values: { ...(job.values || {}) },
            period_ms: job.period_ms,
            counter_field: job.counter_field,
            name,
          },
        ],
        note: `Paused ${name}`,
      }))
      await get().refreshJobs()
    } catch (e) {
      set({ note: String(e) })
    } finally {
      set({ busy: false })
    }
  },

  playPaused: async (p) => {
    set({ busy: true })
    try {
      const st = await api.status()
      if (!st.session?.session_id) {
        throw new Error('No active session. Start a session first.')
      }
      if (st.session.bench_tx !== 'enabled') {
        throw new Error('Bench TX is disabled. Arm Bench TX before resuming TX.')
      }
      if (!p.key) throw new Error('Cannot resume: missing message key')
      const r = await api.inject({
        bus: p.bus,
        key: p.key,
        values: p.values,
        period_ms: p.period_ms,
        counter_field: p.counter_field,
        owner: 'ui:active-tx',
      })
      set((s) => ({
        paused: s.paused.filter((x) => x.pause_id !== p.pause_id),
        note: `Resumed ${p.name}${r.job_id ? ` [${r.job_id}]` : ''}`,
      }))
      await get().refreshJobs()
    } catch (e) {
      set({ note: String(e) })
    } finally {
      set({ busy: false })
    }
  },

  removeRunning: async (jobId) => {
    set({ busy: true })
    try {
      await api.cancelInjection(jobId)
      set((s) => ({
        paused: s.paused.filter((p) => p.pause_id !== jobId),
        expandedId: s.expandedId === jobId ? null : s.expandedId,
        note: `Removed ${jobId}`,
      }))
      await get().refreshJobs()
    } catch (e) {
      set({ note: String(e) })
    } finally {
      set({ busy: false })
    }
  },

  removePaused: (pauseId) => {
    set((s) => ({
      paused: s.paused.filter((p) => p.pause_id !== pauseId),
      expandedId: s.expandedId === pauseId ? null : s.expandedId,
      note: 'Removed paused TX',
    }))
  },

  stopAll: async () => {
    set({ busy: true })
    try {
      const res = await api.cancelAllInjections()
      set({
        paused: [],
        expandedId: null,
        note: `Cleared ${res.canceled_count} running job(s)`,
      })
      await get().refreshJobs()
    } catch (e) {
      set({ note: String(e) })
    } finally {
      set({ busy: false })
    }
  },
}))
