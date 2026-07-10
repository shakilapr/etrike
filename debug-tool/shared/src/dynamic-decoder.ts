import yaml from "js-yaml";
import { Bus, CanFrame, CanField, CanMessageDef, FieldKind, SchemaError, UnknownMessageError, ValidationError } from "./can";

export class DynamicCanDecoder {
  private messages: Map<string, CanMessageDef> = new Map();
  private encoderHooks: Map<string, (data: number[]) => void> = new Map();

  registerEncoderHook(bus: string, id: string, hook: (data: number[]) => void): void {
    this.encoderHooks.set(`${bus}:${id}`, hook);
  }

  /** Load a YAML file string into the database. */
  loadYaml(yamlString: string): void {
    const doc = yaml.load(yamlString) as any;
    if (!doc || !doc.protocols) return;

    for (const protoName of Object.keys(doc.protocols)) {
      const proto = doc.protocols[protoName];
      const bus = proto.bus || "low";
      const byteOrder = proto.byte_order || "motorola";

      for (const msg of proto.messages || []) {
        const canIdStr = typeof msg.id === "number" ? `0x${msg.id.toString(16).padStart(3, "0").toUpperCase()}` : msg.id;
        const bitmask = new Set<number>();

        const fields: CanField[] = (msg.signals || []).map((sig: any) => {
          if (!sig.multiplexed) {
             let startBit = 0;
             if (byteOrder === "intel") {
               startBit = sig.byte * 8 + (sig.bit_offset ?? 0);
             } else {
               const byteLsb = sig.byte + Math.floor((sig.size - 1) / 8);
               startBit = (7 - byteLsb) * 8 + (sig.bit_offset ?? 0);
             }
             for(let i = 0; i < sig.size; i++) {
                 if (bitmask.has(startBit + i)) {
                     throw new SchemaError(`Overlap detected in ${canIdStr} signal ${sig.name || sig.key}`);
                 }
                 bitmask.add(startBit + i);
             }
          }
          const rawOptions = sig.values ?? sig.options;
          const hasEnum = (sig.unit === "enum") || (rawOptions && Object.keys(rawOptions).length > 0);
          const isBoolean = !hasEnum && (sig.size === 1 || (sig.min === 0 && sig.max === 1 && !sig.factor && !sig.offset));
          const kind: "boolean" | "enum" | "number" = hasEnum ? "enum" : (isBoolean ? "boolean" : "number");
          
          let options: Array<{ label: string; value: number }> | undefined = undefined;
          if (rawOptions) {
            options = Object.entries(rawOptions).map(([k, v]) => ({
              value: Number(k),
              label: String(v)
            }));
          }

          return {
            key: sig.key ?? sig.name,
            label: sig.name,
            kind,
            unit: sig.unit,
            min: sig.min,
            max: sig.max,
            options,
            // Internal decoding fields not exported in CanField interface but needed here:
            _byte: sig.byte,
            _bit_offset: sig.bit_offset ?? 0,
            _size: sig.size,
            _type: sig.type || "unsigned",
            _factor: sig.factor ?? 1.0,
            _offset: sig.offset ?? 0.0,
          };
        });

        this.messages.set(`${bus}:${canIdStr}`, {
          bus,
          id: canIdStr,
          name: msg.name,
          sender: msg.sender || "Unknown",
          dlc: typeof msg.dlc === "number" ? msg.dlc : 8,
          period: msg.cycle_ms ? `${msg.cycle_ms}ms` : "event",
          injectable: msg.sender === "Host" || msg.sender === "Any",
          fields,
          // @ts-ignore: storing byteOrder internally
          _byteOrder: byteOrder
        });
      }
    }
  }

  getMessages(): CanMessageDef[] {
    return Array.from(this.messages.values());
  }

  getDef(bus: string, id: string): CanMessageDef | undefined {
    return this.messages.get(`${bus}:${id}`);
  }

