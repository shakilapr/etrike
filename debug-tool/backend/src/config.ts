export interface AppConfig {
  host: string;
  port: number;
  mqttHost: string;
  mqttPort: number;
  mqttUrl: string;
  maxFrames: number;
}

function numberFromEnv(name: string, fallback: number): number {
  const raw = process.env[name];
  if (!raw) return fallback;
  const value = Number(raw);
  return Number.isFinite(value) ? value : fallback;
}

export function loadConfig(): AppConfig {
  const mqttHost = process.env.MQTT_HOST ?? "127.0.0.1";
  const mqttPort = numberFromEnv("MQTT_PORT", 1883);

  return {
    host: process.env.HOST ?? "127.0.0.1",
    port: numberFromEnv("PORT", 3000),
    mqttHost,
    mqttPort,
    mqttUrl: process.env.MQTT_URL ?? `mqtt://${mqttHost}:${mqttPort}`,
    maxFrames: numberFromEnv("MAX_FRAMES", 50000)
  };
}
