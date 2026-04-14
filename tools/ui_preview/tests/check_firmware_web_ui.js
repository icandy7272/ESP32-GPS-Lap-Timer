const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const repoRoot = path.resolve(__dirname, "..", "..", "..");
const webUiEntryPath = path.join(repoRoot, "src/wifi/web_ui.cpp");
const webUiMarkupPath = path.join(repoRoot, "src/wifi/web_ui_markup.cpp");
const webUiScriptPath = path.join(repoRoot, "src/wifi/web_ui_script.cpp");
const webUiScriptCorePath = path.join(repoRoot, "src/wifi/web_ui_script_core.cpp");
const webUiScriptDashboardPath = path.join(repoRoot, "src/wifi/web_ui_script_dashboard.cpp");
const webUiScriptTrackCreationReviewPath = path.join(repoRoot, "src/wifi/web_ui_script_track_creation_review.cpp");
const webUiScriptTrackCreationPath = path.join(repoRoot, "src/wifi/web_ui_script_track_creation.cpp");

assert.ok(
  fs.existsSync(webUiEntryPath),
  "Expected src/wifi/web_ui.cpp to exist",
);
assert.ok(
  fs.existsSync(webUiMarkupPath),
  "Expected src/wifi/web_ui_markup.cpp to hold the HTML/CSS markup builders",
);
assert.ok(
  fs.existsSync(webUiScriptPath),
  "Expected src/wifi/web_ui_script.cpp to assemble the embedded script section",
);
assert.ok(
  fs.existsSync(webUiScriptCorePath),
  "Expected src/wifi/web_ui_script_core.cpp to hold the shared dashboard script",
);
assert.ok(
  fs.existsSync(webUiScriptDashboardPath),
  "Expected src/wifi/web_ui_script_dashboard.cpp to hold the status/tracks/sessions/settings runtime script",
);
assert.ok(
  fs.existsSync(webUiScriptTrackCreationReviewPath),
  "Expected src/wifi/web_ui_script_track_creation_review.cpp to hold the geometry review script",
);
assert.ok(
  fs.existsSync(webUiScriptTrackCreationPath),
  "Expected src/wifi/web_ui_script_track_creation.cpp to hold the track-creation script",
);

const webUiEntrySource = fs.readFileSync(
  webUiEntryPath,
  "utf8",
);
const webUiMarkupSource = fs.readFileSync(webUiMarkupPath, "utf8");
const webUiScriptSource = fs.readFileSync(webUiScriptPath, "utf8");
const webUiScriptCoreSource = fs.readFileSync(webUiScriptCorePath, "utf8");
const webUiScriptDashboardSource = fs.readFileSync(webUiScriptDashboardPath, "utf8");
const webUiScriptTrackCreationReviewSource = fs.readFileSync(webUiScriptTrackCreationReviewPath, "utf8");
const webUiScriptTrackCreationSource = fs.readFileSync(webUiScriptTrackCreationPath, "utf8");

assert.match(
  webUiEntrySource,
  /build_web_ui_head_section\(\)/,
  "src/wifi/web_ui.cpp should delegate head markup assembly",
);
assert.match(
  webUiEntrySource,
  /build_web_ui_body_section\(\)/,
  "src/wifi/web_ui.cpp should delegate body markup assembly",
);
assert.match(
  webUiEntrySource,
  /build_web_ui_script_section\(\)/,
  "src/wifi/web_ui.cpp should delegate script assembly",
);
assert.doesNotMatch(
  webUiEntrySource,
  /function \$\(id\)/,
  "src/wifi/web_ui.cpp should no longer embed the full dashboard script directly",
);
assert.doesNotMatch(
  webUiEntrySource,
  /#include <Arduino\.h>/,
  "src/wifi/web_ui.cpp should avoid the redundant Arduino include",
);
assert.match(
  webUiMarkupSource,
  /build_web_ui_body_section/,
  "src/wifi/web_ui_markup.cpp should define the body builder",
);
assert.match(
  webUiScriptSource,
  /build_web_ui_script_section/,
  "src/wifi/web_ui_script.cpp should define the script-section builder",
);
assert.match(
  webUiScriptCoreSource,
  /build_web_ui_script_core_fragment/,
  "src/wifi/web_ui_script_core.cpp should define the shared script fragment",
);
assert.match(
  webUiScriptDashboardSource,
  /build_web_ui_script_dashboard_fragment/,
  "src/wifi/web_ui_script_dashboard.cpp should define the dashboard runtime fragment",
);
assert.match(
  webUiScriptTrackCreationReviewSource,
  /build_web_ui_script_track_creation_review_fragment/,
  "src/wifi/web_ui_script_track_creation_review.cpp should define the track review fragment",
);
assert.match(
  webUiScriptTrackCreationSource,
  /build_web_ui_script_track_creation_fragment/,
  "src/wifi/web_ui_script_track_creation.cpp should define the track-creation fragment",
);
assert.doesNotMatch(
  webUiScriptCoreSource,
  /function renderCurrentTrackSummary|function loadSessions|function renderGeometryReview/,
  "src/wifi/web_ui_script_core.cpp should stay focused on shared state and helpers",
);
assert.doesNotMatch(
  webUiScriptCoreSource,
  /function updateLineHeading|function updateStartFinishHeading|function setCreationStepState/,
  "src/wifi/web_ui_script_core.cpp should not keep track-creation-only helpers",
);
assert.match(
  webUiScriptTrackCreationSource,
  /function updateLineHeading|function updateStartFinishHeading|function setCreationStepState/,
  "src/wifi/web_ui_script_track_creation.cpp should own the track-creation-only helpers",
);
assert.doesNotMatch(
  webUiScriptTrackCreationSource,
  /function renderGeometryReview|function loadSessions|function loadTracks|function loadSettings/,
  "src/wifi/web_ui_script_track_creation.cpp should stay focused on the track-creation flow",
);

function extractJsFragment(source, functionName, fileLabel) {
  const escapedFunctionName = functionName.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const fragmentMatch = source.match(
    new RegExp(
      escapedFunctionName + String.raw`\s*\([^)]*\)\s*\{\s*return R"JS\(([\s\S]*?)\)JS";`,
    ),
  );
  assert.ok(fragmentMatch, "Failed to extract " + functionName + " from " + fileLabel);
  return fragmentMatch[1];
}

const embeddedScript =
  extractJsFragment(
    webUiScriptCoreSource,
    "build_web_ui_script_core_fragment",
    "src/wifi/web_ui_script_core.cpp",
  ) +
  extractJsFragment(
    webUiScriptDashboardSource,
    "build_web_ui_script_dashboard_fragment",
    "src/wifi/web_ui_script_dashboard.cpp",
  ) +
  extractJsFragment(
    webUiScriptTrackCreationReviewSource,
    "build_web_ui_script_track_creation_review_fragment",
    "src/wifi/web_ui_script_track_creation_review.cpp",
  ) +
  extractJsFragment(
    webUiScriptTrackCreationSource,
    "build_web_ui_script_track_creation_fragment",
    "src/wifi/web_ui_script_track_creation.cpp",
  );

