const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const root = path.resolve(__dirname, "..");
const indexHtml = fs.readFileSync(path.join(root, "index.html"), "utf8");
const scenariosModule = require(path.join(root, "scenarios.js"));
const webConsoleModule = require(path.join(root, "web_console.js"));
const webConsoleSource = fs.readFileSync(path.join(root, "web_console.js"), "utf8");

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

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

assert.equal(
  typeof webConsoleModule.renderWebConsoleMarkup,
  "function",
  "web_console.js should export renderWebConsoleMarkup",
);

const browserGlobalSandbox = {};
vm.runInNewContext(webConsoleSource, browserGlobalSandbox);
assert.equal(
  typeof browserGlobalSandbox.UiPreviewWebConsole,
  "object",
  "web_console.js should assign UiPreviewWebConsole on browser global scope",
);
assert.equal(
  typeof browserGlobalSandbox.UiPreviewWebConsole.renderWebConsoleMarkup,
  "function",
  "browser global UiPreviewWebConsole should expose renderWebConsoleMarkup",
);

const recordingScenario = getScenarioById("recording");
assert.ok(recordingScenario, "Expected recording scenario to exist");
assert.ok(recordingScenario.sessions.length > 0, "recording scenario should include at least one session");
assert.ok(recordingScenario.tracks.length > 0, "recording scenario should include at least one track");

const markup = webConsoleModule.renderWebConsoleMarkup(recordingScenario);
const browserGlobalMarkup = browserGlobalSandbox.UiPreviewWebConsole.renderWebConsoleMarkup(recordingScenario);
assert.match(markup, /Status/);
assert.match(markup, /Sessions/);
assert.match(markup, /Tracks/);
assert.match(markup, /Settings/);
assert.match(markup, /Stop Recording/);
assert.match(markup, new RegExp(escapeRegExp(recordingScenario.sessions[0])));
assert.match(markup, new RegExp(escapeRegExp(recordingScenario.tracks[0].name)));
assert.doesNotMatch(markup, /href="#"/);
assert.equal(browserGlobalMarkup, markup, "browser-global and CommonJS renderers should match");

console.log("preview scaffold ok");
