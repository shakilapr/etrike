const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');

const YAMLS = [
  path.join(__dirname, '../../shared/can/can_high.yaml'),
  path.join(__dirname, '../../shared/can/can_low.yaml')
];

const OUT_DIR = path.join(__dirname, 'generated');
if (!fs.existsSync(OUT_DIR)) {
  fs.mkdirSync(OUT_DIR);
}

function normalizeId(id) {
  if (typeof id === 'string') {
    return id.toLowerCase().startsWith('0x') ? `0x${parseInt(id, 16).toString(16).toUpperCase().padStart(3, '0')}` : `0x${parseInt(id).toString(16).toUpperCase().padStart(3, '0')}`;
  }
  return `0x${id.toString(16).toUpperCase().padStart(3, '0')}`;
}

let messages = [];

for (const yf of YAMLS) {
  const doc = yaml.load(fs.readFileSync(yf, 'utf8'));
  for (const proto of Object.values(doc.protocols || {})) {
    const bus = proto.bus || 'low';
    for (const msg of proto.messages || []) {
      messages.push({ ...msg, bus, normalizedId: normalizeId(msg.id) });
    }
  }
}

// Ensure uniqueness by bus:id
const unique = new Map();
for (const m of messages) {
  unique.set(`${m.bus}:${m.normalizedId}`, m);
}
messages = Array.from(unique.values()).sort((a, b) => {
  if (a.bus !== b.bus) return a.bus.localeCompare(b.bus);
  return parseInt(a.normalizedId, 16) - parseInt(b.normalizedId, 16);
});

// 1. Generate can-catalog.ts
let catalogTs = `// AUTO-GENERATED - DO NOT EDIT\n\n`;
catalogTs += `export type Bus = "high" | "low";\n\n`;
catalogTs += `export interface CanSignalDef {
  name: string;
  byte: number;
  bit_offset: number;
  size: number;
  type: "signed" | "unsigned";
  factor: number;
  offset: number;
  unit: string;
  min: number | null;
  max: number | null;
  receivers: string[];
  values: Record<string, string> | null;
  comment: string;
}\n\n`;
catalogTs += `export interface CanMessageDef {
  bus: Bus;
  id: string;
  name: string;
  dlc: number;
  sender: string;
  receivers: string[];
  cycle_ms: number;
  signals: CanSignalDef[];
}\n\n`;

catalogTs += `export const CAN_MESSAGES: CanMessageDef[] = [\n`;
for (const msg of messages) {
  catalogTs += `  {\n`;
  catalogTs += `    bus: "${msg.bus}",\n`;
  catalogTs += `    id: "${msg.normalizedId}",\n`;
  catalogTs += `    name: "${msg.name}",\n`;
  catalogTs += `    dlc: ${msg.dlc},\n`;
  catalogTs += `    sender: "${msg.sender || 'any'}",\n`;
  catalogTs += `    receivers: ${JSON.stringify(msg.receivers || [])},\n`;
  catalogTs += `    cycle_ms: ${msg.cycle_ms || 0},\n`;
  catalogTs += `    signals: [\n`;
  for (const sig of (msg.signals || [])) {
    catalogTs += `      {\n`;
    catalogTs += `        name: "${sig.name}",\n`;
    catalogTs += `        byte: ${sig.byte},\n`;
    catalogTs += `        bit_offset: ${sig.bit_offset},\n`;
    catalogTs += `        size: ${sig.size},\n`;
    catalogTs += `        type: "${sig.type || 'unsigned'}",\n`;
    catalogTs += `        factor: ${sig.factor || 1},\n`;
    catalogTs += `        offset: ${sig.offset || 0},\n`;
    catalogTs += `        unit: "${sig.unit || ''}",\n`;
    catalogTs += `        min: ${sig.min !== undefined ? sig.min : 'null'},\n`;
    catalogTs += `        max: ${sig.max !== undefined ? sig.max : 'null'},\n`;
    catalogTs += `        receivers: ${JSON.stringify(sig.receivers || [])},\n`;
    catalogTs += `        values: ${sig.values ? JSON.stringify(sig.values) : 'null'},\n`;
    catalogTs += `        comment: ${JSON.stringify(sig.comment || '')}\n`;
    catalogTs += `      },\n`;
  }
  catalogTs += `    ]\n`;
  catalogTs += `  },\n`;
}
catalogTs += `];\n\n`;

