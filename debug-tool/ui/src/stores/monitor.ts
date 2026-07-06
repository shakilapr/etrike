import { writable } from "svelte/store";
import type { Bus } from "../lib/can-decoder";

export type BusFilter = Bus | "all";

export const monitorBusFilter = writable<BusFilter>("all");
export const monitorFilterText = writable("");
export const monitorExpandedKey = writable("");
export const monitorCollapsedCategories = writable<Set<string>>(new Set());
export const monitorAllExpanded = writable(true);
