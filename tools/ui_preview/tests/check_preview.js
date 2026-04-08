const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const root = path.resolve(__dirname, "..");
const indexHtml = fs.readFileSync(path.join(root, "index.html"), "utf8");
const scenariosModule = require(path.join(root, "scenarios.js"));
const webConsoleModule = require(path.join(root, "web_console.js"));
const deviceScreenModule = require(path.join(root, "device_screen.js"));
const webConsoleSource = fs.readFileSync(path.join(root, "web_console.js"), "utf8");
const deviceScreenSource = fs.readFileSync(path.join(root, "device_screen.js"), "utf8");

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
assert.equal(
  typeof deviceScreenModule.renderDeviceScreenMarkup,
  "function",
  "device_screen.js should export renderDeviceScreenMarkup",
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

vm.runInNewContext(deviceScreenSource, browserGlobalSandbox);
assert.equal(
  typeof browserGlobalSandbox.UiPreviewDeviceScreen,
  "object",
  "device_screen.js should assign UiPreviewDeviceScreen on browser global scope",
);
assert.equal(
  typeof browserGlobalSandbox.UiPreviewDeviceScreen.renderDeviceScreenMarkup,
  "function",
  "browser global UiPreviewDeviceScreen should expose renderDeviceScreenMarkup",
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

const gpsSearchingScenario = getScenarioById("gps-searching");
const readyBootScenario = {
  ...getScenarioById("ready-to-drive"),
  device: {
    ...(getScenarioById("ready-to-drive").device || {}),
    screen: "boot",
    boot_state: "ready",
  },
};

const noGpsDrivingScenario = {
  ...getScenarioById("ready-to-drive"),
  device: {
    ...(getScenarioById("ready-to-drive").device || {}),
    screen: "driving",
  },
  status: {
    ...(getScenarioById("ready-to-drive").status || {}),
    gps_fix: false,
  },
};

const offTrackDrivingScenario = {
  ...getScenarioById("off-track"),
  device: {
    ...(getScenarioById("off-track").device || {}),
    screen: "driving",
    off_track: true,
  },
  status: {
    ...(getScenarioById("off-track").status || {}),
    gps_fix: true,
  },
};

const deltaUnavailableScenario = getScenarioById("ready-to-drive");
const deltaValidScenario = getScenarioById("recording");
const lapListScenario = getScenarioById("session-review");

const gpsSearchMarkup = deviceScreenModule.renderDeviceScreenMarkup(gpsSearchingScenario);
const readyBootMarkup = deviceScreenModule.renderDeviceScreenMarkup(readyBootScenario);
const noGpsDrivingMarkup = deviceScreenModule.renderDeviceScreenMarkup(noGpsDrivingScenario);
const offTrackDrivingMarkup = deviceScreenModule.renderDeviceScreenMarkup(offTrackDrivingScenario);
const deltaUnavailableMarkup = deviceScreenModule.renderDeviceScreenMarkup(deltaUnavailableScenario);
const deltaValidMarkup = deviceScreenModule.renderDeviceScreenMarkup(deltaValidScenario);
const lapListMarkup = deviceScreenModule.renderDeviceScreenMarkup(lapListScenario);

assert.match(gpsSearchMarkup, /GPS Searching\.\.\./);
assert.match(readyBootMarkup, /READY/);

assert.match(noGpsDrivingMarkup, /NO GPS/);
assert.match(offTrackDrivingMarkup, /OFF TRACK/);
assert.match(deltaUnavailableMarkup, /---/);
assert.match(deltaValidMarkup, /[+-]\d+\.\d{2}/);

assert.match(lapListMarkup, /Lap List|SESSION:/);

const browserGlobalDeviceMarkup =
  browserGlobalSandbox.UiPreviewDeviceScreen.renderDeviceScreenMarkup(deltaValidScenario);
assert.equal(
  browserGlobalDeviceMarkup,
  deltaValidMarkup,
  "browser-global and CommonJS device renderers should match",
);

console.log("preview scaffold ok");
