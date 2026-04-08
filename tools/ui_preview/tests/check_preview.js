const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const indexHtml = fs.readFileSync(path.join(root, "index.html"), "utf8");
const scenariosModule = require(path.join(root, "scenarios.js"));

assert.match(indexHtml, /id="web-console-root"/);
assert.match(indexHtml, /id="device-screen-root"/);
assert.match(indexHtml, /id="scenario-controls-root"/);

const { SCENARIOS, DEFAULT_SCENARIO_ID, getScenarioById } = scenariosModule;
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
assert.equal(DEFAULT_SCENARIO_ID, "ready-to-drive");

for (const scenarioId of requiredScenarioIds) {
  assert.ok(
    SCENARIOS.some((scenario) => scenario.id === scenarioId),
    `Missing required scenario id: ${scenarioId}`,
  );
}

for (const scenario of SCENARIOS) {
  assert.ok(
    Array.isArray(scenario.tracks) && scenario.tracks.every((track) => typeof track.id === "string"),
    `Scenario ${scenario.id} tracks should use string ids`,
  );
  assert.ok(
    Array.isArray(scenario.sessions) && scenario.sessions.every((session) => typeof session === "string"),
    `Scenario ${scenario.id} sessions should be filename strings`,
  );
}

const fallbackScenario = getScenarioById("missing");
assert.equal(
  fallbackScenario.id,
  "ready-to-drive",
  'getScenarioById("missing") should fall back to ready-to-drive',
);

console.log("preview scaffold ok");
