const fs = require('fs');

let content = fs.readFileSync('can.ts', 'utf8');

// The single source of truth is: shared/can/can_signals.yaml
// We want to add the import at the top
if (!content.includes('import {')) {
  content = content.replace('// The single source of truth is: shared/can/can_signals.yaml\n', 
  '// The single source of truth is: shared/can/can_signals.yaml\n\nimport {\n  readI16BE, readU16BE, readI16LE, readU16LE,\n  readI24BE, readI32BE, readU32BE, readU32LE\n} from "./read-helpers";\n');
}

// Now we need to remove ONLY the readI16BE to readU32LE functions
const lines = content.split('\n');
const start = lines.findIndex(l => l.startsWith('export function readI16BE'));
const end = lines.findIndex(l => l.startsWith('function decodeSesFaults'));

if (start !== -1 && end !== -1) {
  lines.splice(start, end - start);
  fs.writeFileSync('can.ts', lines.join('\n'));
  console.log('Fixed can.ts successfully');
} else {
  console.log('Could not find start/end lines');
  console.log('start:', start, 'end:', end);
}
