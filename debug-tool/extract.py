import sys

def process():
    # 1. Read backend/src/types/can.test.ts
    with open('backend/src/types/can.test.ts', 'r') as f:
        backend_lines = f.readlines()
    
    # Extract lines 105 to 386 (0-indexed: 104 to 385)
    # Let's dynamically find it just in case
    start_idx = -1
    for i, line in enumerate(backend_lines):
        if 'describe("decodeFrame"' in line:
            start_idx = i
            break
            
    end_idx = -1
    for i in range(start_idx, len(backend_lines)):
        if 'describe("validateDataBytes"' in backend_lines[i]:
            end_idx = i
            break
            
    backend_decode = backend_lines[start_idx:end_idx]
    
    # Remove from backend
    new_backend = backend_lines[:start_idx] + backend_lines[end_idx:]
    with open('backend/src/types/can.test.ts', 'w') as f:
        f.writelines(new_backend)

    # 2. Read ui/src/lib/can-decoder.test.ts
    with open('ui/src/lib/can-decoder.test.ts', 'r') as f:
        ui_lines = f.readlines()
        
    start_encode = -1
    for i, line in enumerate(ui_lines):
        if 'describe("encodePayload"' in line:
            start_encode = i
            break
            
    end_encode = -1
    for i in range(start_encode, len(ui_lines)):
        if 'describe("formatBytes"' in ui_lines[i]:
            end_encode = i
            break
            
    ui_codec = ui_lines[start_encode:end_encode]
    
    # Remove from UI
    new_ui = ui_lines[:start_encode] + ui_lines[end_encode:]
    with open('ui/src/lib/can-decoder.test.ts', 'w') as f:
        f.writelines(new_ui)

    # 3. Create shared/tests/codec.test.ts
    import os
    os.makedirs('shared/tests', exist_ok=True)
    with open('shared/tests/codec.test.ts', 'w') as f:
        f.write('import { describe, expect, it, beforeAll } from "vitest";\n')
        f.write('import { decoder, initCanDatabase } from "../src/can";\n')
        f.write('import fs from "fs";\n')
        f.write('import path from "path";\n\n')
        f.write('beforeAll(() => {\n')
        f.write('  const high = fs.readFileSync(path.join(__dirname, "../../shared/can/can_high.yaml"), "utf8");\n')
        f.write('  const low = fs.readFileSync(path.join(__dirname, "../../shared/can/can_low.yaml"), "utf8");\n')
        f.write('  initCanDatabase(high, low);\n')
        f.write('});\n\n')
        f.write('function decodeFrame(bus: "high" | "low", id: string, data: number[]) {\n')
        f.write('  return decoder.decode(bus, id, data);\n')
        f.write('}\n\n')
        f.write('function encodePayload(bus: "high" | "low", id: string, data: Record<string, number|boolean>) {\n')
        f.write('  return decoder.encode(bus, id, data);\n')
        f.write('}\n\n')
        f.writelines(backend_decode)
        f.writelines(ui_codec)
        
if __name__ == '__main__':
    process()
