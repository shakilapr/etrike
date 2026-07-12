import type { BackendStatus } from "./api";
import type { UiCanFrame, CanStats } from "./can-decoder";

export type StreamMessage =
  | { type: "can_frame"; payload: UiCanFrame }
  | { type: "can_frames_batch"; payload: UiCanFrame[] }
  | { type: "stats"; payload: CanStats }
  | { type: "cmd_ack"; payload: Record<string, unknown> }
  | { type: "status"; payload: Partial<BackendStatus> & Record<string, unknown> };
