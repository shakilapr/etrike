import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const metadataPath = path.join(__dirname, 'shared/src/generated/can-metadata.ts');
const metadataContent = fs.readFileSync(metadataPath, 'utf8');

const idMap = new Map();
const sigMap = new Map();

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

  // Replace $latest["high:0x7FD"]
  content = content.replace(/\$latest\["(high|low):(0x[0-9A-Fa-f]+)"\]/g, (str, bus, rawId) => {
    const raw = rawId.toUpperCase();
    const id = idMap.get(raw) || idMap.get(raw.replace('0X', '0x'));
    if (id) {
      return `$latest[\`${bus}:\${${id}}\`]`;
    }
    return str;
  });

  // Replace ID strings inside ""
  content = content.replace(/"0x[0-9A-Fa-f]+"/g, (str) => {
    const raw = str.slice(1, -1).toUpperCase(); // remove quotes
    const id = idMap.get(raw) || idMap.get(raw.replace('0X', '0x'));
    if (id) {
      // In TS files, this works as an identifier.
      // But inside svelte components or string concatenation we need to be careful?
      // Yes, if it was "high:" + "0x7FD", returning "high:" + ID_... is fine.
      // But wait! If it's a string, we want it to be a variable reference!
      // In svelte templates it's fine too.
      return id;
    }
    return str;
  });

  // Replace SIG strings
  content = content.replace(/"([a-zA-Z0-9_]+)"/g, (str, key) => {
    const potentialSigName = Array.from(sigMap.entries()).find(e => e[0] === key)?.[1];
    
    if (potentialSigName) {
      return potentialSigName;
    }
    return str;
  });

  if (content !== originalContent) {
    const idsToImport = new Set();
    const idExtractRegex = /\b(ID_[A-Z0-9_]+|SIG_[A-Z0-9_]+)\b/g;
    let m2;
    while ((m2 = idExtractRegex.exec(content)) !== null) {
      if (Array.from(idMap.values()).includes(m2[1]) || Array.from(sigMap.values()).includes(m2[1])) {
        idsToImport.add(m2[1]);
      }
    }
    if (idsToImport.size > 0) {
      const isSvelte = filePath.endsWith('.svelte');
      const importStr = `import { ${Array.from(idsToImport).join(', ')} } from "@etrike/debug-shared";\n`;
      if (isSvelte) {
        // inject inside <script>
        content = content.replace(/<script\b[^>]*>/, (m) => m + '\n  ' + importStr);
      } else {
        content = importStr + content;
      }
    }

    fs.writeFileSync(filePath, content, 'utf8');
    console.log(`Updated ${filePath}`);
  }
}

const files = [
  path.join(__dirname, 'ui/src/stores/faults.ts'),
  path.join(__dirname, 'ui/src/components/Topbar.svelte'),
  path.join(__dirname, 'ui/src/components/Controller.svelte'),
];

for (const file of files) {
  if (fs.existsSync(file)) {
    processFile(file);
  }
}