function createElement(id = "") {
  return {
    id,
    textContent: "",
    className: "",
    innerHTML: "",
    disabled: false,
    value: "",
    style: {},
    children: [],
    onclick: null,
    _listeners: {},
    addEventListener(type, handler) {
      this._listeners[type] = handler;
    },
    appendChild(child) {
      this.children.push(child);
    },
    getAttribute(name) {
      return this[name] || null;
    },
    setAttribute(name, value) {
      this[name] = String(value);
    },
  };
}

function createHarness() {
  const elements = new Map();
  const calls = [];
  const queuedResponses = [];
  const intervals = new Map();
  const timeouts = new Map();
  let nextTimerId = 1;
  let nowMs = 0;
  let intervalsEnabled = true;

  function getElement(id) {
    if (!elements.has(id)) {
      elements.set(id, createElement(id));
    }
    return elements.get(id);
  }

  function matches(matcher, url, options) {
    if (typeof matcher === "function") {
      return matcher(url, options);
    }
    return matcher === url;
  }

  function makeResponse(payload) {
    const ok = Object.prototype.hasOwnProperty.call(payload, "ok") ? payload.ok : true;
    const body = Object.prototype.hasOwnProperty.call(payload, "body") ? payload.body : payload;
    return {
      ok,
      json() {
        return Promise.resolve(body);
      },
    };
  }

  function defaultPayload(url, options) {
    const method = ((options && options.method) || "GET").toUpperCase();
    if (url === "/api/status") {
      return {
        gps_fix: false,
        satellites: 0,
        lat: 0,
        lon: 0,
        recording: false,
        current_lap: 0,
        best_lap_ms: -1,
        track: "",
      };
    }
    if (url === "/api/sessions") {
      return { sessions: [] };
    }
    if (url === "/api/tracks" && method === "GET") {
      return { tracks: [] };
    }
    if (url === "/api/tracks" && method === "POST") {
      return { ok: true, body: { ok: true } };
    }
    if (url === "/api/settings") {
      return { wifi_ssid: "", wifi_pass: "", brightness: 180 };
    }
    if (url === "/api/recording") {
      return { ok: true, recording: false };
    }
    if (url === "/api/tracks/select" || url === "/api/tracks/delete") {
      return { ok: true };
    }
    return { ok: true };
  }

  function enqueueResponse(matcher, payload) {
    queuedResponses.push({ matcher, payload, deferred: false });
  }

  function enqueueDeferredResponse(matcher) {
    let resolveResponse;
    const pending = new Promise((resolve) => {
      resolveResponse = resolve;
    });
    queuedResponses.push({ matcher, deferred: true, pending });
    return {
      resolve(payload) {
        resolveResponse(makeResponse(payload));
      },
    };
  }

  function fetch(url, options = {}) {
    calls.push({ url, options });
    const index = queuedResponses.findIndex((entry) => matches(entry.matcher, url, options));
    if (index >= 0) {
      const entry = queuedResponses.splice(index, 1)[0];
      if (entry.deferred) {
        return entry.pending;
      }
      return Promise.resolve(makeResponse(entry.payload));
    }
    return Promise.resolve(makeResponse(defaultPayload(url, options)));
  }

  function setIntervalFake(handler, delay) {
    const id = nextTimerId++;
    if (!intervalsEnabled) {
      return id;
    }
    const normalizedDelay = Math.max(0, Number(delay) || 0);
    intervals.set(id, {
      handler,
      delay: normalizedDelay,
      nextAt: nowMs + normalizedDelay,
    });
    return id;
  }

  function clearIntervalFake(id) {
    intervals.delete(id);
  }

  function setTimeoutFake(handler, delay) {
    const id = nextTimerId++;
    const normalizedDelay = Math.max(0, Number(delay) || 0);
    timeouts.set(id, {
      handler,
      nextAt: nowMs + normalizedDelay,
    });
    return id;
  }

  function clearTimeoutFake(id) {
    timeouts.delete(id);
  }

  function findNextTimerBefore(targetMs) {
    let best = null;

    for (const [id, timer] of intervals.entries()) {
      if (timer.nextAt > targetMs) {
        continue;
      }
      if (!best || timer.nextAt < best.when) {
        best = { type: "interval", id, when: timer.nextAt };
      }
    }

    for (const [id, timer] of timeouts.entries()) {
      if (timer.nextAt > targetMs) {
        continue;
      }
      if (!best || timer.nextAt < best.when) {
        best = { type: "timeout", id, when: timer.nextAt };
      }
    }

    return best;
  }

  const context = {
    console,
    fetch,
    confirm() {
      return true;
    },
    setInterval: setIntervalFake,
    clearInterval: clearIntervalFake,
    setTimeout: setTimeoutFake,
    clearTimeout: clearTimeoutFake,
    Date: class FakeDate extends Date {
      constructor(...args) {
        if (args.length > 0) {
          super(...args);
        } else {
          super(nowMs);
        }
      }

      static now() {
        return nowMs;
      }
    },
    document: {
      getElementById(id) {
        return getElement(id);
      },
      createElement(tag) {
        return createElement(tag);
      },
    },
  };

  vm.runInNewContext(embeddedScript, context, {
    filename: "embedded-web-ui.js",
  });

  async function settle() {
    for (let i = 0; i < 8; i++) {
      await Promise.resolve();
    }
  }

  async function tick(ms) {
    const targetMs = nowMs + Math.max(0, Number(ms) || 0);

    while (true) {
      const next = findNextTimerBefore(targetMs);
      if (!next) {
        break;
      }

      nowMs = next.when;
      if (next.type === "timeout") {
        const timer = timeouts.get(next.id);
        timeouts.delete(next.id);
        if (timer) {
          timer.handler();
        }
      } else {
        const timer = intervals.get(next.id);
        if (timer) {
          timer.nextAt += timer.delay;
          timer.handler();
        }
      }
      await settle();
    }

    nowMs = targetMs;
    await settle();
  }

  return {
    context,
    calls,
    getElement,
    enqueueResponse,
    enqueueDeferredResponse,
    settle,
    tick,
    pauseIntervals() {
      intervalsEnabled = false;
      intervals.clear();
    },
    countStatusCalls() {
      return calls.filter((call) => call.url === "/api/status").length;
    },
    countTrackPostCalls() {
      return calls.filter((call) => {
        return call.url === "/api/tracks" &&
          (((call.options || {}).method || "GET").toUpperCase() === "POST");
      }).length;
    },
    countTrackSelectPostCalls() {
      return calls.filter((call) => {
        return call.url === "/api/tracks/select" &&
          (((call.options || {}).method || "GET").toUpperCase() === "POST");
      }).length;
    },
  };
}

