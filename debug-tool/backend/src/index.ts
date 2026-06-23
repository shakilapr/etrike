import cors from "@fastify/cors";
import fastifyStatic from "@fastify/static";
import websocket from "@fastify/websocket";
import Fastify from "fastify";
import { existsSync } from "node:fs";
import { resolve } from "node:path";
import { loadConfig } from "./config";
import { registerCanRoutes } from "./api/can";
import { registerCommandRoutes } from "./api/cmd";
import { registerRecordingRoutes } from "./api/recordings";
import { registerSystemRoutes } from "./api/system";
import { DebugStore } from "./db/queries";
import { startMqttBroker, type MqttBrokerHandle } from "./mqtt/broker";
import { MqttBridge } from "./mqtt/client";
import { StreamHub } from "./ws/stream";

async function main(): Promise<void> {
  const config = loadConfig();
  const startedAt = Date.now() / 1000;
  const app = Fastify({ logger: true });
  const store = new DebugStore(config.maxFrames);
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
    app.setNotFoundHandler((_request, reply) => {
      void reply.sendFile("index.html");
    });
    app.log.info(`Serving UI from ${uiDist}`);
  }

  let mqttBroker: MqttBrokerHandle | null = null;
  try {
    mqttBroker = await startMqttBroker(config.mqttPort, config.mqttHost);
    app.log.info(`MQTT broker listening on ${config.mqttHost}:${config.mqttPort}`);
  } catch (error) {
    app.log.warn({ error }, "Embedded MQTT broker did not start; using configured MQTT_URL only");
  }

  const bridge = new MqttBridge(config, store, hub);
  bridge.start();

  registerSystemRoutes(app, store, bridge, hub, startedAt);
  registerCanRoutes(app, store);
  registerCommandRoutes(app, store, bridge);
  registerRecordingRoutes(app, store);
  hub.registerRoutes(app);

  const shutdown = async () => {
    app.log.info("Shutting down debug backend");
    await bridge.close();
    if (mqttBroker) {
      await mqttBroker.close();
    }
    await app.close();
  };

  process.once("SIGINT", () => {
    void shutdown().then(() => process.exit(0));
  });
  process.once("SIGTERM", () => {
    void shutdown().then(() => process.exit(0));
  });

  await app.listen({ host: config.host, port: config.port });
}

void main().catch((error) => {
  console.error(error);
  process.exit(1);
});
