import { z } from "zod";

const envSchema = z.object({
  HOST: z.string().default("127.0.0.1"),
  PORT: z.coerce.number().int().positive().default(3000),
  CAN_TRANSPORT: z.enum(["serial", "canalystii", "mqtt", "disabled"]).default("serial"),
  MQTT_PORT: z.coerce.number().int().positive().default(1883),
  SERIAL_PORT: z.string().default("COM3"),
  SERIAL_BAUD: z.coerce.number().int().positive().default(115200),
  CANALYST_PYTHON: z.string().default("python"),
  CANALYST_BITRATE: z.coerce.number().int().positive().default(500000),
  CANALYST_POLL_MS: z.coerce.number().int().positive().default(5),
  CANALYST_DEVICE_INDEX: z.coerce.number().int().nonnegative().default(0),
  CANALYST_CH0_BUS: z.enum(["high", "low"]).default("high"),
  CANALYST_CH1_BUS: z.enum(["high", "low"]).default("low"),
  DB_PATH: z.string().default("data/debug-tool.sqlite"),
  MAX_FRAMES: z.coerce.number().int().positive().default(50000)
});

export interface AppConfig {
  host: string;
  port: number;
  canTransport: "serial" | "canalystii" | "mqtt" | "disabled";
  mqttPort: number;
  serialPath: string | null;
  serialBaudRate: number;
  canalystPython: string;
  canalystBitrate: number;
  canalystPollMs: number;
  canalystDeviceIndex: number;
  canalystChannel0Bus: "high" | "low";
  canalystChannel1Bus: "high" | "low";
  dbPath: string;
  maxFrames: number;
}

export function loadConfig(): AppConfig {
  const parsed = envSchema.safeParse(process.env);

  if (!parsed.success) {
    const messages = parsed.error.issues
      .map((issue) => `  ${issue.path.join(".")}: ${issue.message}`)
      .join("\n");
    throw new Error(`Invalid configuration:\n${messages}`);
  }

  const env = parsed.data;
  return {
    host: env.HOST,
    port: env.PORT,
    canTransport: env.CAN_TRANSPORT,
    mqttPort: env.MQTT_PORT,
    serialPath: env.CAN_TRANSPORT === "serial" && env.SERIAL_PORT !== "disabled" ? env.SERIAL_PORT : null,
    serialBaudRate: env.SERIAL_BAUD,
    canalystPython: env.CANALYST_PYTHON,
    canalystBitrate: env.CANALYST_BITRATE,
    canalystPollMs: env.CANALYST_POLL_MS,
    canalystDeviceIndex: env.CANALYST_DEVICE_INDEX,
    canalystChannel0Bus: env.CANALYST_CH0_BUS,
    canalystChannel1Bus: env.CANALYST_CH1_BUS,
    dbPath: env.DB_PATH,
    maxFrames: env.MAX_FRAMES
  };
}