function applyStatusSample(harness, sample = {}) {
  harness.context.applyStatusData({
    gps_fix: false,
    satellites: 0,
    lat: 0,
    lon: 0,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "",
    ...sample,
  });
  harness.context.renderTrackDraft();
}

function seedStableGps(harness, samples) {
  samples.forEach((sample) => applyStatusSample(harness, sample));
}

function sampledResponse(lat, lon, extra = {}) {
  return {
    gps_fix: true,
    satellites: 8,
    lat,
    lon,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
    ...extra,
  };
}

function enqueueStatusSamples(harness, responses) {
  responses.forEach((response) => {
    harness.enqueueResponse("/api/status", response);
  });
}

async function testGpsFixAllowsZeroZeroCoordinates() {
  const harness = createHarness();
  await harness.settle();
  harness.context._trackDraft.gps.fix = true;
  harness.context._trackDraft.gps.lat = 0;
  harness.context._trackDraft.gps.lon = 0;
  assert.equal(
    harness.context.hasValidFix(),
    true,
    "gps_fix=true should be enough even at exact 0,0 coordinates",
  );
}

async function testMarkingUsesFreshStatusAndRejectsZeroLengthLine() {
  const harness = createHarness();
  await harness.settle();

  seedStableGps(harness, [
    sampledResponse(31.2304160, 121.4737004),
    sampledResponse(31.2304163, 121.4737007),
    sampledResponse(31.2304166, 121.4737010),
  ]);

  const statusCallsBefore = harness.calls.filter((call) => call.url === "/api/status").length;
  enqueueStatusSamples(harness, [
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304167, 121.4737010),
  ]);
  harness.context.markStartFinishPoint("p1");
  await harness.settle();
  await harness.tick(2400);

  const statusCallsAfter = harness.calls.filter((call) => call.url === "/api/status").length;
  assert.equal(
    statusCallsAfter,
    statusCallsBefore + 6,
    "marking should use a timed sample window of fresh /api/status snapshots",
  );
  assert.equal(harness.context._trackDraft.startFinish.p1.lat, 31.2304167);
  assert.equal(harness.context._trackDraft.startFinish.p1.lon, 121.4737010);

  enqueueStatusSamples(harness, [
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304167, 121.4737010),
  ]);
  harness.context.markStartFinishPoint("p2");
  await harness.settle();
  await harness.tick(2400);

  assert.equal(
    harness.context._trackDraft.startFinish.p2,
    null,
    "identical P1/P2 samples should be rejected to avoid a zero-length detection line",
  );
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /at least 2 m|too close/i,
  );
}

async function testGpsStabilityControlsMarkingState() {
  const harness = createHarness();
  await harness.settle();

  function applyStatus(sample) {
    harness.context.applyStatusData({
      gps_fix: false,
      satellites: 0,
      lat: 0,
      lon: 0,
      recording: false,
      current_lap: 0,
      best_lap_ms: -1,
      track: "",
      ...sample,
    });
    harness.context.renderTrackDraft();
  }

  applyStatus({
    gps_fix: true,
    satellites: 3,
    lat: 31.2304167,
    lon: 121.4737010,
  });
  assert.match(
    harness.getElement("gps-bar").innerHTML,
    /Need better GPS/i,
  );
  assert.equal(
    harness.getElement("sf-p1-btn").disabled,
    true,
    "marking should stay disabled below the minimum satellite threshold",
  );

  applyStatus({
    gps_fix: true,
    satellites: 4,
    lat: 31.2304167,
    lon: 121.4737010,
  });
  assert.match(
    harness.getElement("gps-bar").innerHTML,
    /Hold still\.\.\. stabilizing/i,
  );
  assert.equal(
    harness.getElement("sf-p1-btn").disabled,
    true,
    "marking should wait for several stable samples before enabling buttons",
  );

  applyStatus({
    gps_fix: true,
    satellites: 4,
    lat: 31.2304174,
    lon: 121.4737017,
  });
  applyStatus({
    gps_fix: true,
    satellites: 4,
    lat: 31.2304181,
    lon: 121.4737014,
  });
  assert.match(
    harness.getElement("gps-bar").innerHTML,
    /Good - ready to mark/i,
  );
  assert.equal(
    harness.getElement("sf-p1-btn").disabled,
    false,
    "marking should enable once the recent GPS samples converge",
  );

  applyStatus({
    gps_fix: true,
    satellites: 4,
    lat: 31.2304700,
    lon: 121.4737600,
  });
  assert.match(
    harness.getElement("gps-bar").innerHTML,
    /Hold still\.\.\. stabilizing/i,
  );
  assert.equal(
    harness.getElement("sf-p1-btn").disabled,
    true,
    "marking should disable again if the latest sample jumps too far",
  );
}

async function testGpsBarShowsFreshnessBuckets() {
  const harness = createHarness();
  await harness.settle();
  harness.pauseIntervals();

  function applyStatus(sample) {
    harness.context.applyStatusData({
      gps_fix: false,
      satellites: 0,
      lat: 0,
      lon: 0,
      recording: false,
      current_lap: 0,
      best_lap_ms: -1,
      track: "",
      ...sample,
    });
    harness.context.renderTrackDraft();
  }

  applyStatus({
    gps_fix: true,
    satellites: 4,
    lat: 31.2304167,
    lon: 121.4737010,
  });
  applyStatus({
    gps_fix: true,
    satellites: 4,
    lat: 31.2304174,
    lon: 121.4737017,
  });
  applyStatus({
    gps_fix: true,
    satellites: 4,
    lat: 31.2304181,
    lon: 121.4737014,
  });

  assert.match(
    harness.getElement("gps-bar").innerHTML,
    /Live/i,
    "fresh GPS data should be labeled as live",
  );

  await harness.tick(1500);
  harness.context.renderTrackDraft();
  assert.match(
    harness.getElement("gps-bar").innerHTML,
    /1s ago/i,
    "freshness should move to a coarse age bucket after about a second",
  );

  await harness.tick(2500);
  harness.context.renderTrackDraft();
  assert.match(
    harness.getElement("gps-bar").innerHTML,
    /Stale/i,
    "freshness should warn when the visible coordinate is several seconds old",
  );
}

