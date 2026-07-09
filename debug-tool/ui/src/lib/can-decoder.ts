import { decoder, initCanDatabase } from "@etrike/debug-shared";
export * from "@etrike/debug-shared";
import type { Bus } from "@etrike/debug-shared";

import highYaml from "../../../../shared/can/can_high.yaml?raw";
import lowYaml from "../../../../shared/can/can_low.yaml?raw";

// Initialize the decoder for the frontend at module load time
initCanDatabase(highYaml, lowYaml);

export function encodePayload(bus: Bus, id: string, values: Record<string, number | boolean>): { dlc: number; data: number[] } {
  return decoder.encode(bus, id, values);
}
