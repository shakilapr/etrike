import { writable } from "svelte/store";
import type { Bus } from "../lib/can-decoder";

export const injectorBus = writable<Bus>("high");
export const injectorSelectedId = writable("0x300");
export const injectorValues = writable<Record<string, number | boolean>>({
  speed_mmps: 2000,
  yaw_rate_mrad_s: 0,
  gear: 1
});
export const injectorConfirmEstop = writable(false);
export const injectorIntervalMs = writable(20);
export const injectorCount = writable(500);
