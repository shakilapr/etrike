import { writable } from "svelte/store";
import type { Bus } from "../lib/can-decoder";

export const injectorBus = writable<Bus>("high");
