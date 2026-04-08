(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
    return;
  }

  root.UiPreviewDeviceScreen = factory();
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  function escapeHtml(value) {
    return String(value == null ? "" : value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function toInt(value, fallback) {
    const number = Number(value);
    if (!Number.isFinite(number)) {
      return fallback;
    }
    return Math.trunc(number);
  }

  function formatDelta(deltaMs) {
    const normalized = toInt(deltaMs, 0);
    const sign = normalized >= 0 ? "+" : "-";
    const absValue = Math.abs(normalized);
    const seconds = Math.floor(absValue / 1000);
    const hundredths = Math.floor((absValue % 1000) / 10);
    return sign + seconds + "." + String(hundredths).padStart(2, "0");
  }

  function formatLapTime(timeMs) {
    const normalized = toInt(timeMs, -1);
    if (normalized < 0) {
      return "--:--.--";
    }

    const minutes = Math.floor(normalized / 60000);
    const seconds = Math.floor((normalized % 60000) / 1000);
    const hundredths = Math.floor((normalized % 1000) / 10);
    return (
      String(minutes) +
      ":" +
      String(seconds).padStart(2, "0") +
      "." +
      String(hundredths).padStart(2, "0")
    );
  }

  function normalizeBootState(value) {
    const normalized = String(value || "splash")
      .trim()
      .toLowerCase()
      .replace(/_/g, "-");
    if (
      normalized === "splash" ||
      normalized === "gps-searching" ||
      normalized === "track-found" ||
      normalized === "recovery" ||
      normalized === "ready"
    ) {
      return normalized;
    }
    return "splash";
  }

  function resolveBootState(device, status) {
    if (device && typeof device.boot_state === "string") {
      return normalizeBootState(device.boot_state);
    }
    return status && status.gps_fix ? "ready" : "gps-searching";
  }

  function normalizeRuntimeScreen(value) {
    const normalized = String(value || "driving")
      .trim()
      .toLowerCase()
      .replace(/_/g, "-");

    if (normalized === "boot" || normalized === "driving" || normalized === "status" || normalized === "lap-list") {
      return normalized;
    }
    return "driving";
  }

  function resolveDrivingDeltaState(status, device) {
    const gpsFix = Boolean(status && status.gps_fix);
    if (!gpsFix) {
      return { text: "NO GPS", stateClass: "device-driving--no-gps" };
    }

    if (device && device.off_track) {
      return { text: "OFF TRACK", stateClass: "device-driving--off-track" };
    }

    if (!(device && device.delta_valid)) {
      return { text: "---", stateClass: "device-driving--delta-unavailable" };
    }

    const delta = toInt(device.delta_ms, 0);
    const polarityClass =
      delta < 0 ? "device-driving--delta-fast" : delta > 0 ? "device-driving--delta-slow" : "device-driving--delta-even";
    return { text: formatDelta(delta), stateClass: polarityClass };
  }

  function renderBootBlock(status, device) {
    const bootState = resolveBootState(device, status);
    const satellites = Math.max(0, toInt(status && status.satellites, 0));
    const trackName = escapeHtml((status && status.track) || "No track");

    if (bootState === "gps-searching") {
      return (
        '<div class="device-boot device-boot--gps-searching">' +
        '<div class="device-boot__headline">GPS Searching...</div>' +
        '<div class="device-boot__subline">' +
        satellites +
        " satellites</div>" +
        "</div>"
      );
    }

    if (bootState === "track-found") {
      return (
        '<div class="device-boot device-boot--track-found">' +
        '<div class="device-boot__subline">Track:</div>' +
        '<div class="device-boot__headline">' +
        trackName +
        "</div>" +
        "</div>"
      );
    }

    if (bootState === "recovery") {
      return (
        '<div class="device-boot device-boot--recovery">' +
        '<div class="device-boot__headline">Session Recovered</div>' +
        "</div>"
      );
    }

    if (bootState === "ready") {
      return (
        '<div class="device-boot device-boot--ready">' +
        '<div class="device-boot__headline">READY</div>' +
        "</div>"
      );
    }

    return (
      '<div class="device-boot device-boot--splash">' +
      '<div class="device-boot__headline">GPS Lap Timer</div>' +
      '<div class="device-boot__subline">v1.0.0</div>' +
      "</div>"
    );
  }

  function renderDrivingBlock(status, device) {
    const gpsFix = Boolean(status && status.gps_fix);
    const satellites = Math.max(0, toInt(status && status.satellites, 0));
    const currentLap = Math.max(0, toInt(status && status.current_lap, 0));
    const currentLapTimeMs = Math.max(0, toInt(device && device.current_lap_time_ms, 0));
    const bestLapMs = toInt(status && status.best_lap_ms, -1);
    const deltaState = resolveDrivingDeltaState(status, device);
    const bestLabel = bestLapMs > 0 ? formatLapTime(bestLapMs) : "--:--.--";

    return (
      '<div class="device-driving ' +
      deltaState.stateClass +
      '">' +
      '<div class="device-driving__top">' +
      '<span class="device-driving__lap">L' +
      currentLap +
      "</span>" +
      '<span class="device-driving__time device-time">' +
      formatLapTime(currentLapTimeMs) +
      "</span>" +
      "</div>" +
      '<div class="device-driving__delta device-time">' +
      escapeHtml(deltaState.text) +
      "</div>" +
      '<div class="device-driving__bottom">' +
      '<span class="device-driving__best device-time">Best:' +
      bestLabel +
      "</span>" +
      '<span class="device-driving__gps">' +
      (gpsFix ? "*" : "?") +
      satellites +
      " sats</span>" +
      "</div>" +
      "</div>"
    );
  }

  function renderStatusBlock(status, device, settings) {
    const gpsFix = Boolean(status && status.gps_fix);
    const satellites = Math.max(0, toInt(status && status.satellites, 0));
    const trackName = escapeHtml((status && status.track) || "No track");
    const recording = Boolean(status && status.recording);
    const lapCount = Math.max(0, toInt(device && device.lap_count, 0));
    const wifiSsid = escapeHtml((settings && settings.wifi_ssid) || "--");
    const recordingLineClass = recording
      ? "device-status__line device-status__line--recording"
      : "device-status__line";

    return (
      '<div class="device-status">' +
      '<div class="device-status__line">GPS: ' +
      satellites +
      " sats  Fix: " +
      (gpsFix ? "3D" : "No fix") +
      "</div>" +
      '<div class="device-status__line">Track: ' +
      trackName +
      "</div>" +
      '<div class="' +
      recordingLineClass +
      '">Recording: ' +
      (recording ? "REC" : "Idle") +
      "</div>" +
      '<div class="device-status__line">Laps: ' +
      lapCount +
      " completed</div>" +
      '<div class="device-status__line">WiFi: ' +
      wifiSsid +
      "</div>" +
      '<div class="device-status__line">SD: --</div>' +
      '<div class="device-status__line">Battery: N/A</div>' +
      '<div class="device-status__line">Uptime: 0m 0s</div>' +
      '<div class="device-status__line device-status__line--firmware">FW: v1.0.0</div>' +
      "</div>"
    );
  }

  function renderLapListRows(device, status) {
    const lapCount = Math.max(0, toInt(device && device.lap_count, 0));
    if (lapCount === 0) {
      return '<div class="device-lap-list__empty">No laps yet</div>';
    }

    const bestLapMs = toInt(status && status.best_lap_ms, -1);
    const bestLapNumber = toInt(device && device.best_lap_number, -1);
    const rowCount = Math.min(lapCount, 7);
    let rowsMarkup = "";

    for (let lapNumber = 1; lapNumber <= rowCount; lapNumber += 1) {
      let lapTimeMs = bestLapMs > 0 ? bestLapMs + (lapNumber - rowCount) * 120 : 60000 + lapNumber * 250;
      if (lapNumber === bestLapNumber && bestLapMs > 0) {
        lapTimeMs = bestLapMs;
      }
      if (lapTimeMs < 0) {
        lapTimeMs = 0;
      }

      let suffix = "";
      if (lapNumber === bestLapNumber && bestLapMs > 0) {
        suffix = "BEST";
      } else if (bestLapMs > 0) {
        suffix = formatDelta(lapTimeMs - bestLapMs);
      }

      rowsMarkup +=
        '<div class="device-lap-list__row' +
        (lapNumber === bestLapNumber && bestLapMs > 0 ? " is-best" : "") +
        '">' +
        '<span class="device-lap-list__lap">' +
        String(lapNumber).padStart(2, " ") +
        "</span>" +
        '<span class="device-lap-list__time device-time">' +
        formatLapTime(lapTimeMs) +
        "</span>" +
        '<span class="device-lap-list__delta device-time">' +
        escapeHtml(suffix) +
        "</span>" +
        "</div>";
    }

    return rowsMarkup;
  }

  function renderLapListBlock(status, device) {
    const lapCount = Math.max(0, toInt(device && device.lap_count, 0));
    return (
      '<div class="device-lap-list">' +
      '<div class="device-lap-list__title">Lap List</div>' +
      '<div class="device-lap-list__header">SESSION: ' +
      lapCount +
      " laps</div>" +
      '<div class="device-lap-list__rows">' +
      renderLapListRows(device, status) +
      "</div>" +
      "</div>"
    );
  }

  function renderDeviceScreenMarkup(scenario) {
    const data = scenario || {};
    const status = data.status || {};
    const device = data.device || {};
    const settings = data.settings || {};
    const screen = normalizeRuntimeScreen(device.screen);

    let bodyMarkup = "";
    if (screen === "boot") {
      bodyMarkup = renderBootBlock(status, device);
    } else if (screen === "status") {
      bodyMarkup = renderStatusBlock(status, device, settings);
    } else if (screen === "lap-list") {
      bodyMarkup = renderLapListBlock(status, device);
    } else {
      bodyMarkup = renderDrivingBlock(status, device);
    }

    return (
      '<div class="device-preview">' +
      '<div class="device-frame">' +
      '<div class="device-frame__header">' +
      '<span class="device-frame__title">ESP32 TFT</span>' +
      '<span class="device-frame__resolution">320x240</span>' +
      "</div>" +
      '<div class="device-screen device-screen--' +
      screen +
      '">' +
      bodyMarkup +
      "</div>" +
      "</div>" +
      "</div>"
    );
  }

  return {
    renderDeviceScreenMarkup: renderDeviceScreenMarkup,
  };
});
