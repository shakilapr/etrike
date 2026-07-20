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
  if (child.exitCode != null) return
  child.kill('SIGTERM')
  await Promise.race([
    new Promise<void>((resolve) => child.once('exit', () => resolve())),
    new Promise<void>((resolve) => setTimeout(resolve, 3_000)),
  ])
  if (child.exitCode == null) child.kill('SIGKILL')
}

export default async function globalSetup(_config: FullConfig) {
  const backend = spawn(
    'python',
    ['-m', 'uvicorn', 'control_toolkit.main:app', '--host', '127.0.0.1', '--port', '8010'],
    {
      cwd: backendDir,
      stdio: 'ignore',
      env: { ...process.env, CTK_NATIVE_SIL_EXE: nativeSil },
    },
  )
  const frontend = spawn(
    process.execPath,
    [path.join(frontendDir, 'node_modules/vite/bin/vite.js'), '--host', '127.0.0.1', '--port', '5174'],
    {
      cwd: frontendDir,
      stdio: 'ignore',
      env: { ...process.env, CTK_E2E_API: backendUrl },
    },
  )

  try {
    await waitFor(`${backendUrl}/api/v1/status`, backend)
    await waitFor(frontendUrl, frontend)
  } catch (error) {
    await stop(frontend)
    await stop(backend)
    throw error
  }

  return async () => {
    await stop(frontend)
    await stop(backend)
  }
}