async function testTrackCreationUsesAdaptivePollingWithoutOverlap() {
  const harness = createHarness();
  await harness.settle();

  const initialStatusCalls = harness.countStatusCalls();
  await harness.tick(1999);
  assert.equal(
    harness.countStatusCalls(),
    initialStatusCalls,
    "idle polling should not fire before the default two-second cadence",
  );

  await harness.tick(1);
  assert.equal(
    harness.countStatusCalls(),
    initialStatusCalls + 1,
    "idle polling should refresh after two seconds",
  );

  harness.getElement("track-name").value = "Adaptive Polling Test";
  harness.context.renderTrackDraft();

  const creationCallsBefore = harness.countStatusCalls();
  await harness.tick(999);
  assert.equal(
    harness.countStatusCalls(),
    creationCallsBefore,
    "track-creation mode should wait until the faster polling interval elapses",
  );

  await harness.tick(1);
  assert.equal(
    harness.countStatusCalls(),
    creationCallsBefore + 1,
    "track-creation mode should poll faster than the default status screen",
  );

  const pendingStatus = harness.enqueueDeferredResponse("/api/status");
  const overlapCallsBefore = harness.countStatusCalls();
  await harness.tick(1000);
  assert.equal(
    harness.countStatusCalls(),
    overlapCallsBefore + 1,
    "the next poll should begin once the faster interval elapses",
  );

  await harness.tick(3000);
  assert.equal(
    harness.countStatusCalls(),
    overlapCallsBefore + 1,
    "slow responses should not cause overlapping queued status polls",
  );

  pendingStatus.resolve({
    gps_fix: true,
    satellites: 4,
    lat: 31.2304167,
    lon: 121.4737010,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "",
  });
  await harness.settle();

  await harness.tick(1000);
  assert.equal(
    harness.countStatusCalls(),
    overlapCallsBefore + 2,
    "polling should resume after the in-flight request finishes",
  );
}

async function testMarkingRequiresGpsStability() {
  const harness = createHarness();
  await harness.settle();

  applyStatusSample(harness, {
    gps_fix: true,
    satellites: 4,
    lat: 31.2304165,
    lon: 121.4737008,
  });

  harness.enqueueResponse("/api/status", {
    gps_fix: true,
    satellites: 4,
    lat: 31.2304167,
    lon: 121.4737010,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "",
  });
  harness.context.markStartFinishPoint("p1");
  await harness.settle();

  assert.equal(
    harness.context._trackDraft.startFinish.p1,
    null,
    "marking should reject fresh fixes until the GPS samples are stable",
  );
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /stabil/i,
  );
}

async function testSampledCaptureUsesTimedMedianAndSamplingCadence() {
  const harness = createHarness();
  await harness.settle();

  seedStableGps(harness, [
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304169, 121.4737012),
    sampledResponse(31.2304171, 121.4737014),
  ]);

  enqueueStatusSamples(harness, [
    sampledResponse(31.2304188, 121.4737020),
    sampledResponse(31.2304190, 121.4737022),
    sampledResponse(31.2304300, 121.4737200),
    sampledResponse(31.2304192, 121.4737024),
    sampledResponse(31.2304194, 121.4737026),
    sampledResponse(31.2304196, 121.4737028),
  ]);

  const statusCallsBefore = harness.countStatusCalls();
  harness.context.markStartFinishPoint("p1");
  await harness.settle();

  assert.equal(
    harness.context._trackDraft.startFinish.p1,
    null,
    "sampling should not store P1 immediately on tap",
  );
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /sampling point/i,
    "sampling should surface in-progress copy while collecting fresh points",
  );

  await harness.tick(399);
  assert.equal(
    harness.countStatusCalls(),
    statusCallsBefore + 1,
    "sampling should not fetch again before the 400ms cadence elapses",
  );

  await harness.tick(1);
  assert.equal(
    harness.countStatusCalls(),
    statusCallsBefore + 2,
    "sampling should temporarily increase capture cadence to 400ms",
  );

  await harness.tick(2000);

  assert.ok(
    harness.context._trackDraft.startFinish.p1,
    "sampling should eventually store a captured point after the full window",
  );
  assert.equal(
    harness.countStatusCalls(),
    statusCallsBefore + 6,
    "sampling should collect a fixed six fresh snapshots across the 2400ms window",
  );
  assert.ok(
    Math.abs(harness.context._trackDraft.startFinish.p1.lat - 31.2304193) < 1e-7,
    "captured latitude should reflect the sample median rather than the first sample",
  );
  assert.ok(
    Math.abs(harness.context._trackDraft.startFinish.p1.lon - 121.4737025) < 1e-7,
    "captured longitude should reflect the sample median rather than the first sample",
  );
  assert.equal(harness.context._trackDraft.startFinish.p1.sampleCount, 6);
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /sampled 6 fixes/i,
    "successful capture should summarize the number of samples used",
  );
}

async function testSampledCaptureCanCancelAndRejectConcurrentMarks() {
  const harness = createHarness();
  await harness.settle();

  seedStableGps(harness, [
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304169, 121.4737012),
    sampledResponse(31.2304171, 121.4737014),
  ]);

  enqueueStatusSamples(harness, [
    sampledResponse(31.2304188, 121.4737020),
    sampledResponse(31.2304190, 121.4737022),
    sampledResponse(31.2304192, 121.4737024),
    sampledResponse(31.2304194, 121.4737026),
    sampledResponse(31.2304196, 121.4737028),
    sampledResponse(31.2304198, 121.4737030),
  ]);

  harness.context.markStartFinishPoint("p1");
  await harness.settle();

  harness.context.markStartFinishPoint("p2");
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /finish the current sample or cancel/i,
    "second mark taps should be rejected while a sample window is already active",
  );

  harness.context.cancelPointSampling();
  await harness.settle();
  await harness.tick(2400);

  assert.equal(
    harness.context._trackDraft.startFinish.p1,
    null,
    "cancel should discard the in-progress sample window",
  );
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /sampling canceled/i,
    "cancel should explain that no point was stored",
  );
}

async function testSampledCaptureFailsOnGpsLossOrTooFewSamples() {
  const harness = createHarness();
  await harness.settle();

  seedStableGps(harness, [
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304169, 121.4737012),
    sampledResponse(31.2304171, 121.4737014),
  ]);

  enqueueStatusSamples(harness, [
    sampledResponse(31.2304188, 121.4737020),
    sampledResponse(31.2304190, 121.4737022),
    sampledResponse(0, 0, { gps_fix: false, satellites: 0 }),
  ]);

  harness.context.markStartFinishPoint("p1");
  await harness.settle();
  await harness.tick(2400);

  assert.equal(
    harness.context._trackDraft.startFinish.p1,
    null,
    "GPS loss during the sample window should fail the capture",
  );
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /gps dropped during sampling|retry/i,
    "failed sampling should tell the user to retry instead of storing partial data",
  );

  seedStableGps(harness, [
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304169, 121.4737012),
    sampledResponse(31.2304171, 121.4737014),
  ]);

  enqueueStatusSamples(harness, [
    sampledResponse(31.2304188, 121.4737020),
    sampledResponse(31.2304190, 121.4737022),
  ]);
  harness.enqueueDeferredResponse("/api/status");

  harness.context.markStartFinishPoint("p1");
  await harness.settle();
  await harness.tick(2400);

  assert.equal(
    harness.context._trackDraft.startFinish.p1,
    null,
    "fewer than three valid samples should not create a point",
  );
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /at least 3 good fixes|retry/i,
    "sample windows with too few valid fixes should produce an actionable retry message",
  );
}

