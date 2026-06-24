import { writable } from "svelte/store";
import type { Bus } from "../lib/can-decoder";

// Discrete actions — one-shot events for Tab/Esc/Space
export type KbAction =
  | { type: "estop_confirm" } // Space (single press)
  | { type: "estop_send" }    // Space (double press)
  | { type: "zero_all" };     // Esc

export interface KbEvent {
  action: KbAction;
  bus: Bus;
  ts: number;
}

// Continuous control — tracks which WASD keys are currently held
// Controller polls this each tick of its 50Hz loop
export const heldKeys = writable<Set<string>>(new Set());

// Discrete event store — Controller subscribes for ESTOP / zero-all
export const kbEvent = writable<KbEvent | null>(null);

// Active bus — toggled by Tab key in App.svelte
export const kbBus = writable<Bus>("high");
