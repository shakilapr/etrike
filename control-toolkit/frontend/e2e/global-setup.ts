import type { FullConfig } from '@playwright/test'
import { spawn, type ChildProcess } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const frontendDir = path.resolve(here, '..')
const backendDir = path.resolve(frontendDir, '../backend')
const nativeSil = path.resolve(frontendDir, '../../native-test/build-sil/sim_engine_native.exe')
const backendUrl = 'http://127.0.0.1:8010'
const frontendUrl = 'http://127.0.0.1:5174'

async function waitFor(url: string, child: ChildProcess, timeoutMs = 120_000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    if (child.exitCode != null) throw new Error(`test server exited (${child.exitCode}): ${url}`)
    try {
      const response = await fetch(url)
      if (response.ok) return
    } catch {
      // Server is still starting.
    }
    await new Promise((resolve) => setTimeout(resolve, 100))
  }
  throw new Error(`timed out waiting for ${url}`)
}

async function stop(child: ChildProcess) {
  if (child.exitCode != null || child.pid == null) return
  if (process.platform === 'win32') {
    spawn('taskkill', ['/F', '/T', '/PID', String(child.pid)])
    return
  }
  child.kill('SIGTERM')
  await Promise.race([
    new Promise<void>((resolve) => child.once('exit', () => resolve())),
    new Promise<void>((resolve) => setTimeout(resolve, 3_000)),
  ])
  if (child.exitCode == null) child.kill('SIGKILL')
}

async function isAlive(url: string): Promise<boolean> {
  try {
    const res = await fetch(url)
    return res.ok || res.status < 500
  } catch {
    return false
  }
}

export default async function globalSetup(_config: FullConfig) {
  let backend: ChildProcess | undefined
  let frontend: ChildProcess | undefined

  if (!(await isAlive(`${backendUrl}/api/v1/status`))) {
    const pythonPath = `${backendDir}${path.delimiter}${path.resolve(backendDir, '../..')}`
    backend = spawn(
      'python',
      ['-m', 'uvicorn', 'control_toolkit.main:app', '--host', '127.0.0.1', '--port', '8010'],
      {
        cwd: backendDir,
        stdio: 'ignore',
        env: {
          ...process.env,
          PYTHONPATH: process.env.PYTHONPATH
            ? `${pythonPath}${path.delimiter}${process.env.PYTHONPATH}`
            : pythonPath,
          CTK_NATIVE_SIL_EXE: nativeSil,
        },
      },
    )
  }

  if (!(await isAlive(frontendUrl))) {
    frontend = spawn(
      process.execPath,
      [path.join(frontendDir, 'node_modules/vite/bin/vite.js'), '--host', '127.0.0.1', '--port', '5174'],
      {
        cwd: frontendDir,
        stdio: 'ignore',
        env: { ...process.env, CTK_E2E_API: backendUrl },
      },
    )
  }

  try {
    if (backend) await waitFor(`${backendUrl}/api/v1/status`, backend)
    if (frontend) await waitFor(frontendUrl, frontend)
  } catch (error) {
    if (frontend) await stop(frontend)
    if (backend) await stop(backend)
    throw error
  }

  return async () => {
    if (frontend) await stop(frontend)
    if (backend) await stop(backend)
  }
}