async function testSectorMarkingUsesSharedSampledCapturePath() {
  const harness = createHarness();
  await harness.settle();

  harness.context.addSectorRow();
  seedStableGps(harness, [
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304169, 121.4737012),
    sampledResponse(31.2304171, 121.4737014),
  ]);

  enqueueStatusSamples(harness, [
    sampledResponse(31.2304200, 121.4737040),
    sampledResponse(31.2304202, 121.4737042),
    sampledResponse(31.2304204, 121.4737044),
    sampledResponse(31.2304206, 121.4737046),
    sampledResponse(31.2304208, 121.4737048),
    sampledResponse(31.2304210, 121.4737050),
  ]);

  harness.context.markSectorPoint(1, "p1");
  await harness.settle();
  await harness.tick(2400);

  assert.ok(
    harness.context._trackDraft.sectors[0].p1,
    "sector P1 should also use the sampled capture path",
  );
  assert.equal(
    harness.context._trackDraft.sectors[0].p1.sampleCount,
    6,
    "sector capture should store the same metadata as start/finish capture",
  );
}

async function testRepeatabilityCheckCanUpgradeShortLineConfidence() {
  const harness = createHarness();
  await harness.settle();

  harness.context.REPEATABILITY_CHECK_ENABLED = true;
  harness.getElement("track-name").value = "Repeatability Test";
  harness.context._trackDraft.startFinish = {
    p1: { lat: 31.2300000, lon: 121.4700000, sampleCount: 3, spreadM: 1.3 },
    p2: { lat: 31.2300000, lon: 121.4700420, sampleCount: 3, spreadM: 1.2 },
    heading: 180,
    flipped: false,
  };
  seedStableGps(harness, [
    sampledResponse(31.2300000, 121.4700000),
    sampledResponse(31.2300001, 121.4700001),
    sampledResponse(31.2300000, 121.4700000),
  ]);
  harness.context.renderTrackDraft();

  assert.equal(
    harness.getElement("create-track-btn").disabled,
    true,
    "short lines with only medium-confidence points should stay blocked before repeatability confirmation",
  );
  assert.equal(
    harness.getElement("repeatability-btn").style.display,
    "block",
    "repeatability affordance should appear when the feature flag is enabled",
  );

  harness.context.startRepeatabilityCheck();
  assert.equal(harness.context._trackDraft.repeatability.active, true);

  enqueueStatusSamples(harness, [
    sampledResponse(31.2300000, 121.4700000),
    sampledResponse(31.2300001, 121.4700001),
    sampledResponse(31.2300000, 121.4700000),
    sampledResponse(31.2300001, 121.4700001),
    sampledResponse(31.2300000, 121.4700000),
    sampledResponse(31.2300001, 121.4700001),
  ]);
  harness.context.markStartFinishPoint("p1");
  await harness.settle();
  await harness.tick(2400);

  enqueueStatusSamples(harness, [
    sampledResponse(31.2300000, 121.4700420),
    sampledResponse(31.2300001, 121.4700421),
    sampledResponse(31.2300000, 121.4700420),
    sampledResponse(31.2300001, 121.4700421),
    sampledResponse(31.2300000, 121.4700420),
    sampledResponse(31.2300001, 121.4700421),
  ]);
  harness.context.markStartFinishPoint("p2");
  await harness.settle();
  await harness.tick(2400);

  assert.equal(harness.context._trackDraft.repeatability.status, "passed");
  assert.equal(
    harness.getElement("create-track-btn").disabled,
    false,
    "close repeatability agreement should allow the short line to be created",
  );
  assert.match(
    harness.getElement("repeatability-summary").textContent,
    /passed|confirmed/i,
    "repeatability UI should summarize a successful confirmation",
  );
}

async function testRepeatabilityCheckBlocksShortLineOnMismatch() {
  const harness = createHarness();
  await harness.settle();

  harness.context.REPEATABILITY_CHECK_ENABLED = true;
  harness.getElement("track-name").value = "Repeatability Mismatch";
  harness.context._trackDraft.startFinish = {
    p1: { lat: 31.2300000, lon: 121.4700000, sampleCount: 3, spreadM: 1.3 },
    p2: { lat: 31.2300000, lon: 121.4700420, sampleCount: 3, spreadM: 1.2 },
    heading: 180,
    flipped: false,
  };
  seedStableGps(harness, [
    sampledResponse(31.2300000, 121.4700000),
    sampledResponse(31.2300001, 121.4700001),
    sampledResponse(31.2300000, 121.4700000),
  ]);
  harness.context.renderTrackDraft();
  harness.context.startRepeatabilityCheck();

  enqueueStatusSamples(harness, [
    sampledResponse(31.2300200, 121.4700100),
    sampledResponse(31.2300201, 121.4700101),
    sampledResponse(31.2300200, 121.4700100),
    sampledResponse(31.2300201, 121.4700101),
    sampledResponse(31.2300200, 121.4700100),
    sampledResponse(31.2300201, 121.4700101),
  ]);
  harness.context.markStartFinishPoint("p1");
  await harness.settle();
  await harness.tick(2400);

  enqueueStatusSamples(harness, [
    sampledResponse(31.2300450, 121.4700100),
    sampledResponse(31.2300451, 121.4700101),
    sampledResponse(31.2300450, 121.4700100),
    sampledResponse(31.2300451, 121.4700101),
    sampledResponse(31.2300450, 121.4700100),
    sampledResponse(31.2300451, 121.4700101),
  ]);
  harness.context.markStartFinishPoint("p2");
  await harness.settle();
  await harness.tick(2400);

  assert.equal(harness.context._trackDraft.repeatability.status, "failed");
  assert.equal(
    harness.getElement("create-track-btn").disabled,
    true,
    "large repeatability disagreement should keep short lines blocked",
  );
  assert.match(
    harness.getElement("repeatability-summary").textContent,
    /mismatch|remeasure/i,
    "repeatability UI should explain that the line needs to be remeasured",
  );
}

