const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const indexHtml = fs.readFileSync(path.join(root, "index.html"), "utf8");

assert.match(indexHtml, /id="web-console-root"/);
assert.match(indexHtml, /id="device-screen-root"/);
assert.match(indexHtml, /id="scenario-controls-root"/);

console.log("preview scaffold ok");
