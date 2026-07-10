import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const metadataPath = path.join(__dirname, 'shared/src/generated/can-metadata.ts');
const metadataContent = fs.readFileSync(metadataPath, 'utf8');

const idMap = new Map(); // "0x206" -> "ID_MTR_MOTOR_FBK"
const sigMap = new Map(); // "actual_speed_mmps" -> "SIG_MTR_MOTOR_FBK_ACTUAL_SPEED_MMPS"

const idRegex = /export const (ID_[A-Z0-9_]+) = "([^"]+)";/g;
let match;
while ((match = idRegex.exec(metadataContent)) !== null) {
  idMap.set(match[2], match[1]);
  const num = parseInt(match[2], 16);
  idMap.set(`0x${num.toString(16).padStart(3, '0').toUpperCase()}`, match[1]);
}

const sigRegex = /export const (SIG_[A-Z0-9_]+) = "([^"]+)";/g;
while ((match = sigRegex.exec(metadataContent)) !== null) {
  sigMap.set(match[2], match[1]);
}

function processFile(filePath) {
  let content = fs.readFileSync(filePath, 'utf8');
  let originalContent = content;

  // Replace IDs: "0xNNN"
  content = content.replace(/"0x[0-9A-Fa-f]+"/g, (str) => {
    const raw = str.slice(1, -1).toUpperCase(); // remove quotes
    const id = idMap.get(raw) || idMap.get(raw.replace('0X', '0x'));
    if (id) {
      return id;
    }
    return str; // keep original if not found
  });

  if (content !== originalContent) {
    const idsToImport = new Set();
    const idExtractRegex = /\bID_[A-Z0-9_]+\b/g;
    let m2;
    while ((m2 = idExtractRegex.exec(content)) !== null) {
      if (Array.from(idMap.values()).includes(m2[0])) {
        idsToImport.add(m2[0]);
      }
    }
    if (idsToImport.size > 0) {
      const importStr = `import { ${Array.from(idsToImport).join(', ')} } from "@etrike/debug-shared";\n`;
      content = importStr + content;
    }

    fs.writeFileSync(filePath, content, 'utf8');
    console.log(`Updated ${filePath}`);
  }
}

const dirs = [
  path.join(__dirname, 'backend/src/sim/ecus'),
  path.join(__dirname, 'ui/src/stores'),
];

for (const dir of dirs) {
  const files = fs.readdirSync(dir);
  for (const file of files) {
    if (file.endsWith('.ts')) {
      processFile(path.join(dir, file));
    }
  }
}