async function testRepeatabilityStateResetsAfterLineChangesAndInheritsFlip() {
  const harness = createHarness();
  await harness.settle();

  harness.context.REPEATABILITY_CHECK_ENABLED = true;
  harness.getElement("track-name").value = "Repeatability Reset";
  harness.context._trackDraft.startFinish = {
    p1: { lat: 31.2300000, lon: 121.4700000, sampleCount: 3, spreadM: 1.3 },
    p2: { lat: 31.2300000, lon: 121.4700420, sampleCount: 3, spreadM: 1.2 },
    heading: 180,
    flipped: true,
  };
  harness.context.renderTrackDraft();

  harness.context.startRepeatabilityCheck();
  assert.equal(
    harness.context._trackDraft.repeatability.candidate.flipped,
    true,
    "repeatability candidate should inherit the currently selected crossing direction",
  );

  harness.context._trackDraft.repeatability.active = false;
  harness.context._trackDraft.repeatability.status = "passed";
  harness.context._trackDraft.repeatability.result = {
    passed: true,
    midpointDeltaM: 0.4,
    headingDeltaDeg: 3.0,
  };
  harness.context.renderTrackDraft();
  assert.equal(
    harness.getElement("create-track-btn").disabled,
    false,
    "a passed repeatability check should unlock creation for a short line",
  );

  harness.context.flipStartFinishHeading();
  assert.equal(
    harness.context._trackDraft.repeatability.status,
    "idle",
    "flipping the accepted direction should invalidate old repeatability results",
  );
  assert.equal(
    harness.getElement("create-track-btn").disabled,
    true,
    "creation should be re-gated after the line direction changes",
  );

  seedStableGps(harness, [
    sampledResponse(31.2300000, 121.4700000),
    sampledResponse(31.2300001, 121.4700001),
    sampledResponse(31.2300000, 121.4700000),
  ]);

  harness.context._trackDraft.repeatability.status = "passed";
  harness.context._trackDraft.repeatability.result = {
    passed: true,
    midpointDeltaM: 0.3,
    headingDeltaDeg: 2.0,
  };
  enqueueStatusSamples(harness, [
    sampledResponse(31.2300000, 121.4700010),
    sampledResponse(31.2300001, 121.4700011),
    sampledResponse(31.2300000, 121.4700010),
    sampledResponse(31.2300001, 121.4700011),
    sampledResponse(31.2300000, 121.4700010),
    sampledResponse(31.2300001, 121.4700011),
  ]);
  harness.context.markStartFinishPoint("p1");
  await harness.settle();
  await harness.tick(2400);

  assert.equal(
    harness.context._trackDraft.repeatability.status,
    "idle",
    "re-marking the main line should invalidate old repeatability results",
  );
}

async function testSamplingIgnoresPreTapStatusRequestsUntilFreshFetchArrives() {
  const harness = createHarness();
  await harness.settle();

  seedStableGps(harness, [
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304169, 121.4737012),
    sampledResponse(31.2304171, 121.4737014),
  ]);

  const staleResponse = harness.enqueueDeferredResponse("/api/status");
  harness.context.refreshStatus();
  await harness.settle();

  harness.context.markStartFinishPoint("p1");
  await harness.settle();

  enqueueStatusSamples(harness, [
    sampledResponse(31.2304188, 121.4737020),
    sampledResponse(31.2304190, 121.4737022),
    sampledResponse(31.2304192, 121.4737024),
    sampledResponse(31.2304194, 121.4737026),
    sampledResponse(31.2304196, 121.4737028),
    sampledResponse(31.2304198, 121.4737030),
  ]);
  staleResponse.resolve(sampledResponse(31.2305000, 121.4737800));
  await harness.settle();

  assert.equal(
    harness.context._trackDraft.startFinish.p1,
    null,
    "a status request that was already in flight before the tap should not count as the first capture sample",
  );
  assert.equal(
    harness.context._pointSampling.samples.length,
    0,
    "sampling should wait for a fresh post-tap request before collecting points",
  );

  await harness.tick(2400);

  assert.ok(
    harness.context._trackDraft.startFinish.p1,
    "sampling should still complete once a genuinely fresh status series arrives",
  );
}

async function testIncompleteSectorBlocksTrackCreation() {
  const harness = createHarness();
  await harness.settle();

  harness.getElement("track-name").value = "Shanghai Test Track";
  harness.context._trackDraft.startFinish = {
    p1: { lat: 31.2201, lon: 121.4101 },
    p2: { lat: 31.2201, lon: 121.4102 },
    heading: 90,
    flipped: false,
  };
  harness.context._trackDraft.sectors = [
    {
      id: 1,
      p1: { lat: 31.2202, lon: 121.4102 },
      p2: null,
      heading: null,
      flipped: false,
    },
  ];
  harness.context.renderTrackDraft();

  harness.context.addTrack();
  await harness.settle();

  assert.equal(
    harness.countTrackPostCalls(),
    0,
    "track creation should not submit while any sector row is incomplete",
  );
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /complete or delete/i,
  );
}

async function testCreateTrackGuardsAgainstDoubleSubmit() {
  const harness = createHarness();
  await harness.settle();

  harness.getElement("track-name").value = "Double Tap Raceway";
  harness.context._trackDraft.startFinish = {
    p1: { lat: 31.2300, lon: 121.4700 },
    p2: { lat: 31.2300, lon: 121.4702 },
    heading: 90,
    flipped: false,
  };
  harness.context.renderTrackDraft();

  const pendingCreate = harness.enqueueDeferredResponse((url, options) => {
    return url === "/api/tracks" &&
      (((options || {}).method || "GET").toUpperCase() === "POST");
  });

  harness.context.addTrack();
  harness.context.addTrack();

  assert.equal(
    harness.countTrackPostCalls(),
    1,
    "second tap should be ignored while the create request is in flight",
  );
  assert.equal(
    harness.getElement("create-track-btn").disabled,
    true,
    "create button should stay disabled while submit is pending",
  );

  harness.enqueueResponse("/api/tracks/select", {
    ok: true,
    body: { ok: true, name: "Double Tap Raceway" },
  });
  pendingCreate.resolve({ ok: true, body: { ok: true, id: "track_122" } });
  await harness.settle();
  await harness.settle();

  assert.equal(harness.getElement("track-name").value, "");
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /created and selected/i,
  );
}

async function testCreateTrackAutoSelectsNewTrack() {
  const harness = createHarness();
  await harness.settle();

  harness.getElement("track-name").value = "Auto Select Raceway";
  harness.context._trackDraft.startFinish = {
    p1: { lat: 31.2300, lon: 121.4700 },
    p2: { lat: 31.2300, lon: 121.4702 },
    heading: 90,
    flipped: false,
  };
  harness.context.renderTrackDraft();

  harness.enqueueResponse((url, options) => {
    return url === "/api/tracks" &&
      (((options || {}).method || "GET").toUpperCase() === "POST");
  }, {
    ok: true,
    body: { ok: true, id: "track_123", name: "Auto Select Raceway" },
  });
  harness.enqueueResponse("/api/tracks/select", {
    ok: true,
    body: { ok: true, name: "Auto Select Raceway" },
  });

  harness.context.addTrack();
  await harness.settle();
  await harness.settle();

  assert.equal(
    harness.countTrackSelectPostCalls(),
    1,
    "track creation should auto-select the newly created track",
  );
  const selectCall = harness.calls.find((call) => call.url === "/api/tracks/select");
  assert.ok(selectCall, "expected a follow-up /api/tracks/select call");
  assert.equal(
    JSON.parse(selectCall.options.body).id,
    "track_123",
    "auto-select should target the id returned by track creation",
  );
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /created and selected/i,
  );
}

