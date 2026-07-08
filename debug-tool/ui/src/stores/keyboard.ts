import { get, writable } from "svelte/store";
import type { Bus } from "../lib/can-decoder";

// ── Types ─────────────────────────────────────────────────────────────────────

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

// ── Stores ────────────────────────────────────────────────────────────────────

/** Continuous control — tracks which WASD+B keys are currently held. Controller polls this each tick. */
export const heldKeys = writable<Set<string>>(new Set());

/** Discrete event store — Controller subscribes for ESTOP / zero-all */
export const kbEvent = writable<KbEvent | null>(null);

/** Active CAN bus — toggled by Tab key */
export const kbBus = writable<Bus>("high");

// ── Input initialization ───────────────────────────────────────────────────────
// Consolidated from App.svelte — a single place owns all window keyboard listeners.

const HELD_KEYS = ["w", "s", "a", "d", "b"] as const;

/**
 * Attach keyboard listeners to the window. Call once in App.svelte's onMount.
 * Returns a cleanup function to remove all listeners on destroy.
 */
export function initKeyboard(): () => void {
  let lastSpaceTs = 0;

  function isFormElement(target: EventTarget | null): boolean {
    return (
      target instanceof HTMLInputElement ||
      target instanceof HTMLSelectElement ||
      target instanceof HTMLTextAreaElement
    );
  }

  // Layer 1 — held key tracking (WASD+B for the 50Hz control loop)
  function onKeyDown(e: KeyboardEvent): void {
    if (isFormElement(e.target)) return;
    const k = e.key.toLowerCase();
    if ((HELD_KEYS as readonly string[]).includes(k)) {
      e.preventDefault();
      heldKeys.update((set) => {
        const next = new Set(set);
        next.add(k);
        return next;
      });
    }
    // Layer 2 — discrete one-shot actions handled in same listener to avoid double registration
    handleDiscrete(e);
  }

  function onKeyUp(e: KeyboardEvent): void {
    if (isFormElement(e.target)) return;
    const k = e.key.toLowerCase();
    if ((HELD_KEYS as readonly string[]).includes(k)) {
      heldKeys.update((set) => {
        const next = new Set(set);
        next.delete(k);
        return next;
      });
    }
  }

  // Safety — clear held keys when window loses focus (prevents stuck keys)
  function onBlur(): void {
    heldKeys.set(new Set());
  }

  // Layer 2 — discrete actions (Tab, Esc, Space×2)
  function handleDiscrete(e: KeyboardEvent): void {
    if (isFormElement(e.target)) return;

    switch (e.key) {
      case "Escape": {
        dispatch({ type: "zero_all" });
        break;
      }
      case "Tab": {
        e.preventDefault();
        kbBus.update((b) => (b === "high" ? "low" : "high"));
        break;
      }
      case " ": {
        e.preventDefault();
        const now = Date.now();
        if (now - lastSpaceTs < 1000) {
          dispatch({ type: "estop_send" });
          lastSpaceTs = 0;
        } else {
          dispatch({ type: "estop_confirm" });
          lastSpaceTs = now;
        }
        break;
      }
    }
  }

  function dispatch(action: KbAction): void {
    const bus = get(kbBus);
    kbEvent.set({ action, bus, ts: Date.now() });
  }

  window.addEventListener("keydown", onKeyDown);
  window.addEventListener("keyup", onKeyUp);
  window.addEventListener("blur", onBlur);

  return () => {
    window.removeEventListener("keydown", onKeyDown);
    window.removeEventListener("keyup", onKeyUp);
    window.removeEventListener("blur", onBlur);
  };
}
