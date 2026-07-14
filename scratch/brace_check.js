const fs = require("fs");
const path = require("path");

const content = fs.readFileSync(path.join(__dirname, "../protocol/generated/typescript/etrike-protocol.ts"), "utf8");

let braces = 0;
let lineNum = 1;
for (let i = 0; i < content.length; i++) {
  const char = content[i];
  if (char === "\n") lineNum++;
  if (char === "{") {
    braces++;
  } else if (char === "}") {
    braces--;
    if (braces === 0) {
      console.log(`Braces matched 0 at line ${lineNum}`);
    }
    if (braces < 0) {
      console.log(`Extra closing brace at line ${lineNum}`);
      break;
    }
  }
}
console.log(`Final braces count: ${braces}`);