async function testCreateTrackSurfacesAutoSelectFailure() {
  const harness = createHarness();
  await harness.settle();

  harness.getElement("track-name").value = "Needs Manual Select";
  harness.context._trackDraft.startFinish = {
    p1: { lat: 31.2400, lon: 121.4800 },
    p2: { lat: 31.2400, lon: 121.4802 },
    heading: 90,
    flipped: false,
  };
  harness.context.renderTrackDraft();

  harness.enqueueResponse((url, options) => {
    return url === "/api/tracks" &&
      (((options || {}).method || "GET").toUpperCase() === "POST");
  }, {
    ok: true,
    body: { ok: true, id: "track_124", name: "Needs Manual Select" },
  });
  harness.enqueueResponse("/api/tracks/select", {
    ok: false,
    body: { error: "track not found" },
  });

  harness.context.addTrack();
  await harness.settle();
  await harness.settle();

  assert.equal(
    harness.countTrackSelectPostCalls(),
    1,
    "auto-select should still be attempted when create succeeds",
  );
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /created.*failed to select/i,
  );
}

async function testStatusRendersCurrentTrackAndBlockedRecordingReason() {
  const harness = createHarness();
  await harness.settle();

  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 8,
    lat: 31.2304167,
    lon: 121.4737010,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
    track_id: "",
    track_source: "Auto-detected",
    track_locked_manual: false,
    recording_cta_state: "blocked_no_track",
    recording_cta_reason: "Select a track before recording.",
  });

  assert.match(
    harness.getElement("current-track-name").textContent,
    /No Track selected/i,
    "status should render a Current Track banner in the UI",
  );
  assert.match(
    harness.getElement("current-track-source").textContent,
    /Auto-detected/i,
    "status should render track source metadata",
  );
  assert.equal(
    harness.getElement("rec-btn").disabled,
    true,
    "recording CTA should be disabled when no valid track is selected",
  );
  assert.match(
    harness.getElement("rec-btn").textContent,
    /Select Track to Record/i,
    "recording CTA should show a contextual blocked label",
  );
  assert.match(
    harness.getElement("rec-cta-reason").textContent,
    /Select a track before recording\./i,
    "recording CTA should include a concrete blocked reason",
  );
}

async function testNearbyTracksRenderSortedAndSupportSwitching() {
  const harness = createHarness();
  await harness.settle();

  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 9,
    lat: 31.2304167,
    lon: 121.4737010,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "Shanghai International Circuit",
    track_id: "track_002",
    track_source: "Selected manually",
    track_locked_manual: true,
    recording_cta_state: "ready",
    recording_cta_reason: "",
    current_track_distance_m: 36,
    nearby_tracks: [
      { id: "track_001", name: "Ningbo Kart Center", distance_m: 96 },
      { id: "track_003", name: "Zhuhai International Circuit", distance_m: 18 },
    ],
  });

  assert.match(
    harness.getElement("current-track-distance").textContent,
    /36 m/i,
    "current track should show distance from the current position",
  );

  const nearbyMarkup = harness.getElement("nearby-tracks").innerHTML;
  assert.match(nearbyMarkup, /Nearby Tracks/i);
  assert.match(nearbyMarkup, /Resume Auto/i);
  assert.match(nearbyMarkup, /Use This Track/i);
  assert.ok(
    nearbyMarkup.indexOf("Zhuhai International Circuit") <
      nearbyMarkup.indexOf("Ningbo Kart Center"),
    "nearby candidates should be sorted by ascending distance",
  );

  harness.enqueueResponse("/api/tracks/select", {
    ok: true,
    body: { ok: true, name: "Zhuhai International Circuit" },
  });
  harness.getElement("nearby-tracks").onclick({
    target: {
      getAttribute(name) {
        if (name === "data-nearby-action") { return "select"; }
        if (name === "data-track-id") { return "track_003"; }
        return null;
      },
    },
  });
  await harness.settle();

  const manualSwitchCall = harness.calls.find((call) => {
    return call.url === "/api/tracks/select" &&
      /track_003/.test((call.options || {}).body || "");
  });
  assert.ok(manualSwitchCall, "nearby switch action should post to /api/tracks/select");
  assert.match(manualSwitchCall.options.body, /"source":"manual"/);

  harness.enqueueResponse("/api/tracks/select", {
    ok: true,
    body: { ok: true, name: "Ningbo Kart Center" },
  });
  harness.getElement("nearby-tracks").onclick({
    target: {
      getAttribute(name) {
        if (name === "data-nearby-action") { return "auto"; }
        return null;
      },
    },
  });
  await harness.settle();

  const autoResumeCall = harness.calls.find((call) => {
    return call.url === "/api/tracks/select" &&
      /"source":"auto"/.test((call.options || {}).body || "");
  });
  assert.ok(autoResumeCall, "resume auto action should post auto selection intent");

  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 10,
    lat: 31.2304192,
    lon: 121.4737032,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "Shanghai International Circuit",
    track_id: "track_002",
    track_source: "Selected manually",
    track_locked_manual: true,
    recording_cta_state: "ready",
    recording_cta_reason: "",
    current_track_distance_m: 42,
    nearby_tracks: [
      { id: "track_003", name: "Zhuhai International Circuit", distance_m: 22 },
      { id: "track_001", name: "Ningbo Kart Center", distance_m: 88 },
    ],
  });

  assert.match(
    harness.getElement("current-track-name").textContent,
    /Shanghai International Circuit/i,
    "manual selection should survive later idle GPS refreshes until auto mode is resumed",
  );
}

