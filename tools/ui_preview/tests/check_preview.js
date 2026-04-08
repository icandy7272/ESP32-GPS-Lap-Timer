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

function formatLapTimeForExpectation(timeMs) {
  const minutes = Math.floor(timeMs / 60000);
  const seconds = Math.floor((timeMs % 60000) / 1000);
  const hundredths = Math.floor((timeMs % 1000) / 10);
  return String(minutes) + ":" + String(seconds).padStart(2, "0") + "." + String(hundredths).padStart(2, "0");
}

assert.match(indexHtml, /id="web-console-root"/);
assert.match(indexHtml, /id="device-screen-root"/);
assert.match(indexHtml, /id="scenario-controls-root"/);

const { SCENARIOS, DEFAULT_SCENARIO_ID, getScenarioById } = scenariosModule;
const requiredScenarioIds = [
  "cold-boot",
  "gps-searching",
  "ready-to-drive",
  "no-gps-driving",
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
  if (scenario.device && Object.prototype.hasOwnProperty.call(scenario.device, "laps")) {
    assert.ok(
      Array.isArray(scenario.device.laps),
      `Scenario ${scenario.id} laps should be an array when present`,
    );
  }
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
assert.equal(
  typeof deviceScreenModule.renderDeviceScreenComparisonMarkup,
  "function",
  "device_screen.js should export renderDeviceScreenComparisonMarkup",
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
assert.equal(
  typeof browserGlobalSandbox.UiPreviewDeviceScreen.renderDeviceScreenComparisonMarkup,
  "function",
  "browser global UiPreviewDeviceScreen should expose renderDeviceScreenComparisonMarkup",
);

const recordingScenario = getScenarioById("recording");
const heavyTrackLibraryScenario = getScenarioById("heavy-track-library");
const coldBootScenario = getScenarioById("cold-boot");
const bestLapImprovedScenario = getScenarioById("best-lap-improved");
const sessionReviewScenario = getScenarioById("session-review");
assert.ok(recordingScenario, "Expected recording scenario to exist");
assert.ok(recordingScenario.sessions.length > 0, "recording scenario should include at least one session");
assert.ok(recordingScenario.tracks.length > 0, "recording scenario should include at least one track");
assert.ok(coldBootScenario, "Expected cold-boot scenario to exist");
assert.ok(bestLapImprovedScenario, "Expected best-lap-improved scenario to exist");
assert.ok(sessionReviewScenario, "Expected session-review scenario to exist");
assert.ok(
  heavyTrackLibraryScenario.sessions.length > 1,
  "heavy-track-library should include multiple sessions",
);
assert.ok(
  heavyTrackLibraryScenario.tracks.length > 1,
  "heavy-track-library should include multiple tracks",
);

const markup = webConsoleModule.renderWebConsoleMarkup(recordingScenario);
const browserGlobalMarkup = browserGlobalSandbox.UiPreviewWebConsole.renderWebConsoleMarkup(recordingScenario);
const heavyTrackLibraryMarkup = webConsoleModule.renderWebConsoleMarkup(heavyTrackLibraryScenario);
assert.match(markup, /Status/);
assert.match(markup, /Sessions/);
assert.match(markup, /Tracks/);
assert.match(markup, /Settings/);
assert.match(markup, /Stop Recording/);
assert.match(markup, new RegExp(escapeRegExp(recordingScenario.sessions[0])));
assert.match(markup, new RegExp(escapeRegExp(recordingScenario.tracks[0].name)));
assert.doesNotMatch(markup, /href="#"/);
assert.equal(browserGlobalMarkup, markup, "browser-global and CommonJS renderers should match");
assert.match(
  heavyTrackLibraryMarkup,
  new RegExp(escapeRegExp(heavyTrackLibraryScenario.sessions[1])),
);
assert.match(
  heavyTrackLibraryMarkup,
  new RegExp(escapeRegExp(heavyTrackLibraryScenario.tracks[1].name)),
);
assert.doesNotMatch(
  heavyTrackLibraryMarkup,
  /No sessions|No tracks/,
  "heavy-track-library should not render empty-state text",
);

const gpsSearchingScenario = getScenarioById("gps-searching");
const readyBootScenario = {
  ...getScenarioById("ready-to-drive"),
  device: {
    ...(getScenarioById("ready-to-drive").device || {}),
    screen: "boot",
    boot_state: "ready",
  },
};

const recoveryBootScenario = {
  ...getScenarioById("cold-boot"),
  device: {
    ...(getScenarioById("cold-boot").device || {}),
    screen: "boot",
    boot_state: "recovery",
  },
};

const noGpsDrivingScenario = getScenarioById("no-gps-driving");

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
const lapListScenarioLaps = ((lapListScenario.device || {}).laps || []);
const lapListBestMismatchScenario = {
  ...lapListScenario,
  device: {
    ...(lapListScenario.device || {}),
    best_lap_number: 2,
  },
};
const idleStatusScenario = {
  ...getScenarioById("heavy-track-library"),
  device: {
    ...(getScenarioById("heavy-track-library").device || {}),
    screen: "status",
  },
  status: {
    ...(getScenarioById("heavy-track-library").status || {}),
    recording: false,
  },
};
const recordingStatusScenario = {
  ...getScenarioById("recording"),
  device: {
    ...(getScenarioById("recording").device || {}),
    screen: "status",
  },
  status: {
    ...(getScenarioById("recording").status || {}),
    recording: true,
  },
};

const gpsSearchMarkup = deviceScreenModule.renderDeviceScreenMarkup(gpsSearchingScenario);
const readyBootMarkup = deviceScreenModule.renderDeviceScreenMarkup(readyBootScenario);
const recoveryBootMarkup = deviceScreenModule.renderDeviceScreenMarkup(recoveryBootScenario);
const noGpsDrivingMarkup = deviceScreenModule.renderDeviceScreenMarkup(noGpsDrivingScenario);
const offTrackDrivingMarkup = deviceScreenModule.renderDeviceScreenMarkup(offTrackDrivingScenario);
const deltaUnavailableMarkup = deviceScreenModule.renderDeviceScreenMarkup(deltaUnavailableScenario);
const deltaValidMarkup = deviceScreenModule.renderDeviceScreenMarkup(deltaValidScenario);
const comparisonMarkup = deviceScreenModule.renderDeviceScreenComparisonMarkup(deltaValidScenario);
const lapListMarkup = deviceScreenModule.renderDeviceScreenMarkup(lapListScenario);
const lapListBestMismatchMarkup = deviceScreenModule.renderDeviceScreenMarkup(lapListBestMismatchScenario);
const idleStatusMarkup = deviceScreenModule.renderDeviceScreenMarkup(idleStatusScenario);
const recordingStatusMarkup = deviceScreenModule.renderDeviceScreenMarkup(recordingStatusScenario);

assert.match(gpsSearchMarkup, /GPS Searching\.\.\./);
assert.match(readyBootMarkup, /READY/);
assert.match(recoveryBootMarkup, /Session Recovered/);

assert.match(noGpsDrivingMarkup, /NO GPS/);
assert.match(offTrackDrivingMarkup, /OFF TRACK/);
assert.match(deltaUnavailableMarkup, /---/);
assert.match(deltaValidMarkup, /[+-]\d+\.\d{2}/);
assert.match(comparisonMarkup, /Original TFT/);
assert.match(comparisonMarkup, /Polished TFT/);
assert.match(comparisonMarkup, /device-preview device-preview--original/);
assert.match(comparisonMarkup, /device-preview device-preview--polished/);

assert.match(lapListMarkup, /Lap List|SESSION:/);
assert.ok(
  lapListScenarioLaps.length >= 8,
  "session-review should provide enough laps to reveal paging or scrolling",
);
assert.match(
  lapListMarkup,
  new RegExp(escapeRegExp(formatLapTimeForExpectation(lapListScenarioLaps[0].lap_time_ms))),
);
assert.match(
  lapListMarkup,
  new RegExp(
    escapeRegExp(formatLapTimeForExpectation(lapListScenarioLaps[Math.min(6, lapListScenarioLaps.length - 1)].lap_time_ms)),
  ),
);
assert.match(
  lapListBestMismatchMarkup,
  /0:50\.89<\/span><span class="device-lap-list__delta device-time">BEST/,
);
assert.doesNotMatch(
  lapListBestMismatchMarkup,
  /0:52\.22<\/span><span class="device-lap-list__delta device-time">BEST/,
);
assert.match(idleStatusMarkup, /class="device-status__line">Recording: Idle/);
assert.doesNotMatch(idleStatusMarkup, /class="device-status__line device-status__line--recording">Recording: Idle/);
assert.match(recordingStatusMarkup, /class="device-status__line device-status__line--recording">Recording: REC/);
assert.match(idleStatusMarkup, /SD: 12\.7 GB free/);
assert.match(idleStatusMarkup, /Battery: N\/A/);
assert.match(idleStatusMarkup, /Uptime: 6m 52s/);
assert.match(idleStatusMarkup, /FW: v1\.0\.0/);
assert.match(deviceScreenModule.renderDeviceScreenMarkup(getScenarioById("cold-boot")), /v1\.0\.0/);

const browserGlobalDeviceMarkup =
  browserGlobalSandbox.UiPreviewDeviceScreen.renderDeviceScreenMarkup(deltaValidScenario);
assert.equal(
  browserGlobalDeviceMarkup,
  deltaValidMarkup,
  "browser-global and CommonJS device renderers should match",
);

console.log("preview scaffold ok");
