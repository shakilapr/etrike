import cors from "@fastify/cors";
import fastifyStatic from "@fastify/static";
import websocket from "@fastify/websocket";
import Fastify from "fastify";
import { existsSync } from "node:fs";
import { resolve } from "node:path";
import { spawn } from "node:child_process";
import { loadConfig, type AppConfig } from "./config";
import { registerCanRoutes } from "./api/can";
import { registerCommandRoutes } from "./api/cmd";
import { registerRecordingRoutes } from "./api/recordings";
import { registerSystemRoutes } from "./api/system";
import { CanalystBridge } from "./canalyst/bridge";
import { DebugStore } from "./db/queries";
import { MqttBridge } from "./mqtt/bridge";
import { SerialBridge } from "./serial/reader";
import { StreamHub } from "./ws/stream";

/**
 * Probe for a CANalyst-II adapter by running a quick Python detection script.
 * Returns true if the hardware is physically connected and accessible.
 */
async function detectCanalystii(pythonPath: string): Promise<boolean> {
  const probeScript = `
import sys
try:
    import canalystii
    dev = canalystii.CanalystDevice(device_index=0, bitrate=500000)
    print("DETECTED", flush=True)
    # Avoid noisy GC cleanup on Windows
    try:
        canalystii.device.CanalystDevice.__del__ = lambda self: None
    except Exception:
        pass
    sys.exit(0)
except Exception as e:
    print("NOT_DETECTED", str(e), flush=True)
    sys.exit(1)
`.trim();

  try {
    const result = await new Promise<string>((resolveResult) => {
      const child = spawn(pythonPath, ["-c", probeScript], {
        stdio: ["pipe", "pipe", "pipe"],
        timeout: 5000
      });
      let stdout = "";
      child.stdout.on("data", (chunk: Buffer) => { stdout += chunk.toString(); });
      child.on("close", () => resolveResult(stdout.trim()));
      child.on("error", () => resolveResult(""));
    });
    return result.startsWith("DETECTED");
  } catch {
    return false;
  }
}

async function main(): Promise<void> {
  let config: AppConfig;
  try {
    config = loadConfig();
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  }
  const startedAt = Date.now() / 1000;
  const app = Fastify({ logger: true });
  const store = new DebugStore(config.dbPath, config.maxFrames);
  const hub = new StreamHub();

  await app.register(cors, {
    origin: true
  });
  await app.register(websocket);

  // Serve built UI in production (SERVE_UI=true or when ../ui/dist exists)
  const uiDist = resolve(__dirname, "../../ui/dist");
  if (process.env.SERVE_UI === "true" || existsSync(uiDist)) {
    await app.register(fastifyStatic, {
      root: uiDist,
      prefix: "/"
    });
    // SPA fallback: serve index.html for non-API routes
    app.setNotFoundHandler((request, reply) => {
      if (request.url.startsWith("/api/")) {
        return reply.code(404).send({ error: "not found" });
      }
      void reply.sendFile("index.html");
    });
    app.log.info(`Serving UI from ${uiDist}`);
  }

  // Resolve transport: explicit env var wins, otherwise auto-detect CANalyst-II
  let effectiveTransport = config.canTransport;
  if (effectiveTransport === "serial") {
    const canalystDetected = await detectCanalystii(config.canalystPython);
    if (canalystDetected) {
      app.log.info("CANalyst-II auto-detected — using canalystii transport");
      effectiveTransport = "canalystii";
    } else {
      app.log.info("No CANalyst-II found, using serial transport");
    }
  }

  const bridge = effectiveTransport === "canalystii"
    ? new CanalystBridge(config, store, hub)
    : effectiveTransport === "mqtt"
      ? new MqttBridge(config, store, hub)
      : new SerialBridge(config, store, hub);
  await bridge.start();

  const shutdown = async () => {
    app.log.info("Shutting down debug backend");
    hub.close();
    const timeout = setTimeout(() => {
      app.log.warn("bridge.close() timed out after 5s, forcing exit");
      process.exit(1);
    }, 5000).unref();
    try {
      await bridge.close();
    } catch (error) {
      app.log.error(error, "bridge.close() failed");
    }
    clearTimeout(timeout);
    store.close();
    await app.close();
  };

  registerSystemRoutes(app, store, bridge, hub, startedAt, shutdown);
  registerCanRoutes(app, store);
  registerCommandRoutes(app, store, bridge);
  registerRecordingRoutes(app, store);
  hub.registerRoutes(app);

  process.once("SIGINT", () => {
    shutdown().then(() => process.exit(0)).catch(() => process.exit(1));
  });
  process.once("SIGTERM", () => {
    shutdown().then(() => process.exit(0)).catch(() => process.exit(1));
  });

  await app.listen({ host: config.host, port: config.port });
}

void main().catch((error) => {
  console.error(error);
  process.exit(1);
});