catalogTs += `export const CAN_BY_BUS_ID = new Map(CAN_MESSAGES.map((item) => [\`\${item.bus}:\${item.id}\`, item]));\n\n`;
catalogTs += `export function findMessage(bus: Bus, id: string): CanMessageDef | undefined {\n`;
catalogTs += `  return CAN_BY_BUS_ID.get(\`\${bus}:\${id}\`);\n`;
catalogTs += `}\n`;
fs.writeFileSync(path.join(OUT_DIR, 'can-catalog.ts'), catalogTs);

// 2. Generate can-decode.ts
let decodeTs = `// AUTO-GENERATED - DO NOT EDIT\n`;
decodeTs += `import { readI16BE, readU16BE, readI16LE, readU16LE, readI32BE, readU32BE, readU32LE, readI24BE } from '../src/read-helpers';\n\n`;
decodeTs += `function normalizeBytes(data: number[]): number[] {
  const bytes = data.map((value) => Number(value) & 0xff);
  while (bytes.length < 8) bytes.push(0);
  return bytes;
}\n\n`;

decodeTs += `export function decodeFrame(bus: "high"|"low", id: string, data: number[]): Record<string, unknown> {\n`;
decodeTs += `  const bytes = normalizeBytes(data);\n`;
decodeTs += `  const key = \`\${bus}:\${id}\`;\n`;
decodeTs += `  switch (key) {\n`;

for (const msg of messages) {
  if (!msg.signals || msg.signals.length === 0) continue;
  
  decodeTs += `    case "${msg.bus}:${msg.normalizedId}": {\n`;
  decodeTs += `      return {\n`;
  for (const sig of msg.signals) {
    let expr = '';
    // This handles standard aligned types
    if (msg.byte_order === 'motorola' || msg.bus === 'high') { // Assume Big Endian unless specified
      if (sig.size === 1) expr = `Boolean((bytes[${sig.byte}] ?? 0) & (1 << ${sig.bit_offset}))`;
      else if (sig.size === 8) expr = `bytes[${sig.byte}] ?? 0`;
      else if (sig.size === 16 && sig.type === 'signed') expr = `readI16BE(bytes, ${sig.byte})`;
      else if (sig.size === 16 && sig.type !== 'signed') expr = `readU16BE(bytes, ${sig.byte})`;
      else if (sig.size === 24 && sig.type === 'signed') expr = `readI24BE(bytes, ${sig.byte})`;
      else if (sig.size === 32 && sig.type === 'signed') expr = `readI32BE(bytes, ${sig.byte})`;
      else if (sig.size === 32 && sig.type !== 'signed') expr = `readU32BE(bytes, ${sig.byte})`;
    } else {
      if (sig.size === 1) expr = `Boolean((bytes[${sig.byte}] ?? 0) & (1 << ${sig.bit_offset}))`;
      else if (sig.size === 8) expr = `bytes[${sig.byte}] ?? 0`;
      else if (sig.size === 16 && sig.type === 'signed') expr = `readI16LE(bytes, ${sig.byte})`;
      else if (sig.size === 16 && sig.type !== 'signed') expr = `readU16LE(bytes, ${sig.byte})`;
      else if (sig.size === 32 && sig.type === 'signed') expr = `readI32LE(bytes, ${sig.byte}) /* FIXME */`;
      else if (sig.size === 32 && sig.type !== 'signed') expr = `readU32LE(bytes, ${sig.byte})`;
    }
    
    // Fallback if not handled by standard alignments
    if (!expr) {
      if (sig.size === 2) {
         expr = `((bytes[${sig.byte}] ?? 0) >> ${sig.bit_offset}) & 0x03`;
      } else if (sig.size === 4) {
         expr = `((bytes[${sig.byte}] ?? 0) >> ${sig.bit_offset}) & 0x0f`;
      } else {
         expr = `((bytes[${sig.byte}] ?? 0) >> ${sig.bit_offset}) & ((1 << ${sig.size}) - 1)`;
      }
    }

    if (sig.factor !== undefined && sig.factor !== 1) {
      expr = `(${expr} * ${sig.factor})`;
    }
    if (sig.offset !== undefined && sig.offset !== 0) {
      expr = `(${expr} + ${sig.offset})`;
    }
    
    decodeTs += `        "${sig.name}": ${expr},\n`;
  }
  decodeTs += `      };\n`;
  decodeTs += `    }\n`;
}

decodeTs += `    default: return {};\n`;
decodeTs += `  }\n`;
decodeTs += `}\n`;
fs.writeFileSync(path.join(OUT_DIR, 'can-decode.ts'), decodeTs);

console.log('Generated can-catalog.ts and can-decode.ts');
