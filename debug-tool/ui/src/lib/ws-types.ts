import type { BackendStatus } from "./api";
import type { CanFrame, CanStats } from "./can-decoder";

export type StreamMessage =
  | { type: "can_frame"; payload: CanFrame }
  | { type: "can_frames_batch"; payload: CanFrame[] }
  | { type: "stats"; payload: CanStats }
  | { type: "cmd_ack"; payload: Record<string, unknown> }
  | { type: "status"; payload: Partial<BackendStatus> & Record<string, unknown> };
