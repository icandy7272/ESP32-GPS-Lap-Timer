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

  function formatUptime(secondsValue) {
    const seconds = Math.max(0, toInt(secondsValue, 0));
    const minutes = Math.floor(seconds / 60);
    const remainingSeconds = seconds % 60;
    return minutes + "m " + remainingSeconds + "s";
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

  function normalizeVariant(value) {
    const normalized = String(value || "original")
      .trim()
      .toLowerCase();
    if (normalized === "polished") {
      return "polished";
    }
    return "original";
  }

  function normalizeSectorState(value) {
    const normalized = String(value || "completed")
      .trim()
      .toLowerCase()
      .replace(/_/g, "-");

    if (
      normalized === "best" ||
      normalized === "focus" ||
      normalized === "live" ||
      normalized === "pending" ||
      normalized === "completed"
    ) {
      return normalized;
    }

    return "completed";
  }

  function getSectorEntries(device) {
    const sectors = Array.isArray(device && device.sectors) ? device.sectors : [];
    return sectors.slice(0, 4).map(function (sector, index) {
      const hasDelta = Boolean(sector) && Object.prototype.hasOwnProperty.call(sector, "delta_ms");
      return {
        label: (sector && sector.label) || "S" + String(index + 1),
        state: normalizeSectorState(sector && sector.state),
        deltaMs: hasDelta ? toInt(sector.delta_ms, 0) : null,
      };
    });
  }

  function formatSectorValue(sector) {
    if (!sector) {
      return "--";
    }

    if (sector.state === "live") {
      return "LIVE";
    }

    if (sector.state === "pending") {
      return "--";
    }

    if (sector.state === "best") {
      return "BEST";
    }

    if (sector.deltaMs == null) {
      return "--";
    }

    return formatDelta(sector.deltaMs);
  }

  function getSectorToneClass(sector) {
    if (!sector) {
      return "device-sector--pending";
    }

    if (sector.state === "best") {
      return "device-sector--best";
    }

    if (sector.state === "live") {
      return "device-sector--live";
    }

    if (sector.state === "pending") {
      return "device-sector--pending";
    }

    if (sector.deltaMs == null || sector.deltaMs === 0) {
      return "device-sector--neutral";
    }

    return sector.deltaMs < 0 ? "device-sector--fast" : "device-sector--slow";
  }

  function findFocusedSector(sectors) {
    return sectors.find(function (sector) {
      return sector.state === "focus";
    }) || null;
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

  function resolveDrivingHeroState(status, device, variant, sectors) {
    const baseState = resolveDrivingDeltaState(status, device);
    const gpsFix = Boolean(status && status.gps_fix);

    if (variant !== "polished" || !gpsFix || (device && device.off_track)) {
      return {
        text: baseState.text,
        stateClass: baseState.stateClass,
        eyebrow: "",
        label: "",
      };
    }

    const focusedSector = findFocusedSector(sectors);
    if (!focusedSector) {
      return {
        text: baseState.text,
        stateClass: baseState.stateClass,
        eyebrow: "",
        label: "",
      };
    }

    let toneClass = "device-driving--delta-even";
    if (focusedSector.state === "best" || (focusedSector.deltaMs != null && focusedSector.deltaMs < 0)) {
      toneClass = "device-driving--delta-fast";
    } else if (focusedSector.deltaMs != null && focusedSector.deltaMs > 0) {
      toneClass = "device-driving--delta-slow";
    }

    return {
      text: formatSectorValue(focusedSector),
      stateClass: toneClass + " device-driving--sector-focus",
      eyebrow: "Sector Delta",
      label: focusedSector.label,
    };
  }

  function renderBootBlock(status, device) {
    const bootState = resolveBootState(device, status);
    const satellites = Math.max(0, toInt(status && status.satellites, 0));
    const trackName = escapeHtml((status && status.track) || "No track");
    const firmwareVersion = escapeHtml((device && device.firmware_version) || "v1.0.0");

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
      '<div class="device-boot__subline">' +
      firmwareVersion +
      "</div>" +
      "</div>"
    );
  }

  function renderSectorRibbon(sectors) {
    if (!Array.isArray(sectors) || sectors.length === 0) {
      return "";
    }

    return (
      '<div class="device-sector-ribbon">' +
      sectors
        .map(function (sector) {
          return (
            '<div class="device-sector ' +
            getSectorToneClass(sector) +
            (sector.state === "focus" ? " device-sector--focus" : "") +
            '">' +
            '<span class="device-sector__label">' +
            escapeHtml(sector.label) +
            "</span>" +
            '<span class="device-sector__value device-time">' +
            escapeHtml(formatSectorValue(sector)) +
            "</span>" +
            "</div>"
          );
        })
        .join("") +
      "</div>"
    );
  }

  function renderDrivingBlock(status, device, variant) {
    const gpsFix = Boolean(status && status.gps_fix);
    const satellites = Math.max(0, toInt(status && status.satellites, 0));
    const currentLap = Math.max(0, toInt(status && status.current_lap, 0));
    const currentLapTimeMs = Math.max(0, toInt(device && device.current_lap_time_ms, 0));
    const bestLapMs = toInt(status && status.best_lap_ms, -1);
    const sectors = variant === "polished" ? getSectorEntries(device) : [];
    const deltaState = resolveDrivingHeroState(status, device, variant, sectors);
    const bestLabel = bestLapMs > 0 ? formatLapTime(bestLapMs) : "--:--.--";
    const hasSectorRibbon = variant === "polished" && sectors.length > 0;
    const referenceLabel = escapeHtml((device && device.sector_reference_label) || "Best lap sector split");

    return (
      '<div class="device-driving ' +
      deltaState.stateClass +
      (hasSectorRibbon ? " device-driving--has-sectors" : "") +
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
      (deltaState.eyebrow
        ? '<span class="device-driving__delta-eyebrow">' +
          escapeHtml(deltaState.eyebrow) +
          "</span>"
        : "") +
      (deltaState.label
        ? '<span class="device-driving__delta-label">' +
          escapeHtml(deltaState.label) +
          "</span>"
        : "") +
      '<span class="device-driving__delta-value">' +
      escapeHtml(deltaState.text) +
      "</span>" +
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
      (hasSectorRibbon ? renderSectorRibbon(sectors) : "") +
      (hasSectorRibbon
        ? '<div class="device-driving__reference">Reference: ' +
          referenceLabel +
          "</div>"
        : "") +
      "</div>"
    );
  }

  function renderStatusBlock(status, device, settings) {
    const gpsFix = Boolean(status && status.gps_fix);
    const satellites = Math.max(0, toInt(status && status.satellites, 0));
    const trackName = escapeHtml((status && status.track) || "No track");
    const recording = Boolean(status && status.recording);
    const laps = Array.isArray(device && device.laps) ? device.laps : [];
    const lapCount = laps.length > 0 ? laps.length : Math.max(0, toInt(device && device.lap_count, 0));
    const wifiSsid = escapeHtml((settings && settings.wifi_ssid) || "--");
    const sdFreeGb = Number(device && device.sd_free_gb);
    const sdLine = Number.isFinite(sdFreeGb) && sdFreeGb >= 0 ? "SD: " + sdFreeGb.toFixed(1) + " GB free" : "SD: --";
    const uptimeLine = "Uptime: " + formatUptime(device && device.uptime_seconds);
    const firmwareLine = "FW: " + escapeHtml((device && device.firmware_version) || "v1.0.0");
    const recordingLineClass = recording
      ? "device-status__line device-status__line--recording"
      : "device-status__line";

    function renderStatusLine(className, text) {
      return (
        '<div class="' +
        className +
        '"><span class="device-status__text">' +
        text +
        "</span></div>"
      );
    }

    return (
      '<div class="device-status">' +
      renderStatusLine("device-status__line", "GPS: " + satellites + " sats  Fix: " + (gpsFix ? "3D" : "No fix")) +
      renderStatusLine("device-status__line", "Track: " + trackName) +
      renderStatusLine(recordingLineClass, "Recording: " + (recording ? "REC" : "Idle")) +
      renderStatusLine("device-status__line", "Laps: " + lapCount + " completed") +
      renderStatusLine("device-status__line", "WiFi: " + wifiSsid) +
      renderStatusLine("device-status__line", sdLine) +
      renderStatusLine("device-status__line", "Battery: N/A") +
      renderStatusLine("device-status__line", uptimeLine) +
      renderStatusLine("device-status__line device-status__line--firmware", firmwareLine) +
      "</div>"
    );
  }

  function renderScenarioLapRows(device, status) {
    const laps = Array.isArray(device && device.laps) ? device.laps : [];
    if (laps.length === 0) {
      return "";
    }

    const bestLapMs = toInt(status && status.best_lap_ms, -1);
    return laps
      .slice(0, 7)
      .map(function (lap, index) {
        const lapNumber = Math.max(0, toInt(lap && lap.lap_number, index + 1));
        const lapTimeMs = toInt(lap && lap.lap_time_ms, -1);
        const lapStatus = String((lap && lap.status) || "timed").trim().toLowerCase().replace(/_/g, "-");
        const isBest = bestLapMs > 0 && lapStatus === "timed" && lapTimeMs === bestLapMs;
        let suffix = "";
        if (isBest && lapTimeMs >= 0) {
          suffix = "BEST";
        } else if (lapStatus === "timed" && lapTimeMs >= 0) {
          if (bestLapMs >= 0) {
            suffix = formatDelta(lapTimeMs - bestLapMs);
          }
        }
        return (
          '<div class="device-lap-list__row' +
          (isBest ? " is-best" : "") +
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
          "</div>"
        );
      })
      .join("");
  }

  function renderSyntheticLapRows(device, status) {
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

  function renderLapListRows(device, status) {
    const scenarioRowsMarkup = renderScenarioLapRows(device, status);
    if (scenarioRowsMarkup) {
      return scenarioRowsMarkup;
    }

    return renderSyntheticLapRows(device, status);
  }

  function renderLapListBlock(status, device) {
    const laps = Array.isArray(device && device.laps) ? device.laps : [];
    const lapCount = laps.length > 0 ? laps.length : Math.max(0, toInt(device && device.lap_count, 0));
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

  function renderDeviceScreenMarkup(scenario, options) {
    const data = scenario || {};
    const status = data.status || {};
    const device = data.device || {};
    const settings = data.settings || {};
    const screen = normalizeRuntimeScreen(device.screen);
    const variant = normalizeVariant(options && options.variant);

    let bodyMarkup = "";
    if (screen === "boot") {
      bodyMarkup = renderBootBlock(status, device);
    } else if (screen === "status") {
      bodyMarkup = renderStatusBlock(status, device, settings);
    } else if (screen === "lap-list") {
      bodyMarkup = renderLapListBlock(status, device);
    } else {
      bodyMarkup = renderDrivingBlock(status, device, variant);
    }

    return (
      '<div class="device-preview device-preview--' +
      variant +
      '">' +
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

  function renderDeviceScreenComparisonMarkup(scenario) {
    return (
      '<div class="device-compare">' +
      '<section class="device-panel device-panel--original">' +
      '<div class="device-panel__header">' +
      '<h2 class="device-panel__title">Original TFT</h2>' +
      '<p class="device-panel__description">Baseline preview aligned to the current firmware shell.</p>' +
      "</div>" +
      renderDeviceScreenMarkup(scenario, { variant: "original" }) +
      "</section>" +
      '<section class="device-panel device-panel--polished">' +
      '<div class="device-panel__header">' +
      '<h2 class="device-panel__title">Polished TFT</h2>' +
      '<p class="device-panel__description">Same state, tuned for stronger contrast and hierarchy.</p>' +
      "</div>" +
      renderDeviceScreenMarkup(scenario, { variant: "polished" }) +
      "</section>" +
      "</div>"
    );
  }

  return {
    renderDeviceScreenMarkup: renderDeviceScreenMarkup,
    renderDeviceScreenComparisonMarkup: renderDeviceScreenComparisonMarkup,
  };
});
