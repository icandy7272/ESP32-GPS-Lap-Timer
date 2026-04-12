(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
    return;
  }

  root.UiPreviewWebConsole = factory();
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  function escapeHtml(value) {
    return String(value == null ? "" : value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function hasOwn(object, key) {
    return Boolean(object) && Object.prototype.hasOwnProperty.call(object, key);
  }

  function toFiniteNumber(value, fallback) {
    var normalized = Number(value);
    return Number.isFinite(normalized) ? normalized : fallback;
  }

  function formatBestLap(bestLapMs) {
    var normalized = Number(bestLapMs) || 0;
    if (normalized <= 0) {
      return "--";
    }

    return (normalized / 1000).toFixed(3) + "s";
  }

  function normalizeHeading(deg) {
    var value = toFiniteNumber(deg, 0) % 360;
    if (value < 0) {
      value += 360;
    }
    return value;
  }

  function bearingDeg(p1, p2) {
    var lat1 = p1.lat * Math.PI / 180;
    var lat2 = p2.lat * Math.PI / 180;
    var dLon = (p2.lon - p1.lon) * Math.PI / 180;
    var y = Math.sin(dLon) * Math.cos(lat2);
    var x = Math.cos(lat1) * Math.sin(lat2) -
      Math.sin(lat1) * Math.cos(lat2) * Math.cos(dLon);
    return normalizeHeading(Math.atan2(y, x) * 180 / Math.PI);
  }

  function crossingHeadingDeg(p1, p2, flipped) {
    var heading = normalizeHeading(bearingDeg(p1, p2) + 90);
    if (flipped) {
      heading = normalizeHeading(heading + 180);
    }
    return heading;
  }

  function compassLabel(deg) {
    var labels = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"];
    return labels[Math.floor((normalizeHeading(deg) + 22.5) / 45) % 8];
  }

  function headingArrow(deg) {
    var arrows = ["↑", "↗", "→", "↘", "↓", "↙", "←", "↖"];
    return arrows[Math.floor((normalizeHeading(deg) + 22.5) / 45) % 8];
  }

  function clonePoint(point) {
    if (!point) {
      return null;
    }
    var lat = toFiniteNumber(point.lat, null);
    var lon = toFiniteNumber(point.lon, null);
    if (lat == null || lon == null) {
      return null;
    }
    return { lat: lat, lon: lon };
  }

  function formatCoord(point, fallback) {
    if (!point) {
      return fallback || "Not set";
    }
    return point.lat.toFixed(7) + ", " + point.lon.toFixed(7);
  }

  function renderEmptyListItem(label) {
    return '<li class="web-console__item web-console__item--empty">' + escapeHtml(label) + "</li>";
  }

  function getRecordingCtaLabel(isRecording) {
    return isRecording ? "Stop Recording" : "Start Recording";
  }

  function renderSessionsMarkup(sessions) {
    if (!Array.isArray(sessions) || sessions.length === 0) {
      return renderEmptyListItem("No sessions");
    }

    return sessions
      .map(function (sessionName) {
        var safeSession = escapeHtml(sessionName);
        return (
          '<li class="web-console__item">' +
          '<span class="web-console__session-link">' +
          safeSession +
          "</span>" +
          "</li>"
        );
      })
      .join("");
  }

  function renderTracksMarkup(tracks) {
    if (!Array.isArray(tracks) || tracks.length === 0) {
      return renderEmptyListItem("No tracks");
    }

    return tracks
      .map(function (track) {
        var trackId = escapeHtml((track && track.id) || "unknown");
        var trackName = escapeHtml((track && track.name) || "Unknown");
        return (
          '<li class="web-console__item web-console__item--track">' +
          '<span class="web-console__track-name">' +
          trackName +
          " (" +
          trackId +
          ")" +
          "</span>" +
          '<span class="web-console__track-actions">' +
          '<button type="button" class="web-console__action-button" disabled>Select</button>' +
          '<button type="button" class="web-console__action-button web-console__action-button--danger" disabled>Delete</button>' +
          "</span>" +
          "</li>"
        );
      })
      .join("");
  }

  function normalizeLine(source) {
    var line = source || {};
    var p1 = clonePoint(line.p1);
    var p2 = clonePoint(line.p2);
    var flipped = Boolean(line.flipped);
    var heading = hasOwn(line, "heading") ? toFiniteNumber(line.heading, null) : null;

    if (heading == null && p1 && p2) {
      heading = crossingHeadingDeg(p1, p2, flipped);
    }

    return {
      p1: p1,
      p2: p2,
      heading: heading,
      flipped: flipped,
    };
  }

  function normalizeSector(source, index) {
    var normalized = normalizeLine(source);
    normalized.id = (source && source.id) || String(index + 1);
    return normalized;
  }

  function normalizeMessage(message) {
    var normalized = message || {};
    return {
      kind: normalized.kind || "",
      text: normalized.text || "",
    };
  }

  function normalizeTrackCreationState(data) {
    var status = data.status || {};
    var source = data.track_creation || data.trackCreation || {};
    var gpsSource = source.gps || {};
    var sectors = Array.isArray(source.sectors)
      ? source.sectors.map(function (sector, index) {
          return normalizeSector(sector, index);
        })
      : [];

    var gps = {
      fix: hasOwn(gpsSource, "fix") ? Boolean(gpsSource.fix) : Boolean(status.gps_fix),
      satellites: hasOwn(gpsSource, "satellites")
        ? toFiniteNumber(gpsSource.satellites, 0)
        : toFiniteNumber(status.satellites, 0),
      lat: hasOwn(gpsSource, "lat")
        ? toFiniteNumber(gpsSource.lat, 0)
        : toFiniteNumber(status.lat, 0),
      lon: hasOwn(gpsSource, "lon")
        ? toFiniteNumber(gpsSource.lon, 0)
        : toFiniteNumber(status.lon, 0),
    };

    gps.stability = String(
      gpsSource.stability ||
      (gps.fix
        ? (gps.satellites >= 4 ? "stable" : "low_sats")
        : "no_fix")
    );

    return {
      name: String(source.name || ""),
      gps: gps,
      startFinish: normalizeLine(source.start_finish || source.startFinish),
      sectors: sectors,
      sectorsExpanded: hasOwn(source, "sectorsExpanded")
        ? Boolean(source.sectorsExpanded)
        : (hasOwn(source, "sectors_expanded")
            ? Boolean(source.sectors_expanded)
            : sectors.length > 0),
      message: normalizeMessage(source.message),
      submitPending: hasOwn(source, "submitPending")
        ? Boolean(source.submitPending)
        : Boolean(source.submit_pending),
    };
  }

  function hasValidFix(draft) {
    return Boolean(draft && draft.gps && draft.gps.fix);
  }

  function getGpsStatusInfo(draft) {
    var state = (draft && draft.gps && draft.gps.stability) || "no_fix";

    if (state === "stable") {
      return {
        state: state,
        ready: true,
        statusClass: "ok",
        label: "Stable - ready to mark",
        barClass: "web-console__gps-bar--ok",
      };
    }

    if (state === "stabilizing") {
      return {
        state: state,
        ready: false,
        statusClass: "warn",
        label: "Hold still... stabilizing",
        barClass: "web-console__gps-bar--warn",
      };
    }

    if (state === "low_sats") {
      return {
        state: state,
        ready: false,
        statusClass: "err",
        label: "Waiting for better GPS",
        barClass: "web-console__gps-bar--err",
      };
    }

    return {
      state: "no_fix",
      ready: false,
      statusClass: "err",
      label: "Waiting for fix",
      barClass: "web-console__gps-bar--err",
    };
  }

  function canMarkWithCurrentGps(draft) {
    return getGpsStatusInfo(draft).ready;
  }

  function sectorIsComplete(sector) {
    return Boolean(sector && sector.p1 && sector.p2 && sector.heading != null);
  }

  function hasIncompleteSectors(draft) {
    return draft.sectors.some(function (sector) {
      return !sectorIsComplete(sector);
    });
  }

  function isCreateReady(draft) {
    return Boolean(
      draft.name.trim() &&
      draft.startFinish.p1 &&
      draft.startFinish.p2 &&
      draft.startFinish.heading != null &&
      !hasIncompleteSectors(draft) &&
      !draft.submitPending
    );
  }

  function renderGpsBar(draft) {
    var gpsStatus = getGpsStatusInfo(draft);
    var fixOk = hasValidFix(draft);
    var coords = fixOk
      ? formatCoord({ lat: draft.gps.lat, lon: draft.gps.lon }, "--")
      : "--";

    return (
      '<div class="web-console__gps-bar ' +
      gpsStatus.barClass +
      '">' +
      '<div class="web-console__gps-status">' +
      '<div class="web-console__gps-status-main">' +
      '<span class="web-console__gps-indicator"></span>' +
      '<span class="web-console__gps-text">' +
      escapeHtml(gpsStatus.label) +
      "</span>" +
      "</div>" +
      '<span class="web-console__gps-sats">' +
      escapeHtml(String(draft.gps.satellites)) +
      " sats</span>" +
      "</div>" +
      '<div class="web-console__helper">Current position: ' +
      escapeHtml(coords) +
      "</div>" +
      "</div>"
    );
  }

  function renderMarkRow(label, point, isMarked, isDisabled) {
    return (
      '<div class="web-console__mark-row">' +
      '<button type="button" class="web-console__mark-button' +
      (isMarked ? " web-console__mark-button--marked" : "") +
      (isDisabled ? " web-console__mark-button--disabled" : "") +
      '"' +
      (isDisabled ? " disabled" : "") +
      ">" +
      escapeHtml(isMarked ? "✓ " + label : "Mark " + label) +
      "</button>" +
      '<div class="web-console__coord">' +
      escapeHtml(formatCoord(point, "Not set")) +
      "</div>" +
      "</div>"
    );
  }

  function renderHeadingBar(heading) {
    return (
      '<div class="web-console__heading-bar">' +
      '<span class="web-console__heading-value">' +
      escapeHtml(headingArrow(heading) + " " + Math.round(heading) + "° " + compassLabel(heading)) +
      "</span>" +
      '<button type="button" class="web-console__inline-action">Flip</button>' +
      "</div>"
    );
  }

  function renderStartFinishSection(draft) {
    var startFinish = draft.startFinish;
    var p1Marked = Boolean(startFinish.p1);
    var p2Marked = Boolean(startFinish.p2);
    var canMark = canMarkWithCurrentGps(draft);
    var showHeading = startFinish.heading != null && p1Marked && p2Marked;

    return (
      '<div class="web-console__track-group">' +
      '<p class="web-console__helper web-console__helper--section">Start/Finish</p>' +
      renderMarkRow("P1", startFinish.p1, p1Marked, !canMark) +
      renderMarkRow("P2", startFinish.p2, p2Marked, !canMark || !p1Marked) +
      (showHeading ? renderHeadingBar(startFinish.heading) : "") +
      "</div>"
    );
  }

  function renderSectorCard(sector, index, draft) {
    var p1Marked = Boolean(sector.p1);
    var p2Marked = Boolean(sector.p2);
    var canMark = canMarkWithCurrentGps(draft);
    var headingMarkup = sectorIsComplete(sector)
      ? renderHeadingBar(sector.heading)
      : '<div class="web-console__helper">Mark P1 then P2 to calculate heading.</div>';

    return (
      '<div class="web-console__sector-card">' +
      '<div class="web-console__sector-head">' +
      '<strong>Sector ' +
      escapeHtml(String(index + 1)) +
      "</strong>" +
      '<button type="button" class="web-console__inline-action">Delete</button>' +
      "</div>" +
      renderMarkRow("P1", sector.p1, p1Marked, !canMark) +
      renderMarkRow("P2", sector.p2, p2Marked, !canMark || !p1Marked) +
      headingMarkup +
      "</div>"
    );
  }

  function renderSectorList(draft) {
    if (!draft.sectorsExpanded) {
      return "";
    }

    if (!draft.sectors.length) {
      return (
        '<div class="web-console__sector-list">' +
        '<div class="web-console__helper">No sectors yet. Add up to 3 optional split lines.</div>' +
        "</div>"
      );
    }

    return (
      '<div class="web-console__sector-list">' +
      draft.sectors
        .map(function (sector, index) {
          return renderSectorCard(sector, index, draft);
        })
        .join("") +
      "</div>"
    );
  }

  function renderTrackMessage(message) {
    var className = "web-console__track-msg";
    if (message.kind === "success") {
      className += " web-console__track-msg--success";
    }
    if (message.kind === "error") {
      className += " web-console__track-msg--error";
    }

    return '<div class="' + className + '">' + escapeHtml(message.text) + "</div>";
  }

  function renderTrackCreationMarkup(data) {
    var draft = normalizeTrackCreationState(data || {});
    var canCreate = isCreateReady(draft);
    var showAddSectorButton = draft.sectorsExpanded;
    var createLabel = draft.submitPending ? "Creating Track..." : "Create Track";

    return (
      '<div class="web-console__track-creation">' +
      '<h3 class="web-console__subheading">Track Creation</h3>' +
      '<input class="web-console__input" placeholder="Track name" value="' +
      escapeHtml(draft.name) +
      '" readonly />' +
      renderGpsBar(draft) +
      renderStartFinishSection(draft) +
      '<button type="button" class="web-console__collapse-toggle">' +
      escapeHtml(draft.sectorsExpanded ? "Hide Sector Splits (optional)" : "Show Sector Splits (optional)") +
      "</button>" +
      renderSectorList(draft) +
      (showAddSectorButton
        ? '<button type="button" class="web-console__secondary-button' +
          (draft.sectors.length >= 3 ? " web-console__secondary-button--disabled" : "") +
          '"' +
          (draft.sectors.length >= 3 ? " disabled" : "") +
          '>+ Add Sector</button>'
        : "") +
      '<button type="button" class="web-console__primary-button' +
      (canCreate ? "" : " web-console__primary-button--disabled") +
      '"' +
      (canCreate ? "" : " disabled") +
      ">" +
      escapeHtml(createLabel) +
      "</button>" +
      renderTrackMessage(draft.message) +
      "</div>"
    );
  }

  function renderWebConsoleMarkup(scenario) {
    var data = scenario || {};
    var status = data.status || {};
    var settings = data.settings || {};
    var isRecording = Boolean(status.recording);
    var gpsFix = status.gps_fix ? "Yes" : "No";
    var recordingState = isRecording ? "REC" : "Idle";
    var currentLap = Number(status.current_lap) || 0;
    var satellites = Number(status.satellites) || 0;
    var bestLap = formatBestLap(status.best_lap_ms);
    var trackName = status.track ? escapeHtml(status.track) : "None";
    var ssid = escapeHtml(settings.wifi_ssid || "");
    var wifiPass = escapeHtml(settings.wifi_pass || "");
    var brightness = Number(settings.brightness);
    var sessions = Array.isArray(data.sessions) ? data.sessions : [];
    var tracks = Array.isArray(data.tracks) ? data.tracks : [];

    return (
      '<div class="web-console-preview">' +
      '<div class="web-console__card" data-card="status">' +
      "<h2>Status</h2>" +
      '<div class="web-console__row"><span class="web-console__label">GPS Fix</span><span class="web-console__value">' +
      gpsFix +
      "</span></div>" +
      '<div class="web-console__row"><span class="web-console__label">Satellites</span><span class="web-console__value">' +
      satellites +
      "</span></div>" +
      '<div class="web-console__row"><span class="web-console__label">Recording</span><span class="web-console__value">' +
      recordingState +
      "</span></div>" +
      '<div class="web-console__row"><span class="web-console__label">Current Lap</span><span class="web-console__value">' +
      currentLap +
      "</span></div>" +
      '<div class="web-console__row"><span class="web-console__label">Best Lap</span><span class="web-console__value">' +
      bestLap +
      "</span></div>" +
      '<div class="web-console__row"><span class="web-console__label">Track</span><span class="web-console__value">' +
      trackName +
      "</span></div>" +
      '<button type="button" class="web-console__recording-cta' +
      (isRecording ? " web-console__recording-cta--active" : "") +
      '" disabled>' +
      getRecordingCtaLabel(isRecording) +
      "</button>" +
      "</div>" +
      '<div class="web-console__card" data-card="sessions">' +
      "<h2>Sessions</h2>" +
      '<ul class="web-console__list">' +
      renderSessionsMarkup(sessions) +
      "</ul>" +
      "</div>" +
      '<div class="web-console__card" data-card="tracks">' +
      "<h2>Tracks</h2>" +
      '<ul class="web-console__list">' +
      renderTracksMarkup(tracks) +
      "</ul>" +
      renderTrackCreationMarkup(data) +
      "</div>" +
      '<div class="web-console__card" data-card="settings">' +
      "<h2>Settings</h2>" +
      '<form class="web-console__form" aria-label="Settings form shell">' +
      '<label class="web-console__field">SSID<input class="web-console__input" value="' +
      ssid +
      '" disabled /></label>' +
      '<label class="web-console__field">Password<input class="web-console__input" type="password" value="' +
      wifiPass +
      '" disabled /></label>' +
      '<label class="web-console__field">Brightness<input class="web-console__input" value="' +
      (Number.isFinite(brightness) ? brightness : 0) +
      '" disabled /></label>' +
      '<button type="button" class="web-console__primary-button" disabled>Save Settings</button>' +
      "</form>" +
      "</div>" +
      "</div>"
    );
  }

  return {
    renderWebConsoleMarkup: renderWebConsoleMarkup,
  };
});
