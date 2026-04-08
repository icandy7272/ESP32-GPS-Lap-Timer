const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const indexHtml = fs.readFileSync(path.join(root, "index.html"), "utf8");

assert.match(indexHtml, /id="web-console-root"/);
assert.match(indexHtml, /id="device-screen-root"/);
assert.match(indexHtml, /id="scenario-controls-root"/);

const scripts = [
  "scenarios.js",
  "web_console.js",
  "device_screen.js",
  "app.js",
];

scripts.forEach((fileName) => {
  const needle = new RegExp(`<script[^>]+src="${fileName}"`, "i");
  assert.ok(
    needle.test(indexHtml),
    `missing script tag for ${fileName}`
  );
  assert.ok(
    fs.existsSync(path.join(root, fileName)),
    `${fileName} must exist`
  );
});

console.log("preview scaffold ok");
