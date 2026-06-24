import { z } from "zod";

const envSchema = z.object({
  HOST: z.string().default("127.0.0.1"),
  PORT: z.coerce.number().int().positive().default(3000),
  SERIAL_PORT: z.string().default("COM3"),
  SERIAL_BAUD: z.coerce.number().int().positive().default(115200),
  DB_PATH: z.string().default("data/debug-tool.sqlite"),
  MAX_FRAMES: z.coerce.number().int().positive().default(50000)
});

export interface AppConfig {
  host: string;
  port: number;
  serialPath: string | null;
  serialBaudRate: number;
  dbPath: string;
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
    serialPath: env.SERIAL_PORT === "disabled" ? null : env.SERIAL_PORT,
    serialBaudRate: env.SERIAL_BAUD,
    dbPath: env.DB_PATH,
    maxFrames: env.MAX_FRAMES
  };
}
