import { z } from "zod";

const envSchema = z.object({
  HOST: z.string().default("127.0.0.1"),
  PORT: z.coerce.number().int().positive().default(3000),
  MQTT_HOST: z.string().default("127.0.0.1"),
  MQTT_PORT: z.coerce.number().int().positive().default(1883),
  MQTT_URL: z.string().optional(),
  MAX_FRAMES: z.coerce.number().int().positive().default(50000)
});

export interface AppConfig {
  host: string;
  port: number;
  mqttHost: string;
  mqttPort: number;
  mqttUrl: string | null;
  maxFrames: number;
}

export function loadConfig(): AppConfig {
  const parsed = envSchema.safeParse(process.env);

  if (!parsed.success) {
    console.error("Invalid configuration:");
    for (const issue of parsed.error.issues) {
      console.error(`  ${issue.path.join(".")}: ${issue.message}`);
    }
    process.exit(1);
  }

  const env = parsed.data;
  return {
    host: env.HOST,
    port: env.PORT,
    mqttHost: env.MQTT_HOST,
    mqttPort: env.MQTT_PORT,
    mqttUrl: env.MQTT_URL || null,
    maxFrames: env.MAX_FRAMES
  };
}
