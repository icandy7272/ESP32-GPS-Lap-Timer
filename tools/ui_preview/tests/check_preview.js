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
const stylesSource = fs.readFileSync(path.join(root, "styles.css"), "utf8");

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
assert.match(stylesSource, /\.web-console__gps-bar\s*\{/);
assert.match(stylesSource, /\.web-console__gps-bar--ok\s*\{/);
assert.match(stylesSource, /\.web-console__gps-bar--warn\s*\{/);
assert.match(stylesSource, /\.web-console__mark-button--marked\s*\{/);
assert.match(stylesSource, /\.web-console__sector-card\s*\{/);
assert.match(stylesSource, /\.device-panel__description\s*\{[\s\S]*font-size: 0\.78rem;/);
assert.match(stylesSource, /\.device-preview--polished \.device-status\s*\{[\s\S]*font-size: 0\.88rem;/);
assert.match(stylesSource, /\.device-preview--polished \.device-driving__delta\s*\{[\s\S]*font-size: clamp\(3\.1rem, 11vw, 4\.1rem\);/);
assert.match(stylesSource, /\.device-status__line\s*\{[\s\S]*overflow: visible;[\s\S]*min-height: 1\.24em;[\s\S]*line-height: 1\.24;/);
assert.match(stylesSource, /\.device-status__text\s*\{[\s\S]*overflow: hidden;[\s\S]*text-overflow: ellipsis;[\s\S]*white-space: nowrap;/);
assert.match(stylesSource, /\.device-preview--polished \.device-driving--sector-focus \.device-driving__delta\s*\{[\s\S]*font-size: clamp\(2\.7rem, 9\.4vw, 3\.55rem\);/);
assert.match(stylesSource, /\.device-sector-ribbon\s*\{[\s\S]*grid-auto-flow: column;[\s\S]*grid-auto-columns: minmax\(0, 1fr\);[\s\S]*gap: 0;/);
assert.match(stylesSource, /\.device-sector\s*\{[\s\S]*border-radius: 0;/);

const { SCENARIOS, DEFAULT_SCENARIO_ID, getScenarioById } = scenariosModule;
const requiredScenarioIds = [
  "cold-boot",
  "gps-searching",
  "ready-to-drive",
  "track-creation-stabilizing",
  "no-gps-driving",
  "manual-track-selected",
  "guided-track-review",
  "recording",
  "recording-slow",
  "sector-focus",
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
  Array.isArray(scenario.sessions) && scenario.sessions.every((session) => {
    return typeof session === "string" ||
      (session && typeof session === "object" && typeof session.filename === "string");
  }),
  `Scenario ${scenario.id} sessions should be filename strings or metadata objects`,
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
const manualTrackSelectedScenario = getScenarioById("manual-track-selected");
const guidedTrackReviewScenario = getScenarioById("guided-track-review");
const heavyTrackLibraryScenario = getScenarioById("heavy-track-library");
const coldBootScenario = getScenarioById("cold-boot");
const bestLapImprovedScenario = getScenarioById("best-lap-improved");
const sessionReviewScenario = getScenarioById("session-review");
const trackCreationStabilizingScenario = getScenarioById("track-creation-stabilizing");
assert.ok(recordingScenario, "Expected recording scenario to exist");
assert.ok(manualTrackSelectedScenario, "Expected manual-track-selected scenario to exist");
assert.ok(guidedTrackReviewScenario, "Expected guided-track-review scenario to exist");
assert.ok(recordingScenario.sessions.length > 0, "recording scenario should include at least one session");
assert.ok(recordingScenario.tracks.length > 0, "recording scenario should include at least one track");
assert.ok(coldBootScenario, "Expected cold-boot scenario to exist");
assert.ok(bestLapImprovedScenario, "Expected best-lap-improved scenario to exist");
assert.ok(sessionReviewScenario, "Expected session-review scenario to exist");
assert.ok(trackCreationStabilizingScenario, "Expected track-creation-stabilizing scenario to exist");
assert.ok(
  heavyTrackLibraryScenario.sessions.length > 1,
  "heavy-track-library should include multiple sessions",
);
assert.ok(
  heavyTrackLibraryScenario.tracks.length > 1,
  "heavy-track-library should include multiple tracks",
);

const markup = webConsoleModule.renderWebConsoleMarkup(recordingScenario);
const manualTrackMarkup = webConsoleModule.renderWebConsoleMarkup(manualTrackSelectedScenario);
const guidedReviewMarkup = webConsoleModule.renderWebConsoleMarkup(guidedTrackReviewScenario);
const browserGlobalMarkup = browserGlobalSandbox.UiPreviewWebConsole.renderWebConsoleMarkup(recordingScenario);
const heavyTrackLibraryMarkup = webConsoleModule.renderWebConsoleMarkup(heavyTrackLibraryScenario);
const stabilizingMarkup = webConsoleModule.renderWebConsoleMarkup(trackCreationStabilizingScenario);
assert.match(markup, /Status/);
assert.match(markup, /Sessions/);
assert.match(markup, /Tracks/);
assert.match(markup, /Advanced|Settings/);
assert.match(markup, /Track Creation/);
assert.match(markup, /Current Track/);
assert.match(markup, /Mark P1|✓ P1/);
assert.match(markup, /Mark P2|✓ P2/);
assert.match(markup, /Sector Splits \(optional\)/);
assert.match(markup, /Current position:/);
assert.match(markup, /Stop Recording/);
assert.match(manualTrackMarkup, /Selected manually/);
assert.match(manualTrackMarkup, /Ready to Record/);
assert.match(manualTrackMarkup, /Current/);
assert.match(manualTrackMarkup, /Nearby Tracks/);
assert.match(manualTrackMarkup, /Resume Auto/);
assert.match(manualTrackMarkup, /36 m/);
assert.match(manualTrackMarkup, /18 m/);
assert.match(manualTrackMarkup, /96 m/);
{
  const nearbySection = manualTrackMarkup.slice(manualTrackMarkup.indexOf("Nearby Tracks"));
  assert.ok(
    nearbySection.indexOf("Zhuhai International Circuit") <
      nearbySection.indexOf("Ningbo Kart Center"),
    "nearby alternatives should be sorted by distance",
  );
}
assert.match(guidedReviewMarkup, /Name/);
assert.match(guidedReviewMarkup, /Start\/Finish/);
assert.match(guidedReviewMarkup, /Review/);
assert.match(guidedReviewMarkup, /Re-mark P1/);
assert.match(guidedReviewMarkup, /Re-mark P2/);
assert.match(guidedReviewMarkup, /becomes current/i);
assert.match(guidedReviewMarkup, /<svg/i);
assert.match(stabilizingMarkup, /Hold still\.\.\. stabilizing/i);
assert.match(stabilizingMarkup, /web-console__gps-bar--warn/);
assert.match(
  stabilizingMarkup,
  /web-console__mark-button web-console__mark-button--disabled" disabled>Mark P1</,
);
assert.match(markup, new RegExp(escapeRegExp(recordingScenario.sessions[0])));
assert.match(markup, new RegExp(escapeRegExp(recordingScenario.tracks[0].name)));
assert.doesNotMatch(markup, /Add Track/);
assert.doesNotMatch(markup, /SF lat1|SF lon1|SF lat2|SF lon2|SF heading/);
assert.doesNotMatch(markup, /href="#"/);
assert.equal(browserGlobalMarkup, markup, "browser-global and CommonJS renderers should match");
assert.match(
  heavyTrackLibraryMarkup,
  new RegExp(escapeRegExp(
    typeof heavyTrackLibraryScenario.sessions[1] === "string"
      ? heavyTrackLibraryScenario.sessions[1]
      : heavyTrackLibraryScenario.sessions[1].filename,
  )),
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
const sessionReviewMarkup = webConsoleModule.renderWebConsoleMarkup(sessionReviewScenario);
assert.match(sessionReviewMarkup, /Date/i);
assert.match(sessionReviewMarkup, /Track/i);
assert.match(sessionReviewMarkup, /Best Lap/i);
assert.match(sessionReviewMarkup, /Ningbo Kart Center/);
assert.match(sessionReviewMarkup, /50\.890s/);
assert.match(sessionReviewMarkup, /Advanced/i);
assert.doesNotMatch(sessionReviewMarkup, /Save Settings/);

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
const deltaSlowScenario = getScenarioById("recording-slow");
const sectorFocusScenario = getScenarioById("sector-focus");
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
const deltaSlowMarkup = deviceScreenModule.renderDeviceScreenMarkup(deltaSlowScenario);
const sectorFocusPolishedMarkup = deviceScreenModule.renderDeviceScreenMarkup(sectorFocusScenario, { variant: "polished" });
const sectorFocusOriginalMarkup = deviceScreenModule.renderDeviceScreenMarkup(sectorFocusScenario, { variant: "original" });
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
assert.match(deltaSlowMarkup, /\+0\.24/);
assert.match(sectorFocusPolishedMarkup, /device-driving--sector-focus/);
assert.doesNotMatch(sectorFocusPolishedMarkup, /Sector Delta/);
assert.doesNotMatch(sectorFocusPolishedMarkup, /Reference:/);
assert.match(sectorFocusPolishedMarkup, />S1<\/span>/);
assert.match(sectorFocusPolishedMarkup, /-0\.05/);
assert.match(sectorFocusPolishedMarkup, /class="device-driving__delta-label">S2/);
assert.match(sectorFocusPolishedMarkup, />S2<\/span>/);
assert.match(sectorFocusPolishedMarkup, /\+0\.12/);
assert.match(sectorFocusPolishedMarkup, />S3<\/span>/);
assert.match(sectorFocusPolishedMarkup, />LIVE<\/span>/);
assert.doesNotMatch(sectorFocusPolishedMarkup, />S4<\/span>/);
assert.doesNotMatch(sectorFocusPolishedMarkup, />BEST<\/span>/);
assert.match(
  sectorFocusPolishedMarkup,
  /device-driving__delta[\s\S]*device-sector-ribbon[\s\S]*device-driving__bottom/,
);
assert.doesNotMatch(sectorFocusOriginalMarkup, /device-sector-ribbon/);
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
assert.match(idleStatusMarkup, /class="device-status__line"><span class="device-status__text">Recording: Idle/);
assert.doesNotMatch(idleStatusMarkup, /class="device-status__line device-status__line--recording"><span class="device-status__text">Recording: Idle/);
assert.match(recordingStatusMarkup, /class="device-status__line device-status__line--recording"><span class="device-status__text">Recording: REC/);
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