async function testGuidedTrackReviewStageSupportsRemarkingSinglePoints() {
  const harness = createHarness();
  await harness.settle();

  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 9,
    lat: 31.2304167,
    lon: 121.4737010,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
    track_id: "",
    track_source: "Waiting to select",
    track_locked_manual: false,
    recording_cta_state: "blocked_no_track",
    recording_cta_reason: "Select a track before recording.",
  });
  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 9,
    lat: 31.2304168,
    lon: 121.4737011,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
  });
  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 9,
    lat: 31.2304169,
    lon: 121.4737012,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
  });

  harness.getElement("track-name").value = "Sprint Layout";
  harness.context.renderTrackDraft();

  enqueueStatusSamples(harness, [
    sampledResponse(31.2304167, 121.4737010),
    sampledResponse(31.2304168, 121.4737011),
    sampledResponse(31.2304169, 121.4737012),
    sampledResponse(31.2304170, 121.4737013),
    sampledResponse(31.2304171, 121.4737014),
    sampledResponse(31.2304172, 121.4737015),
  ]);
  harness.context.markStartFinishPoint("p1");
  await harness.settle();
  await harness.tick(2400);

  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 9,
    lat: 31.2304366,
    lon: 121.4737407,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
  });
  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 9,
    lat: 31.2304367,
    lon: 121.4737408,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
  });
  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 9,
    lat: 31.2304368,
    lon: 121.4737409,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
  });

  enqueueStatusSamples(harness, [
    sampledResponse(31.2304366, 121.4737407),
    sampledResponse(31.2304367, 121.4737408),
    sampledResponse(31.2304368, 121.4737409),
    sampledResponse(31.2304369, 121.4737410),
    sampledResponse(31.2304370, 121.4737411),
    sampledResponse(31.2304371, 121.4737412),
  ]);
  harness.context.markStartFinishPoint("p2");
  await harness.settle();
  await harness.tick(2400);

  assert.match(
    harness.getElement("creation-step-name").className,
    /done/i,
    "name step should be marked complete once a name is entered",
  );
  assert.match(
    harness.getElement("creation-step-start-finish").className,
    /done/i,
    "start/finish step should be marked complete once both points exist",
  );
  assert.match(
    harness.getElement("creation-step-review").className,
    /active/i,
    "review step should become active once geometry is ready",
  );
  assert.match(
    harness.getElement("geometry-review").innerHTML,
    /<svg/i,
    "review stage should render a geometry preview before submit",
  );
  assert.match(
    harness.getElement("geometry-review").innerHTML,
    /North|review-north/i,
    "review stage should include a north reference",
  );
  assert.match(
    harness.getElement("geometry-review").innerHTML,
    /Line length/i,
    "review stage should include a measured line-length annotation",
  );
  assert.match(
    harness.getElement("geometry-review").innerHTML,
    /Crossing/i,
    "review stage should include crossing direction metadata",
  );
  assert.match(
    harness.getElement("geometry-review").innerHTML,
    /Good - ready to save|Acceptable - short lines may drift|Noisy - try again/i,
    "review stage should surface an action-oriented confidence label",
  );
  assert.match(
    harness.getElement("review-copy").textContent,
    /becomes current/i,
    "review stage should explain that a new track becomes current",
  );
  assert.match(
    harness.getElement("review-copy").textContent,
    /do not close or refresh/i,
    "review copy should warn that track creation does not survive refreshes",
  );
  assert.match(harness.getElement("sf-p1-btn").textContent, /Re-mark P1/i);
  assert.match(harness.getElement("sf-p2-btn").textContent, /Re-mark P2/i);

  const originalP2Lat = harness.context._trackDraft.startFinish.p2.lat;
  const originalP2Lon = harness.context._trackDraft.startFinish.p2.lon;

  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 9,
    lat: 31.2304100,
    lon: 121.4736923,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
  });
  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 9,
    lat: 31.2304101,
    lon: 121.4736924,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
  });
  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 9,
    lat: 31.2304102,
    lon: 121.4736925,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "No Track",
  });

  enqueueStatusSamples(harness, [
    sampledResponse(31.2304100, 121.4736923),
    sampledResponse(31.2304101, 121.4736924),
    sampledResponse(31.2304102, 121.4736925),
    sampledResponse(31.2304103, 121.4736926),
    sampledResponse(31.2304104, 121.4736927),
    sampledResponse(31.2304105, 121.4736928),
  ]);
  harness.context.markStartFinishPoint("p1");
  await harness.settle();
  await harness.tick(2400);

  assert.equal(
    harness.context._trackDraft.startFinish.p2.lat,
    originalP2Lat,
    "re-marking P1 should not clear P2",
  );
  assert.equal(
    harness.context._trackDraft.startFinish.p2.lon,
    originalP2Lon,
    "re-marking P1 should preserve the other endpoint",
  );
}

async function testAdvancedSettingsStartCollapsed() {
  const harness = createHarness();
  await harness.settle();

  assert.equal(
    harness.getElement("advanced-settings-panel").style.display,
    "none",
    "advanced settings should start collapsed",
  );

  harness.context.toggleAdvancedSettings();
  assert.equal(
    harness.getElement("advanced-settings-panel").style.display,
    "block",
    "advanced settings should expand when the affordance is used",
  );
}

async function testSessionCardsRenderMetadataAndEncodedDownloads() {
  const harness = createHarness();
  await harness.settle();

  harness.enqueueResponse("/api/sessions", {
    sessions: [
      {
        filename: "20260408_Ningbo Kart Center_140530_001.vbo",
        date: "2026-04-08 14:05",
        track: "Ningbo Kart Center",
        best_lap_ms: 50890,
      },
    ],
  });
  harness.context.loadSessions();
  await harness.settle();

  const sessions = harness.getElement("sessions");
  assert.equal(sessions.children.length, 1, "session list should render one session card");
  assert.match(sessions.children[0].innerHTML, /Date/);
  assert.match(sessions.children[0].innerHTML, /Track/);
  assert.match(sessions.children[0].innerHTML, /Best Lap/);
  assert.match(sessions.children[0].innerHTML, /50\.890s/);
  assert.match(
    sessions.children[0].innerHTML,
    /\/files\/20260408_Ningbo%20Kart%20Center_140530_001\.vbo/,
    "download href should URL-encode spaces in session filenames",
  );
}

(async function main() {
  await testGpsFixAllowsZeroZeroCoordinates();
  await testMarkingUsesFreshStatusAndRejectsZeroLengthLine();
  await testGpsStabilityControlsMarkingState();
  await testGpsBarShowsFreshnessBuckets();
  await testTrackCreationUsesAdaptivePollingWithoutOverlap();
  await testMarkingRequiresGpsStability();
  await testSampledCaptureUsesTimedMedianAndSamplingCadence();
  await testSampledCaptureCanCancelAndRejectConcurrentMarks();
  await testSampledCaptureFailsOnGpsLossOrTooFewSamples();
  await testSectorMarkingUsesSharedSampledCapturePath();
  await testRepeatabilityCheckCanUpgradeShortLineConfidence();
  await testRepeatabilityCheckBlocksShortLineOnMismatch();
  await testRepeatabilityStateResetsAfterLineChangesAndInheritsFlip();
  await testSamplingIgnoresPreTapStatusRequestsUntilFreshFetchArrives();
  await testIncompleteSectorBlocksTrackCreation();
  await testCreateTrackGuardsAgainstDoubleSubmit();
  await testCreateTrackAutoSelectsNewTrack();
  await testCreateTrackSurfacesAutoSelectFailure();
  await testStatusRendersCurrentTrackAndBlockedRecordingReason();
  await testNearbyTracksRenderSortedAndSupportSwitching();
  await testGuidedTrackReviewStageSupportsRemarkingSinglePoints();
  await testAdvancedSettingsStartCollapsed();
  await testSessionCardsRenderMetadataAndEncodedDownloads();
  console.log("check_firmware_web_ui: PASS");
})().catch((error) => {
  console.error(error && error.stack ? error.stack : error);
  process.exit(1);
});
