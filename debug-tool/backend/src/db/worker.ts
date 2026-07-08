import { parentPort } from "worker_threads";
import { DebugStoreImpl } from "./queries";
import type { CanStats } from "../types/can";

if (!parentPort) {
  throw new Error("worker.ts must be run as a worker thread");
}

let store: DebugStoreImpl;

parentPort.on("message", (msg: any) => {
  try {
    if (msg.method === "init") {
      store = new DebugStoreImpl(msg.args[0], msg.args[1]);
      if (msg.id) parentPort?.postMessage({ id: msg.id, success: true });
    } else if (msg.type === "close") {
      store.close();
      parentPort?.postMessage({ id: msg.id, success: true });
      process.exit(0);
    } else if (msg.method) {
      // @ts-ignore
      const result = store[msg.method](...msg.args);
      if (msg.id) {
        parentPort?.postMessage({ id: msg.id, success: true, result });
      }
    } else {
      console.warn("[DB Worker] Unknown message type:", msg.type);
    }
  } catch (error) {
    if (msg.id) {
      parentPort?.postMessage({
        id: msg.id,
        success: false,
        error: error instanceof Error ? error.message : String(error)
      });
    } else {
      console.error("[DB Worker] Unhandled error:", error);
    }
  }
});
