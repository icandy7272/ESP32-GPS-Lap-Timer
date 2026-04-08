const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const indexHtml = fs.readFileSync(path.join(root, "index.html"), "utf8");
const scenariosModule = require(path.join(root, "scenarios.js"));

assert.match(indexHtml, /id="web-console-root"/);
assert.match(indexHtml, /id="device-screen-root"/);
assert.match(indexHtml, /id="scenario-controls-root"/);

const { SCENARIOS, getScenarioById } = scenariosModule;
const requiredScenarioIds = [
  "cold-boot",
  "gps-searching",
  "ready-to-drive",
  "recording",
  "best-lap-improved",
  "off-track",
  "session-review",
  "heavy-track-library",
];

assert.ok(Array.isArray(SCENARIOS), "SCENARIOS should be an array");
assert.ok(SCENARIOS.length >= 8, "Expected at least 8 scenarios");

for (const scenarioId of requiredScenarioIds) {
  assert.ok(
    SCENARIOS.some((scenario) => scenario.id === scenarioId),
    `Missing required scenario id: ${scenarioId}`,
  );
}

const fallbackScenario = getScenarioById("missing");
assert.equal(
  fallbackScenario.id,
  "ready-to-drive",
  'getScenarioById("missing") should fall back to ready-to-drive',
);

console.log("preview scaffold ok");