  decode(bus: string, id: string, data: number[]): Record<string, unknown> {
    const def = this.getDef(bus, id);
    if (!def) return { bus }; // unknown message: return bus context

    const buf = new Uint8Array(8);
    for (let i = 0; i < Math.min(data.length, 8); i++) buf[i] = data[i];
    const view = new DataView(buf.buffer);

    const byteOrder = (def as any)._byteOrder;
    
    const val_le = view.getBigUint64(0, true);
    const val_be = view.getBigUint64(0, false);

    const decoded: Record<string, unknown> = {};

    for (const f of def.fields as any[]) {
      const { key, _byte, _bit_offset, _size, _type, _factor, _offset } = f;

      let rawBig = 0n;
      if (byteOrder === "intel") {
        const startBit = BigInt(_byte * 8 + _bit_offset);
        const mask = (1n << BigInt(_size)) - 1n;
        rawBig = (val_le >> startBit) & mask;
      } else { // motorola
        const byteLsb = _byte + Math.floor((_size - 1) / 8);
        const startBit = BigInt((7 - byteLsb) * 8 + _bit_offset);
        const mask = (1n << BigInt(_size)) - 1n;
        rawBig = (val_be >> startBit) & mask;
      }

      let raw = Number(rawBig);

      if (_type === "signed") {
        if (rawBig & (1n << BigInt(_size - 1))) {
          raw = Number(rawBig - (1n << BigInt(_size)));
        }
      }

      let finalVal: number | boolean = raw * _factor + _offset;

      // Handle precision issues with floats
      if (typeof finalVal === "number" && (_factor % 1 !== 0 || _offset % 1 !== 0)) {
        finalVal = Math.round(finalVal * 1000000) / 1000000;
      }

      if (f.kind === "boolean") {
        finalVal = Boolean(raw);
      }

      decoded[key] = finalVal;

      // Emit enum label as {key}_name if this field has options
      if (f.options && typeof finalVal === "number") {
        const option = f.options.find((o: any) => o.value === raw);
        if (option) {
          decoded[`${key}_name`] = option.label;
        }
      }
    }

    return decoded;
  }

  encode(bus: string, id: string, values: Record<string, number | boolean>): { dlc: number; data: number[] } {
    const def = this.getDef(bus, id);
    if (!def) throw new UnknownMessageError(bus, id);

    let val_le = 0n;
    let val_be = 0n;
    const byteOrder = (def as any)._byteOrder;

    for (const f of def.fields as any[]) {
      const { key, _byte, _bit_offset, _size, _type, _factor, _offset, min, max, options } = f;
      if (values[key] === undefined) continue;

      let val = Number(values[key]);
      if (!Number.isFinite(val)) {
        throw new ValidationError(`Signal ${key} must be finite`);
      }

      if (typeof min === "number" && typeof max === "number") {
        if (val < min || val > max) {
          throw new ValidationError(`Signal ${key} value ${val} out of range [${min}, ${max}]`);
        }
      }
      
      const rawVal = Math.round((val - _offset) / _factor);
      
      if (options && f.unit === "enum") {
        if (!options.some((o: any) => o.value === rawVal)) {
          throw new ValidationError(`Signal ${key} value ${rawVal} not in allowed options`);
        }
      }

      const minRaw = _type === "signed" ? -(2 ** (_size - 1)) : 0;
      const maxRaw = _type === "signed" ? (2 ** (_size - 1)) - 1 : (2 ** _size) - 1;
      if (rawVal < minRaw || rawVal > maxRaw) {
        throw new ValidationError(`Signal ${key} raw value ${rawVal} overflows bit-width ${_size}`);
      }

      // Handle signed
      let rawBig = BigInt(rawVal);
      if (_type === "signed" && rawVal < 0) {
        rawBig = (1n << BigInt(_size)) + BigInt(rawVal);
      }

      rawBig = rawBig & ((1n << BigInt(_size)) - 1n);

      if (byteOrder === "intel") {
        const startBit = BigInt(_byte * 8 + _bit_offset);
        val_le |= (rawBig << startBit);
      } else {
        const byteLsb = _byte + Math.floor((_size - 1) / 8);
        const startBit = BigInt((7 - byteLsb) * 8 + _bit_offset);
        val_be |= (rawBig << startBit);
      }
    }

    const buf = new Uint8Array(8);
    const view = new DataView(buf.buffer);
    if (byteOrder === "intel") {
      view.setBigUint64(0, val_le, true);
    } else {
      view.setBigUint64(0, val_be, false);
    }

    const finalData = Array.from(buf.subarray(0, def.dlc));
    const hook = this.encoderHooks.get(`${bus}:${def.id}`);
    if (hook) {
      hook(finalData);
    }

    return { dlc: def.dlc, data: finalData };
  }
}
