import { writable } from "svelte/store";
import type { Bus } from "../lib/can-decoder";

export type KbAction =
  | { type: "speed_up" }      // W
  | { type: "speed_down" }    // S
  | { type: "yaw_left" }      // A (high) / angle_left (low)
  | { type: "yaw_right" }     // D (high) / angle_right (low)
  | { type: "brake_set" }     // B
  | { type: "brake_release" } // R
  | { type: "estop_confirm" } // Space (single press)
  | { type: "estop_send" }    // Space (double press)
  | { type: "zero_all" };     // Esc

export interface KbEvent {
  action: KbAction;
  bus: Bus;
  ts: number;
}

export const kbEvent = writable<KbEvent | null>(null);
export const kbBus = writable<Bus>("high");
