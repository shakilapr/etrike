import * as fs from 'fs';
import * as path from 'path';
import type { SimFrame } from '../core/types.js';

export interface TraceRecord {
  time_ms: number;
  bus: string;
  id: string; // "0x204"
  dlc: number;
  data: string; // "00 01 02"
  decoded?: Record<string, any>;
}

function bufferToHexSpace(buffer: Uint8Array): string {
  return Array.from(buffer).map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
}

export function exportFramesToJsonl(frames: SimFrame[], outputPath: string) {
  const records: TraceRecord[] = frames.map(f => ({
    time_ms: f.simTimeMs,
    bus: f.bus,
    id: typeof f.canId === 'string' ? f.canId : `0x${f.canId.toString(16).toUpperCase()}`,
    dlc: f.dlc,
    data: bufferToHexSpace(f.data),
    decoded: f.decoded
  }));

  const lines = records.map(r => JSON.stringify(r)).join('\n');
  
  const dir = path.dirname(outputPath);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
  
  fs.writeFileSync(outputPath, lines + '\n');
  console.log(`Exported ${records.length} frames to ${outputPath}`);
}
