const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const repoRoot = path.resolve(__dirname, "..", "..", "..");
const webUiSource = fs.readFileSync(
  path.join(repoRoot, "src/wifi/web_ui.cpp"),
  "utf8",
);

const scriptMatch = webUiSource.match(/return R"JS\(<script>\n([\s\S]*?)<\/script>\)JS";/);
assert.ok(scriptMatch, "Failed to extract embedded web UI script from src/wifi/web_ui.cpp");

const embeddedScript = scriptMatch[1];

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

  const context = {
    console,
    fetch,
    confirm() {
      return true;
    },
    setInterval() {
      return 1;
    },
    clearInterval() {},
    setTimeout() {
      return 1;
    },
    clearTimeout() {},
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

  return {
    context,
    calls,
    getElement,
    enqueueResponse,
    enqueueDeferredResponse,
    settle,
    countTrackPostCalls() {
      return calls.filter((call) => {
        return call.url === "/api/tracks" &&
          (((call.options || {}).method || "GET").toUpperCase() === "POST");
      }).length;
    },
  };
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

  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 4,
    lat: 31.2304160,
    lon: 121.4737004,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "",
  });
  harness.context.applyStatusData({
    gps_fix: true,
    satellites: 4,
    lat: 31.2304163,
    lon: 121.4737007,
    recording: false,
    current_lap: 0,
    best_lap_ms: -1,
    track: "",
  });

  const statusCallsBefore = harness.calls.filter((call) => call.url === "/api/status").length;
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

  const statusCallsAfter = harness.calls.filter((call) => call.url === "/api/status").length;
  assert.equal(
    statusCallsAfter,
    statusCallsBefore + 1,
    "marking should request a fresh /api/status snapshot",
  );
  assert.equal(harness.context._trackDraft.startFinish.p1.lat, 31.2304167);
  assert.equal(harness.context._trackDraft.startFinish.p1.lon, 121.4737010);

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
  harness.context.markStartFinishPoint("p2");
  await harness.settle();

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
    /Waiting for better GPS/i,
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
    /Stable - ready to mark/i,
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

async function testMarkingRequiresGpsStability() {
  const harness = createHarness();
  await harness.settle();

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

  pendingCreate.resolve({ ok: true, body: { ok: true } });
  await harness.settle();

  assert.equal(harness.getElement("track-name").value, "");
  assert.match(
    harness.getElement("track-create-msg").textContent,
    /successfully/i,
  );
}

(async function main() {
  await testGpsFixAllowsZeroZeroCoordinates();
  await testMarkingUsesFreshStatusAndRejectsZeroLengthLine();
  await testGpsStabilityControlsMarkingState();
  await testMarkingRequiresGpsStability();
  await testIncompleteSectorBlocksTrackCreation();
  await testCreateTrackGuardsAgainstDoubleSubmit();
  console.log("check_firmware_web_ui: PASS");
})().catch((error) => {
  console.error(error && error.stack ? error.stack : error);
  process.exit(1);
});
